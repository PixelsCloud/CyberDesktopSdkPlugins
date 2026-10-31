#include <Windows.h>
#include <sdk/RenderOutput.h>

#include <algorithm>
#include <cmath>
#include <memory>

#include "plugins/hourglass-suite/Hourglass.h"
#include "plugins/hourglass-suite/PhysicalSand.h"
#include "sdk/Environment.h"
#include "sdk/Update.h"
#include "sdk/cpp/PluginDiagnostics.h"

namespace hourglassplugin {
cdsdk::Settings defaults(Spec spec) {
    if (!spec.showTimer) {
        return {{{"duration", CD_SETTING_INTEGER, 30, 0, "", 5, 3600},
                 {"physics", CD_SETTING_TOGGLE, 1},
                 {"repeat", CD_SETTING_TOGGLE, 1},
                 {"running", CD_SETTING_TOGGLE, 1},
                 {"lighting", CD_SETTING_CHOICE, 0, 0, "theme", 0, 0, {"theme", "day", "night"}},
                 {"color", CD_SETTING_CHOICE, 0, 0, "amber", 0, 0, {"amber", "mint", "cyan"}},
                 {"show_background", CD_SETTING_TOGGLE, 1},
                 {"glow", CD_SETTING_INTEGER, 35, 0, "", 0, 100},
                 {"frame_rate", CD_SETTING_CHOICE, 0, 0, "20", 0, 0, {"15", "20", "30"}},
                 {"background_corner_radius", CD_SETTING_INTEGER, 5, 0, "", 0, 32}}};
    }
    return {{{"duration", CD_SETTING_INTEGER, 30, 0, "", 5, 3600},
             {"physics", CD_SETTING_TOGGLE, 1},
             {"repeat", CD_SETTING_TOGGLE, 1},
             {"running", CD_SETTING_TOGGLE, 1},
             {"lighting", CD_SETTING_CHOICE, 0, 0, "theme", 0, 0, {"theme", "day", "night"}},
             {"color", CD_SETTING_CHOICE, 0, 0, "amber", 0, 0, {"amber", "mint", "cyan"}},
             {"show_background", CD_SETTING_TOGGLE, 1},
             {"show_timer", CD_SETTING_TOGGLE, 1},
             {"glow", CD_SETTING_INTEGER, 35, 0, "", 0, 100},
             {"frame_rate", CD_SETTING_CHOICE, 0, 0, "20", 0, 0, {"15", "20", "30"}},
             {"background_corner_radius", CD_SETTING_INTEGER, 5, 0, "", 0, 32}}};
}

namespace {
struct Instance {
    explicit Instance(Spec value) : spec(value), settings(defaults(value)) {}

    Spec spec;
    cdsdk::Settings settings;
    Model model;
    PhysicalSand sand;
    Renderer renderer;
    CdHostApi host{};
    void* context = nullptr;
    CdRect safe{};
    cdsdk::Timer timer;
    cdsdk::Environment environment;
    uint64_t lastTick = 0;
    bool hidden = false, dark = true, pressed = false;
    double pressX = 0, pressY = 0;
    cdsdk::plugin_detail::PluginDiagnostics diagnostics;

    void redraw() {
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
        if (self.hidden || !self.settings.at("running").integer) {
            return;
        }
        const double seconds = now >= self.lastTick ? std::min(.25, (now - self.lastTick) / 1000.0) : 0;
        self.lastTick = now;
        self.model.advance(seconds, self.settings.at("repeat").integer != 0);
        if (self.settings.at("physics").integer) {
            self.sand.advance(seconds, self.model);
        }
        if (self.model.phase() == Phase::Finished) {
            self.stop();
        }
        if (seconds > 0) {
            self.redraw();
        }
    } catch (...) {
    }
}

CdResult subscribe(Instance& self, const cdsdk::Settings& settings, bool restarting, cdsdk::Timer& token) {
    token.bind(self.host, self.context);
    if (self.hidden || !settings.at("running").integer || (!restarting && self.model.phase() == Phase::Finished)) {
        return CD_OK;
    }
    const uint32_t fps = uint32_t(std::stoi(settings.at("frame_rate").text));
    return token.replace((1000 + fps - 1) / fps, tick, &self);
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
        const auto& lighting = self.settings.at("lighting").text;
        const Options options{lighting == "night" || lighting == "theme" && self.dark,
                              self.settings.at("running").integer != 0,
                              self.settings.at("show_background").integer != 0,
                              self.spec.showTimer && self.settings.at("show_timer").integer != 0,
                              float(self.settings.at("background_corner_radius").integer),
                              self.settings.at("glow").integer / 100.0,
                              self.settings.at("color").text,
                              self.settings.at("physics").integer ? &self.sand : nullptr};
        return self.renderer.render(self.model, options, *frame, self.safe) ? CD_OK : CD_ERROR_INTERNAL;
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
        self.dark = value->text_color.r * .2126 + value->text_color.g * .7152 + value->text_color.b * .0722 > 140;
        self.pressed = false;
        self.redraw();
    } catch (...) {
    }
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
        const bool restarting =
            next.at("duration").integer != self.settings.at("duration").integer ||
            next.at("physics").integer != self.settings.at("physics").integer ||
            (next.at("repeat").integer && !self.settings.at("repeat").integer && self.model.phase() == Phase::Finished);
        const bool finishing = !restarting && !next.at("repeat").integer && self.model.phase() == Phase::Turning;
        const bool resubscribe = restarting || finishing ||
                                 next.at("running").integer != self.settings.at("running").integer ||
                                 next.at("frame_rate").text != self.settings.at("frame_rate").text;
        cdsdk::Timer replacement;
        if (resubscribe) {
            const auto subscribed = finishing ? CD_OK : subscribe(self, next, restarting, replacement);
            if (subscribed != CD_OK) {
                return subscribed;
            }
            self.stop();
            self.timer = std::move(replacement);
            self.lastTick = self.environment.snapshot().monotonic_ms;
        }
        self.settings = std::move(next);
        if (restarting) {
            self.model.reset(double(self.settings.at("duration").integer));
            self.sand.reset();
        } else if (finishing) {
            self.model.advance(0, false);
        }
        self.pressed = false;
        self.redraw();
        return CD_OK;
    } catch (...) {
        return CD_ERROR_INTERNAL;
    }
}

