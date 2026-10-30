#include <Windows.h>
#include <sdk/RenderOutput.h>
#include <sdk/Update.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <new>

#include "plugins/koi-suite/Pond.h"

namespace koiplugin {
cdsdk::Settings defaults(Spec spec) {
    return {{{"lighting", CD_SETTING_CHOICE, 0, 0, "theme", 0, 0, {"theme", "day", "night"}},
             {"fish_count", CD_SETTING_INTEGER, spec.fishCount, 0, "", 1, 12},
             {"mouse_behavior", CD_SETTING_CHOICE, 0, 0, "flee", 0, 0, {"flee", "follow"}},
             {"show_background", CD_SETTING_TOGGLE, 1},
             {"blessings", CD_SETTING_TOGGLE, 1},
             {"swim_speed", CD_SETTING_NUMBER, 0, 1, "", .3, 2},
             {"motion_speed", CD_SETTING_NUMBER, 0, kDefaultMotionSpeed, "", .25, 4},
             {"shimmer", CD_SETTING_INTEGER, 65, 0, "", 0, 100},
             {"color", CD_SETTING_COLOR, 0xDE593CFF},
             {"leaves", CD_SETTING_TOGGLE, 1},
             {"flowers", CD_SETTING_TOGGLE, 1},
             {"animation", CD_SETTING_TOGGLE, 1},
             {"frame_rate", CD_SETTING_CHOICE, 0, 0, "20", 0, 0, {"15", "20", "30"}},
             {"seed", CD_SETTING_INTEGER, 7, 0, "", 1, 9999},
             {"background_corner_radius", CD_SETTING_INTEGER, 5, 0, "", 0, 32},
             {"language", CD_SETTING_CHOICE, 0, 0, "zh-CN", 0, 0, {"zh-CN", "en"}}}};
}

namespace {
struct Instance {
    explicit Instance(Spec value) : spec(value), settings(defaults(value)), startupSeed(std::random_device{}()) {}

    Spec spec;
    cdsdk::Settings settings;
    Scene scene;
    Renderer renderer;
    CdHostApi host{};
    void* context = nullptr;
    cdsdk::Timer timer;
    uint64_t lastTick = 0, lastPondFrame = 0;
    CdRect safe{};
    bool paused = false, themeNight = false, lightingKnown = false, pressed = false;
    Vec pressPosition{};
    double night = 0;
    double motionTime = 0;
    bool pondDirty = true;
    uint32_t startupSeed;

    double targetNight() const {
        const auto& mode = settings.at("lighting").text;
        return mode == "night" || (mode == "theme" && themeNight) ? 1 : 0;
    }

    void configure() {
        scene.configure(safe.width, safe.height, int(settings.at("fish_count").integer),
                        startupSeed ^ (uint32_t(settings.at("seed").integer) * 0x9E3779B9u),
                        fishLengthForGrid(safe.width, safe.height, spec.rows, spec.columns));
        scene.setMouseBehavior(settings.at("mouse_behavior").text == "follow" ? MouseBehavior::Follow
                                                                              : MouseBehavior::Flee);
    }

    void redraw(bool pond = true) {
        pondDirty |= pond;
        host.request_redraw(context);
    }

    void stop() {
        timer.stop();
    }

