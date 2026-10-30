#include <Windows.h>
#include <sdk/Interaction.h>
#include <sdk/Update.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>

#include "plugins/ambience-suite/Atmosphere.h"
#include "sdk/services/AmbientAudio.h"

namespace atmosphere {
#define APPEARANCE(BACKGROUND)                                                       \
    {"lighting", CD_SETTING_CHOICE, 0, 0, "theme", 0, 0, {"theme", "day", "night"}}, \
        {"show_background", CD_SETTING_TOGGLE, BACKGROUND},                          \
        {"background_corner_radius", CD_SETTING_INTEGER, 5, 0, "", 0, 32}, {         \
        "language", CD_SETTING_CHOICE, 0, 0, "zh-CN", 0, 0, {                        \
            "zh-CN", "en"                                                            \
        }                                                                            \
    }
#define MOTION                                                                               \
    {"animation", CD_SETTING_TOGGLE, 1}, {"speed", CD_SETTING_NUMBER, 0, .75, "", .25, 2}, { \
        "frame_rate", CD_SETTING_CHOICE, 0, 0, "20", 0, 0, {                                 \
            "15", "20", "30"                                                                 \
        }                                                                                    \
    }

cdsdk::Settings defaults(Spec spec) {
    if (spec.kind == Kind::Plant) {
        return {{APPEARANCE(spec.rows > 1),
                 MOTION,
                 {"species", CD_SETTING_CHOICE, 0, 0, "pothos", 0, 0, {"pothos", "succulent", "daisy"}},
                 {"mode", CD_SETTING_CHOICE, 0, 0, "ornamental", 0, 0, {"ornamental", "grow"}},
                 {"garden", CD_SETTING_TOGGLE, spec.columns >= 4},
                 {"show_info", CD_SETTING_TOGGLE, spec.rows > 1}}};
    }
    return {{APPEARANCE(1),
             MOTION,
             {"primary", CD_SETTING_INTEGER, 0, 0, "", 0, 5},
             {"preset", CD_SETTING_CHOICE, 0, 0, "rain", 0, 0, {"rain", "sea", "campfire", "custom"}},
             {"link", CD_SETTING_CHOICE, 0, 0, "independent", 0, 0, {"independent", "rain", "fire", "stars"}},
             {"master", CD_SETTING_INTEGER, 25, 0, "", 0, 100},
             {"timer_minutes", CD_SETTING_INTEGER, 30, 0, "", 0, 240},
             {"rain", CD_SETTING_INTEGER, 60, 0, "", 0, 100},
             {"ocean", CD_SETTING_INTEGER, 0, 0, "", 0, 100},
             {"stream", CD_SETTING_INTEGER, 0, 0, "", 0, 100},
             {"forest", CD_SETTING_INTEGER, 0, 0, "", 0, 100},
             {"fire", CD_SETTING_INTEGER, 0, 0, "", 0, 100},
             {"wind", CD_SETTING_INTEGER, 0, 0, "", 0, 100}}};
}

#undef APPEARANCE
#undef MOTION

namespace {
constexpr const char* kTracks[]{"rain", "ocean", "stream", "forest", "fire", "wind"};

struct Instance;
void noiseAction(Instance& self, const cybercanvas::SceneEvent& result);

struct Instance {
    Instance(Spec value, std::wstring directory)
        : spec(value), settings(defaults(value)), renderer(std::move(directory)) {
        model.plantedAt = wallSeconds();
        renderer.onAction([this](const auto& result) {
            if (!hidden) {
                noiseAction(*this, result);
            }
        });
        renderer.onDiagnostics([this](auto& stats) {
            stats.redraw = updates.stats();
        });
    }

    Spec spec;
    cdsdk::Settings settings;
    Model model;
    Renderer renderer;
    CdHostApi host{};
    CdAmbientApi ambient{};

    void* context = nullptr;
    CdRect safe{};
    double dpr = 1;
    cybercanvas::AnimationClock clock;
    cdsdk::Timer timer;
    cdsdk::RedrawRequests updates;
    uint64_t audio = 0, growthBucket = 0, countdownBucket = 0;
    bool hidden = false, pressed = false;
    double pressX = 0, pressY = 0;

    void redraw() {
        updates.invalidate();
    }

    void stopTimer() {
        timer.stop();
    }