void restart(Instance& self) {
    auto next = self.settings;
    const CdSettingValue running{sizeof(running), CD_SETTING_TOGGLE, "running", 1};
    if (next.apply(&running, 1) != CD_OK) {
        return;
    }
    cdsdk::Timer replacement;
    if (subscribe(self, next, true, replacement) != CD_OK) {
        return;
    }
    self.stop();
    self.timer = std::move(replacement);
    self.lastTick = self.environment.snapshot().monotonic_ms;
    self.settings = std::move(next);
    self.model.reset(double(self.settings.at("duration").integer));
    self.sand.reset();
    self.redraw();
}

void CD_CALL input(void* pointer, const CdInput* value) noexcept {
    try {
        if (!pointer || !value || value->struct_size < sizeof(CdInput) || !std::isfinite(value->x) ||
            !std::isfinite(value->y)) {
            return;
        }
        auto& self = *static_cast<Instance*>(pointer);
        if (self.hidden) {
            self.pressed = false;
            return;
        }
        const double x = value->x - self.safe.x, y = value->y - self.safe.y;
        if (value->kind == CD_INPUT_LEAVE || std::hypot(x - self.pressX, y - self.pressY) > 5 || x < 0 || y < 0 ||
            x >= self.safe.width || y >= self.safe.height) {
            self.pressed = false;
        }
        if (value->kind == CD_INPUT_PRESS && value->buttons & CD_BUTTON_LEFT && x >= 0 && y >= 0 &&
            x < self.safe.width && y < self.safe.height) {
            self.pressed = true;
            self.pressX = x;
            self.pressY = y;
        } else if (value->kind == CD_INPUT_RELEASE) {
            const bool click = self.pressed && !(value->buttons & CD_BUTTON_LEFT);
            self.pressed = false;
            if (click) {
                const bool reset = self.spec.showTimer ? x < self.safe.width * .38 : y < self.safe.height * .5;
                if (reset || self.model.phase() == Phase::Finished) {
                    restart(self);
                } else {
                    const CdSettingValue running{sizeof(running), CD_SETTING_TOGGLE, "running",
                                                 self.settings.at("running").integer ? 0 : 1};
                    apply(&self, &running, 1);
                }
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
        self.pressed = false;
        self.stop();
        self.lastTick = self.environment.snapshot().monotonic_ms;
        if (!self.hidden) {
            subscribe(self, self.settings, false, self.timer);
        }
    } catch (...) {
    }
}

CdResult CD_CALL save(void* pointer, uint8_t*, uint64_t, uint64_t* size) noexcept {
    if (!pointer || !size) {
        return CD_ERROR_ARGUMENT;
    }
    *size = 0;
    return CD_OK;
}

CdResult CD_CALL restore(void* pointer, const uint8_t*, uint64_t size) noexcept {
    return pointer && !size ? CD_OK : CD_ERROR_ARGUMENT;
}

CdResult CD_CALL get(void* pointer, const char* id, CdSettingValue* output, uint32_t size) noexcept {
    return pointer ? static_cast<Instance*>(pointer)->settings.get(id, output, size) : CD_ERROR_ARGUMENT;
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
        self->environment.bind(*context);
        self->diagnostics.bind(*context, self.get(), [](void* pointer) {
            const auto value = static_cast<Instance*>(pointer)->sand.stats();
            return cdsdk::plugin_detail::PhysicsCounters{value.steps, value.advanceNanoseconds, value.awakeBodies,
                                                         value.maximumAdvanceNanoseconds};
        });
        self->lastTick = self->environment.snapshot().monotonic_ms;
        const auto subscribed = subscribe(*self, self->settings, false, self->timer);
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
}  // namespace hourglassplugin