    ~Instance() {
        stop();
    }
};

void CD_CALL tick(void* pointer, uint64_t now) noexcept {
    try {
        auto& self = *static_cast<Instance*>(pointer);
        if (self.paused || !self.settings.at("animation").integer) {
            return;
        }
        const double seconds = now >= self.lastTick ? std::min(.15, (now - self.lastTick) / 1000.0) : 0;
        self.lastTick = now;
        const int previousWord = self.scene.blessing().word;
        self.scene.advance(seconds, self.settings.at("swim_speed").number);
        self.motionTime += seconds * self.settings.at("motion_speed").number;
        const double desired = self.targetNight();
        self.night += std::clamp(desired - self.night, -seconds * .8, seconds * .8);
        const double pondInterval = 1000.0 / std::stoi(self.settings.at("frame_rate").text);
        const bool pondDue =
            !self.lastPondFrame || now < self.lastPondFrame || now - self.lastPondFrame >= pondInterval;
        const auto& word = self.scene.blessing();
        const bool textMotion = self.settings.at("blessings").integer && word.word >= 0 &&
                                (self.scene.time() < 1.2 || word.age < 1.2 || word.age > kBlessingDuration - 1.3);
        if (seconds > 0 && (pondDue || textMotion || word.word != previousWord)) {
            self.redraw(pondDue);
        }
    } catch (...) {
    }
}

CdResult subscribe(Instance& self, const cdsdk::Settings& settings, cdsdk::Timer& timer) {
    timer.bind(self.host, self.context);
    if (self.paused || !settings.at("animation").integer) {
        return CD_OK;
    }
    const uint32_t fps =
        std::max(uint32_t(std::stoi(settings.at("frame_rate").text)), settings.at("blessings").integer ? 60u : 0u);
    return timer.replace((1000 + fps - 1) / fps, tick, &self);
}

void CD_CALL destroy(void* pointer) noexcept {
    const auto instance = std::unique_ptr<Instance>(static_cast<Instance*>(pointer));
}

CdResult CD_CALL render(void* pointer, const CdFrame* frame) noexcept {
    try {
        if (!pointer || !frame || frame->struct_size < sizeof(CdFrame) || !frame->rgba || !frame->width ||
            !frame->height || frame->width > 4096 || frame->height > 4096 || frame->stride < frame->width * 4 ||
            frame->capacity < uint64_t(frame->stride) * frame->height || !std::isfinite(frame->device_pixel_ratio) ||
            frame->device_pixel_ratio < .5 || frame->device_pixel_ratio > 4) {
            return CD_ERROR_ARGUMENT;
        }
        auto& self = *static_cast<Instance*>(pointer);
        if (self.safe.width <= 0 || self.safe.height <= 0) {
            self.safe = {0, 0, frame->width / frame->device_pixel_ratio, frame->height / frame->device_pixel_ratio};
        }
        self.configure();
        const Appearance appearance{self.night,
                                    self.settings.at("shimmer").integer / 100.0,
                                    float(self.settings.at("background_corner_radius").integer),
                                    self.settings.at("leaves").integer != 0,
                                    self.settings.at("flowers").integer != 0,
                                    uint32_t(self.settings.at("color").integer),
                                    self.settings.at("show_background").integer != 0,
                                    self.settings.at("blessings").integer != 0,
                                    self.motionTime,
                                    self.pondDirty};
        if (!self.renderer.render(self.scene, appearance, *frame, self.safe)) {
            return CD_ERROR_INTERNAL;
        }
        if (self.pondDirty) {
            self.lastPondFrame = self.lastTick;
            self.pondDirty = false;
        }
        return CD_OK;
    } catch (...) {
        return CD_ERROR_INTERNAL;
    }
}

void CD_CALL layout(void* pointer, const CdLayout* value) noexcept {
    try {
        if (!pointer || !value || value->struct_size < sizeof(CdLayout) || !std::isfinite(value->logical_width) ||
            !std::isfinite(value->logical_height) || value->logical_width <= 0 || value->logical_height <= 0) {
            return;
        }
        auto& self = *static_cast<Instance*>(pointer);
        auto safe = value->safe_content;
        if (!std::isfinite(safe.x) || !std::isfinite(safe.y) || !std::isfinite(safe.width) ||
            !std::isfinite(safe.height) || safe.width <= 0 || safe.height <= 0) {
            safe = {0, 0, value->logical_width, value->logical_height};
        }
        safe.x = std::clamp(safe.x, 0.0, value->logical_width);
        safe.y = std::clamp(safe.y, 0.0, value->logical_height);
        safe.width = std::clamp(safe.width, 0.0, value->logical_width - safe.x);
        safe.height = std::clamp(safe.height, 0.0, value->logical_height - safe.y);
        self.safe = safe;
        // C ABI 1 exposes theme text/accent colors, not a material name. Bright text
        // means dark surface. Host sends a fresh layout when the theme color changes.
        const double brightness =
            value->text_color.r * .2126 + value->text_color.g * .7152 + value->text_color.b * .0722;
        self.themeNight = brightness > 140;
        if (!self.lightingKnown || !self.settings.at("animation").integer || self.paused) {
            self.night = self.targetNight();
            self.lightingKnown = true;
        }
        self.pressed = false;
        self.scene.clearPointer();
        self.configure();
        self.redraw();
    } catch (...) {
    }
}

void CD_CALL input(void* pointer, const CdInput* value) noexcept {
    try {
        if (!pointer || !value || value->struct_size < sizeof(CdInput) || !std::isfinite(value->x) ||
            !std::isfinite(value->y)) {
            return;
        }
        auto& self = *static_cast<Instance*>(pointer);
        if (self.paused || !self.settings.at("animation").integer) {
            self.pressed = false;
            return;
        }
        const Vec position{value->x - self.safe.x, value->y - self.safe.y};
        if (value->kind == CD_INPUT_LEAVE || (position - self.pressPosition).length() > 5) {
            self.pressed = false;
        }
        if (value->kind == CD_INPUT_LEAVE) {
            self.scene.clearPointer();
            return;
        }
        if (value->kind == CD_INPUT_MOVE && !value->buttons && !self.pressed) {
            self.scene.pointer(position);
        }
        if (value->kind == CD_INPUT_PRESS && (value->buttons & CD_BUTTON_LEFT)) {
            self.pressed = true;
            self.pressPosition = position;
            self.scene.clearPointer();
        } else if (value->kind == CD_INPUT_RELEASE) {
            // Qt reports the remaining held buttons on release; left release usually
            // arrives with buttons == 0. Only an actual release completes the click.
            const bool click = self.pressed && !(value->buttons & CD_BUTTON_LEFT);
            self.pressed = false;
            if (click) {
                self.scene.click(position);
                self.redraw();
            }
        }
    } catch (...) {
    }
}

void CD_CALL pause(void* pointer, uint32_t paused) noexcept {
    try {
        if (!pointer) {
            return;
        }
        auto& self = *static_cast<Instance*>(pointer);
        if (self.paused == (paused != 0)) {
            return;
        }
        self.paused = paused != 0;
        self.pressed = false;
        self.scene.clearPointer();
        self.stop();
        self.lastTick = GetTickCount64();
        if (!self.paused) {
            subscribe(self, self.settings, self.timer);
        }
    } catch (...) {
    }
}

CdResult CD_CALL save(void* pointer, uint8_t*, uint64_t, uint64_t* size) noexcept {
    if (!pointer || !size) {
        return CD_ERROR_ARGUMENT;
    }
    // Settings are persisted by the host. The pond has no business state or disk
    // writes; fish positions are intentionally ephemeral, like water animation.
    *size = 0;
    return CD_OK;
}

CdResult CD_CALL restore(void* pointer, const uint8_t*, uint64_t size) noexcept {
    return pointer && !size ? CD_OK : CD_ERROR_ARGUMENT;
}

CdResult CD_CALL get(void* pointer, const char* id, CdSettingValue* output, uint32_t size) noexcept {
    return pointer ? static_cast<Instance*>(pointer)->settings.get(id, output, size) : CD_ERROR_ARGUMENT;
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

        const bool resubscribe = next.at("animation").integer != self.settings.at("animation").integer ||
                                 next.at("frame_rate").text != self.settings.at("frame_rate").text ||
                                 next.at("blessings").integer != self.settings.at("blessings").integer;
        cdsdk::Timer replacement;
        if (resubscribe) {
            const auto subscribed = subscribe(self, next, replacement);
            if (subscribed != CD_OK) {
                return subscribed;
            }
            self.stop();
            self.timer = std::move(replacement);
            self.lastTick = GetTickCount64();
        }
        self.settings = std::move(next);
        if (!self.settings.at("animation").integer || self.paused) {
            self.night = self.targetNight();
        }
        self.configure();
        self.redraw();
        return CD_OK;
    } catch (...) {
        return CD_ERROR_INTERNAL;
    }
}
}  // namespace