    ~Instance() {
        renderer.cancelInput();
        stopTimer();
        if (audio && ambient.close) {
            ambient.close(context, audio);
        }
    }

    bool refreshAudio() {
        if (!audio || !ambient.snapshot) {
            return false;
        }
        CdAmbientSnapshot state{sizeof(state)};
        if (ambient.snapshot(context, audio, &state) != CD_OK) {
            return false;
        }
        const bool changed = model.audioStatus != state.status || model.audioError != state.error ||
                             model.master != state.config.master || model.deadline != state.deadline ||
                             !std::equal(model.levels.begin(), model.levels.end(), state.config.levels);
        model.audioStatus = state.status;
        model.audioError = state.error;
        model.master = state.config.master;
        model.deadline = state.deadline;
        std::copy(std::begin(state.config.levels), std::end(state.config.levels), model.levels.begin());
        if (spec.kind == Kind::Noise) {
            CdSettingValue sync[9]{};
            for (int i = 0; i < 6; ++i) {
                sync[i] = {sizeof(CdSettingValue), CD_SETTING_INTEGER, kTracks[i],
                           int64_t(std::lround(model.levels[i] * 100))};
            }
            sync[6] = {sizeof(CdSettingValue), CD_SETTING_INTEGER, "master", int64_t(std::lround(model.master * 100))};
            sync[7] = {sizeof(CdSettingValue), CD_SETTING_INTEGER, "timer_minutes", state.config.timer_minutes};
            const auto loudest = std::max_element(model.levels.begin(), model.levels.end());
            const auto primary =
                *loudest > 0 ? int64_t(loudest - model.levels.begin()) : settings.at("primary").integer;
            sync[8] = {sizeof(CdSettingValue), CD_SETTING_INTEGER, "primary", primary};
            settings.apply(sync, 9);
        }
        return changed;
    }
};

void retime(Instance& self);

void CD_CALL tick(void* pointer, uint64_t now) noexcept {
    try {
        auto& self = *static_cast<Instance*>(pointer);
        if (self.hidden) {
            return;
        }
        const double seconds = self.clock.tick(now).unscaledDelta;
        if (self.spec.kind == Kind::Noise) {
            bool changed = self.renderer.pollImages();
            changed |= self.refreshAudio();
            changed |= self.renderer.advanceFeedback(seconds);
            const bool animate = self.settings.at("animation").integer &&
                                 self.model.audioStatus == CD_AMBIENT_PLAYING && self.model.master > 0 &&
                                 std::any_of(self.model.levels.begin(), self.model.levels.end(), [](float level) {
                                     return level > 0;
                                 });
            const auto before = self.model.motion.seconds();
            self.model.advance(seconds, self.settings.at("speed").number, animate);
            changed |= before != self.model.motion.seconds();
            if (self.model.audioStatus == CD_AMBIENT_PLAYING && self.model.deadline) {
                const auto bucket = wallSeconds();
                changed |= bucket != self.countdownBucket;
                self.countdownBucket = bucket;
            }
            retime(self);
            if (changed) {
                self.redraw();
            }
            return;
        }
        const bool animate = self.settings.at("animation").integer != 0;
        const bool feedback = self.model.feedbackActive();
        const auto before = self.model.motion.seconds();
        self.model.advance(seconds, self.settings.at("speed").number, animate);
        bool changed = before != self.model.motion.seconds() || (feedback && seconds > 0);
        if (self.settings.at("mode").text == "grow") {
            const auto bucket =
                uint64_t(self.model.growth(wallSeconds(), self.settings.at("mode").text == "grow") * 1000);
            changed |= bucket != self.growthBucket;
            self.growthBucket = bucket;
        }
        retime(self);
        if (changed) {
            self.redraw();
        }
    } catch (...) {
    }
}

uint32_t subscriptionInterval(const Instance& self, const cdsdk::Settings& settings) {
    if (self.hidden) {
        return 0;
    }
    const bool animate = settings.at("animation").integer && settings.at("speed").number > 0;
    const bool noiseMotion = self.spec.kind == Kind::Noise && self.model.audioStatus == CD_AMBIENT_PLAYING &&
                             self.model.master > 0 &&
                             std::any_of(self.model.levels.begin(), self.model.levels.end(), [](float level) {
                                 return level > 0;
                             });
    const bool frames = self.spec.kind == Kind::Plant ? animate || self.model.feedbackActive() : animate && noiseMotion;
    if (self.spec.kind == Kind::Noise && !frames && self.renderer.feedbackActive()) {
        return 33;
    }
    if (frames) {
        const auto fps = uint32_t(std::stoi(settings.at("frame_rate").text));
        return (1000 + fps - 1) / fps;
    }
    return self.spec.kind == Kind::Noise || settings.at("mode").text == "grow" ? 1000 : 0;
}

CdResult subscribe(Instance& self, const cdsdk::Settings& settings, cdsdk::Timer& token) {
    token.bind(self.host, self.context);
    const auto interval = subscriptionInterval(self, settings);
    if (!interval) {
        return CD_OK;
    }
    return token.replace(interval, tick, &self);
}

void retime(Instance& self) {
    const auto interval = subscriptionInterval(self, self.settings);
    if (self.timer.interval() == interval) {
        return;
    }
    cdsdk::Timer replacement;
    if (subscribe(self, self.settings, replacement) != CD_OK) {
        return;
    }
    if (!self.timer.interval() || self.timer.interval() > interval) {
        self.clock.resetAnchor();
    }
    self.stopTimer();
    self.timer = std::move(replacement);
}

CdAmbientConfig configuration(const cdsdk::Settings& settings) {
    CdAmbientConfig value{sizeof(value), uint32_t(settings.at("timer_minutes").integer),
                          float(settings.at("master").integer / 100.)};
    for (int i = 0; i < 6; ++i) {
        value.levels[i] = float(settings.at(kTracks[i]).integer / 100.);
    }
    return value;
}

CdResult CD_CALL apply(void* pointer, const CdSettingValue* values, uint32_t count) noexcept {
    try {
        if (!pointer) {
            return CD_ERROR_ARGUMENT;
        }
        auto& self = *static_cast<Instance*>(pointer);
        auto next = self.settings;
        const auto result = next.apply(values, count);
        if (result != CD_OK) {
            return result;
        }
        if (self.spec.kind == Kind::Noise) {
            bool trackChanged = false;
            for (const auto* track : kTracks) {
                trackChanged |= next.at(track).integer != self.settings.at(track).integer;
            }
            if (trackChanged && next.at("preset").text == self.settings.at("preset").text) {
                const CdSettingValue custom{sizeof(custom), CD_SETTING_CHOICE, "preset", 0, 0, "custom"};
                next.apply(&custom, 1);
            }
            int select = -1;
            if (!trackChanged && next.at("primary").integer != self.settings.at("primary").integer) {
                select = int(next.at("primary").integer);
            } else if (!trackChanged && next.at("preset").text != self.settings.at("preset").text &&
                       next.at("preset").text != "custom") {
                select = next.at("preset").text == "sea" ? 1 : next.at("preset").text == "campfire" ? 4 : 0;
            }
            if (select >= 0) {
                CdSettingValue changes[8]{};
                for (int i = 0; i < 6; ++i) {
                    changes[i] = {sizeof(CdSettingValue), CD_SETTING_INTEGER, kTracks[i], i == select ? 60 : 0};
                }
                changes[6] = {sizeof(CdSettingValue), CD_SETTING_INTEGER, "primary", select};
                changes[7] = {sizeof(CdSettingValue),
                              CD_SETTING_CHOICE,
                              "preset",
                              0,
                              0,
                              select == 0   ? "rain"
                              : select == 1 ? "sea"
                              : select == 4 ? "campfire"
                                            : "custom"};
                next.apply(changes, 8);
            }
            int active = 0;
            for (const auto* track : kTracks) {
                active += next.at(track).integer > 0;
            }
            if (active > 4) {
                return CD_ERROR_ARGUMENT;
            }
        }
        cdsdk::Timer replacement;
        const auto subscribed = subscribe(self, next, replacement);
        if (subscribed != CD_OK) {
            return subscribed;
        }
        uint64_t group = self.audio;
        bool replaceGroup = false;
        if (self.spec.kind == Kind::Noise) {
            bool configChanged = next.at("master").integer != self.settings.at("master").integer ||
                                 next.at("timer_minutes").integer != self.settings.at("timer_minutes").integer;
            for (const auto* track : kTracks) {
                configChanged |= next.at(track).integer != self.settings.at(track).integer;
            }
            replaceGroup = !self.audio || next.at("link").text != self.settings.at("link").text;
            if (replaceGroup && self.ambient.open) {
                const auto key = next.at("link").text == "independent" ? "" : next.at("link").text.c_str();
                if (self.ambient.open(self.context, key, &group) != CD_OK) {
                    replacement.stop();
                    return CD_ERROR_INTERNAL;
                }
            }
            if (group && (configChanged || !replaceGroup || next.at("link").text == "independent")) {
                const auto config = configuration(next);
                const auto applied = self.ambient.configure(self.context, group, &config);
                if (applied != CD_OK) {
                    if (replaceGroup) {
                        self.ambient.close(self.context, group);
                    }
                    replacement.stop();
                    return applied;
                }
            }
        }
        self.stopTimer();
        self.timer = std::move(replacement);
        self.clock.resetAnchor();
        if (replaceGroup) {
            if (self.audio && self.ambient.close) {
                self.ambient.close(self.context, self.audio);
            }
            self.audio = group;
        }
        self.settings = std::move(next);
        if (self.spec.kind == Kind::Plant) {
            self.renderer.cancelInput();
        }
        if (self.spec.kind == Kind::Noise && !self.audio) {
            self.model.audioError = 1;
            self.model.master = float(self.settings.at("master").integer / 100.);
            for (int i = 0; i < 6; ++i) {
                self.model.levels[i] = float(self.settings.at(kTracks[i]).integer / 100.);
            }
        }
        self.refreshAudio();
        retime(self);
        self.redraw();
        return CD_OK;
    } catch (...) {
        return CD_ERROR_INTERNAL;
    }
}

void CD_CALL destroy(void* pointer) noexcept {
    const auto instance = std::unique_ptr<Instance>(static_cast<Instance*>(pointer));
}

void CD_CALL layout(void* pointer, const CdLayout* value) noexcept {
    if (!pointer || !value || value->struct_size < sizeof(*value)) {
        return;
    }
    auto& self = *static_cast<Instance*>(pointer);
    self.dpr = std::isfinite(value->device_pixel_ratio) ? std::clamp(value->device_pixel_ratio, .5, 4.) : 1;
    self.safe = value->safe_content;
    if (self.safe.width <= 0 || self.safe.height <= 0) {
        self.safe = {0, 0, value->logical_width, value->logical_height};
    }
    self.model.dark = value->text_color.r * .2126 + value->text_color.g * .7152 + value->text_color.b * .0722 > 140;
    self.pressed = false;
    self.renderer.cancelInput();

    self.redraw();
}

CdResult CD_CALL render(void* pointer, const CdFrame* frame) noexcept {
    if (!pointer || !frame || frame->struct_size < sizeof(*frame)) {
        return CD_ERROR_ARGUMENT;
    }
    auto& self = *static_cast<Instance*>(pointer);
    self.updates.beginFrame();
    return self.renderer.render(self.spec, self.settings, self.model, *frame, self.safe) ? CD_OK : CD_ERROR_ARGUMENT;
}

void setNoiseValue(Instance& self, const char* id, int64_t value) {
    if (self.settings.at(id).integer == value) {
        return;
    }
    const CdSettingValue change{sizeof(change), CD_SETTING_INTEGER, id, value};
    apply(&self, &change, 1);
}

void setTrack(Instance& self, int type, int64_t volume) {
    if (self.settings.at(kTracks[type]).integer == volume) {
        return;
    }
    CdSettingValue changes[2]{{sizeof(CdSettingValue), CD_SETTING_INTEGER, kTracks[type], volume}};
    uint32_t size = 1;
    int active = 0, quietest = -1;
    for (int i = 0; i < 6; ++i) {
        if (i != type && self.settings.at(kTracks[i]).integer > 0) {
            ++active;
            if (quietest < 0 || self.settings.at(kTracks[i]).integer < self.settings.at(kTracks[quietest]).integer) {
                quietest = i;
            }
        }
    }
    if (volume && active >= 4) {
        changes[1] = {sizeof(CdSettingValue), CD_SETTING_INTEGER, kTracks[quietest], 0};
        size = 2;
    }
    apply(&self, changes, size);
}

void noiseAction(Instance& self, const cybercanvas::SceneEvent& result) {
    if (result.value) {
        if (result.key == 10) {
            setNoiseValue(self, "master", int64_t(std::lround(*result.value)));
        } else if (result.key >= 20 && result.key < 26) {
            setTrack(self, int(result.key - 20), int64_t(std::lround(*result.value)));
        }
    }
    if (result.clicked) {
        if (result.key == 1 && self.audio) {
            self.ambient.command(self.context, self.audio,
                                 self.model.audioStatus == CD_AMBIENT_PLAYING ? CD_AMBIENT_PAUSE : CD_AMBIENT_PLAY);
            self.refreshAudio();
        } else if (result.key == 2) {
            setNoiseValue(self, "primary", (self.settings.at("primary").integer + 1) % 6);
        } else if (result.key == 3) {
            const auto minutes = self.settings.at("timer_minutes").integer;
            setNoiseValue(self, "timer_minutes", minutes == 15 ? 30 : minutes == 30 ? 60 : minutes == 60 ? 0 : 15);
        } else if (result.key == 4) {
            self.renderer.cancelInput();
            self.model.noisePage = !self.model.noisePage;
        }
    }
    retime(self);
    if (result.clicked || result.value) {
        self.redraw();
    }
}

void noiseInput(Instance& self, const CdInput& event) {
    auto& scene =
        self.renderer.noiseScene(self.spec, self.settings, self.model, self.safe.width, self.safe.height, self.dpr);
    noiseAction(self, scene.input(event, {self.safe.x, self.safe.y}));
    if (scene.needsPaint()) {
        self.redraw();
    }
}

void CD_CALL input(void* pointer, const CdInput* e) noexcept {
    try {
        if (!pointer || !e || e->struct_size < sizeof(*e) || !std::isfinite(e->x) || !std::isfinite(e->y)) {
            return;
        }
        auto& self = *static_cast<Instance*>(pointer);
        if (self.hidden) {
            return;
        }
        if (self.spec.kind == Kind::Noise) {
            noiseInput(self, *e);
            return;
        }
        if (self.renderer.plantInput(self.spec, self.settings, self.safe, *e)) {
            self.pressed = false;
            return;
        }
        const double x = e->x - self.safe.x, y = e->y - self.safe.y;
        const bool inside = x >= 0 && y >= 0 && x < self.safe.width && y < self.safe.height;
        if (e->kind == CD_INPUT_CANCEL || e->kind == CD_INPUT_LEAVE ||
            std::hypot(x - self.pressX, y - self.pressY) > 5) {
            self.pressed = false;
        }
        if (e->kind == CD_INPUT_PRESS && (e->buttons & CD_BUTTON_LEFT)) {
            self.pressed = inside;
            self.pressX = x;
            self.pressY = y;
        } else if (e->kind == CD_INPUT_RELEASE) {
            const bool click = self.pressed && inside && !(e->buttons & CD_BUTTON_LEFT);
            self.pressed = false;
            if (!click) {
                return;
            }
            if (self.spec.kind == Kind::Plant && y > self.safe.height * .53) {
                self.model.water(wallSeconds());
                retime(self);
                self.redraw();
            } else {
                self.model.click(x, y);
                retime(self);
                self.redraw();
            }
        }
    } catch (...) {
    }
}

void CD_CALL pause(void* pointer, uint32_t hidden) noexcept {
    try {
        if (!pointer) {
            return;
        }
        auto& self = *static_cast<Instance*>(pointer);
        if (self.hidden == (hidden != 0)) {
            return;
        }
        self.hidden = hidden != 0;
        self.updates.pause(self.hidden);
        self.renderer.pauseImages(self.hidden);
        self.pressed = false;
        self.renderer.cancelInput();

        self.stopTimer();
        self.clock.setPaused(self.hidden);
        if (!self.hidden) {
            self.refreshAudio();
            retime(self);
            self.redraw();
        }
    } catch (...) {
    }
}

CdResult CD_CALL get(void* pointer, const char* id, CdSettingValue* out, uint32_t size) noexcept {
    return pointer ? static_cast<Instance*>(pointer)->settings.get(id, out, size) : CD_ERROR_ARGUMENT;
}

CdResult CD_CALL save(void* pointer, uint8_t* bytes, uint64_t capacity, uint64_t* size) noexcept {
    if (!pointer || !size) {
        return CD_ERROR_ARGUMENT;
    }
    const auto& self = *static_cast<Instance*>(pointer);
    *size = self.spec.kind == Kind::Plant ? 32 : 0;
    if (!*size || !bytes && !capacity) {
        return CD_OK;
    }
    if (!bytes || capacity < 32) {
        return CD_ERROR_BUFFER;
    }
    std::memset(bytes, 0, 32);
    std::memcpy(bytes, "POT1", 4);
    std::memcpy(bytes + 4, &self.model.plantedAt, 8);
    std::memcpy(bytes + 12, &self.model.wateredAt, 8);
    std::memcpy(bytes + 20, &self.model.bonus, 8);
    return CD_OK;
}

CdResult CD_CALL restore(void* pointer, const uint8_t* bytes, uint64_t size) noexcept {
    if (!pointer || size && !bytes) {
        return CD_ERROR_ARGUMENT;
    }
    if (!size) {
        return CD_OK;
    }
    auto& self = *static_cast<Instance*>(pointer);
    if (self.spec.kind != Kind::Plant || size != 32 || std::memcmp(bytes, "POT1", 4)) {
        return CD_ERROR_ARGUMENT;
    }
    Model next = self.model;
    std::memcpy(&next.plantedAt, bytes + 4, 8);
    std::memcpy(&next.wateredAt, bytes + 12, 8);
    std::memcpy(&next.bonus, bytes + 20, 8);
    if (!std::isfinite(next.bonus) || next.bonus < 0 || next.bonus > .35 || next.plantedAt > 4102444800ULL ||
        next.wateredAt > 4102444800ULL) {
        return CD_ERROR_ARGUMENT;
    }
    next.interaction.seek(10);
    next.waterFeedback = false;
    self.model = next;
    retime(self);
    self.redraw();
    return CD_OK;
}
}  // namespace

CdResult create(Spec spec, const CdCreateContext* c, CdInstanceApi* out, uint32_t size) {
    try {
        if (!c || c->struct_size < sizeof(*c) || c->abi_version != CD_PLUGIN_ABI_VERSION || !c->host ||
            c->host->struct_size < sizeof(CdHostApi) || c->host->abi_version != CD_PLUGIN_ABI_VERSION ||
            !c->host->subscribe_timer || !c->host->unsubscribe || !c->host->request_redraw || !out ||
            size < sizeof(*out)) {
            return CD_ERROR_ARGUMENT;
        }
        auto self = std::make_unique<Instance>(
            spec, std::filesystem::u8path(c->plugin_directory ? c->plugin_directory : "").wstring());
        self->host = *c->host;
        self->renderer.bindHost(*c);
        if (spec.kind == Kind::Plant) {
            self->renderer.bindPlant(*c, [ptr = self.get()](uint32_t) {
                if (!ptr->hidden) {
                    ptr->model.water(wallSeconds());
                    retime(*ptr);
                    ptr->redraw();
                }
            });
        }

        self->context = c->host_context;
        self->updates.bind(*c);
        self->renderer.onInvalidate([pointer = self.get()] {
            pointer->redraw();
        });
        self->clock.resetAnchor();
        if (spec.kind != Kind::Plant && c->host->struct_size >= sizeof(CdServiceHostApi)) {
            const auto* host = reinterpret_cast<const CdServiceHostApi*>(c->host);
            if (host->query_service) {
                const auto result =
                    host->query_service(c->host_context, CD_SERVICE_AMBIENT, 1, &self->ambient, sizeof(self->ambient));
                if (result != CD_OK || self->ambient.struct_size < sizeof(self->ambient) || !self->ambient.open ||
                    !self->ambient.close || !self->ambient.configure || !self->ambient.command ||
                    !self->ambient.snapshot) {
                    self->ambient = {};
                }
            }
        }
        if (spec.kind == Kind::Noise && self->ambient.open) {
            if (self->ambient.open(self->context, "", &self->audio) != CD_OK) {
                return CD_ERROR_INTERNAL;
            }
            const auto config = configuration(self->settings);
            self->ambient.configure(self->context, self->audio, &config);
            self->refreshAudio();
        }
        const auto result = subscribe(*self, self->settings, self->timer);
        if (result != CD_OK) {
            return result;
        }
        *out = {
            sizeof(*out), CD_PLUGIN_ABI_VERSION, self.get(), destroy, render, input, layout, pause, save, restore, get,
            apply};
        self.release();
        return CD_OK;
    } catch (...) {
        return CD_ERROR_INTERNAL;
    }
}
}  // namespace atmosphere
