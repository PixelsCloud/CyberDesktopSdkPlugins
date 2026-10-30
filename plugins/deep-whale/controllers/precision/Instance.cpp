// Controller binding reused from plugins/vampire-alice/controllers/precision/Instance.cpp.
#include <CyberCanvas/PanelLayout.h>

#include <algorithm>
#include <cmath>

#include "plugins/deep-whale/controllers/precision/Clock.h"

namespace deep_precision {
Instance::Instance(const CdCreateContext& context)
    : settings({{"motion", CD_SETTING_CHOICE, 0, 0, "beat", 0, 0, {"sweep", "beat", "tick"}},
                {"seconds", CD_SETTING_TOGGLE, 1},
                {"background", CD_SETTING_TOGGLE, 1},
                {"detail", CD_SETTING_TOGGLE, 1},
                {"animation", CD_SETTING_TOGGLE, 1},
                {"reduced_motion", CD_SETTING_TOGGLE, 0},
                {"hand_color", CD_SETTING_COLOR, 0xc3d6dcff},
                {"seconds_color", CD_SETTING_COLOR, 0xe5b29dff}}),
      kind_(identify(context.plugin_id ? context.plugin_id : "")),
      model_(kind_),
      renderer_(kind_, context.plugin_directory && *context.plugin_directory
                           ? std::filesystem::path(
                                 std::u8string_view(reinterpret_cast<const char8_t*>(context.plugin_directory))) /
                                 "assets"
                           : std::filesystem::path{}) {
    canvas_.bindGpu(context);
    environment_.bind(context);
    timer_.bind(context);
    redraw_.bind(context);
    model_.synchronize(environment_.snapshot(), options(settings));
    if (schedule(settings) != CD_OK) {
        throw std::runtime_error("Cannot schedule precision clock");
    }
}

Instance::~Instance() {
    timer_.stop();
}

CdResult Instance::schedule(const cdsdk::Settings& next) {
    if (hidden_) {
        timer_.stop();
        return CD_OK;
    }
    const auto environment = environment_.snapshot();
    return timer_.replace(cadence(kind_, options(next), environment.unix_ms, renderer_.pending()), wake, this);
}

void CD_CALL Instance::wake(void* value, uint64_t) noexcept {
    auto& self = *static_cast<Instance*>(value);
    try {
        bool changed = self.renderer_.poll();
        changed |= self.model_.synchronize(self.environment_.snapshot(), options(self.settings));
        if (changed) {
            self.redraw_.invalidate();
        }
        if (self.schedule(self.settings) != CD_OK) {
            self.timer_.stop();
        }
    } catch (...) {
        self.timer_.stop();
    }
}

void Instance::arrange() {
    const auto view = renderer_.view(safe_, {1000, 1000});
    origin_ = view.origin;
    scale_ = view.scale;
}

CdResult Instance::render(const CdFrame& frame) {
    if (!canvas_.valid(frame)) {
        return CD_ERROR_ARGUMENT;
    }
    redraw_.beginFrame();
    canvas_.render(frame, safe_, [&](auto& canvas, CdRect safe) {
        safe_ = safe;
        arrange();
        renderer_.appearance(settings);
        const auto value = options(settings);
        canvas.saved([&] {
            canvas.translate(origin_.x - safe.x, origin_.y - safe.y);
            canvas.scale(scale_, scale_);
            renderer_.draw(canvas, model_.state(), value, scale_);
        });
    });
    return schedule(settings);
}

CdResult Instance::applySettings(const CdSettingValue* values, uint32_t count) {
    auto next = settings;
    const auto result = next.apply(values, count);
    if (result != CD_OK) {
        return result;
    }
    const auto scheduled = schedule(next);
    if (scheduled != CD_OK) {
        return scheduled;
    }
    settings = std::move(next);
    model_.synchronize(environment_.snapshot(), options(settings));
    redraw_.invalidate();
    return CD_OK;
}

void Instance::layoutChanged(const CdLayout& layout) {
    if (!std::isfinite(layout.logical_width) || !std::isfinite(layout.logical_height) || layout.logical_width <= 0 ||
        layout.logical_height <= 0) {
        return;
    }
    const auto area = layout.safe_content;
    safe_ = std::isfinite(area.x) && std::isfinite(area.y) && std::isfinite(area.width) && std::isfinite(area.height) &&
                    area.width > 0 && area.height > 0
                ? area
                : CdRect{0, 0, layout.logical_width, layout.logical_height};
    arrange();
    redraw_.invalidate();
}

void Instance::setPaused(bool paused) {
    hidden_ = paused;
    renderer_.pause(paused);
    redraw_.pause(paused);
    if (paused) {
        timer_.stop();
    } else {
        model_.synchronize(environment_.snapshot(), options(settings));
        schedule(settings);
        redraw_.invalidate();
    }
}
}  // namespace deep_precision