CdResult create(Spec spec, const CdCreateContext* context, CdInstanceApi* output, uint32_t size) {
    try {
        if (!context || context->struct_size < sizeof(CdCreateContext) ||
            context->abi_version != CD_PLUGIN_ABI_VERSION || !context->host ||
            context->host->struct_size < sizeof(CdHostApi) || context->host->abi_version != CD_PLUGIN_ABI_VERSION ||
            !context->host->request_redraw || !context->host->subscribe_timer || !context->host->unsubscribe ||
            !output || size < sizeof(CdInstanceApi)) {
            return CD_ERROR_ARGUMENT;
        }
        auto self = std::make_unique<Instance>(spec);
        self->host = *context->host;
        cdsdk::bindOutputIfSupported(self->renderer, *context);
        self->context = context->host_context;
        self->lastTick = GetTickCount64();
        const auto subscribed = subscribe(*self, self->settings, self->timer);
        if (subscribed != CD_OK) {
            return subscribed;
        }
        *output = {sizeof(CdInstanceApi),
                   CD_PLUGIN_ABI_VERSION,
                   self.get(),
                   destroy,
                   render,
                   input,
                   layout,
                   pause,
                   save,
                   restore,
                   get,
                   apply};
        self.release();
        return CD_OK;
    } catch (...) {
        return CD_ERROR_INTERNAL;
    }
}
}  // namespace koiplugin
