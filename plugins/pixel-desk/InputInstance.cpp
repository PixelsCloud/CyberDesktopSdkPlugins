#include <Windows.h>

#include <optional>

#include "plugins/pixel-desk/PixelInput.h"

namespace pixeldesk {
InputInstance::InputInstance(const CdCreateContext& context)
    : settings(widgetplugin::defaults(widgetplugin::Kind::InputStats, true)),
      services_(context.host, context.host_context),
      renderer_(context) {
    environment_.bind(context);
    timer_.bind(context);
    redraw_.bind(context);
    renderer_.onChange([this] {
        invalidate();
    });
    if (subscribe() != CD_OK) {
        throw std::runtime_error("Cannot subscribe to aggregate input statistics");
    }
}

InputInstance::~InputInstance() {
    watch_.reset();
    timer_.stop();
    renderer_.onChange({});
    renderer_.pause(true);
}

CdResult InputInstance::subscribe() {
    struct Gate {
        bool committed = false;
        std::optional<CdInputStats> pending;
    };

    const auto gate = std::make_shared<Gate>();
    auto next = services_.input([this, gate](const CdInputStats& value) {
        if (gate->committed) {
            receive(value);
        } else {
            gate->pending = value;
        }
    });
    if (next.result() != CD_OK) {
        return next.result();
    }
    watch_ = std::move(next);
    gate->committed = true;
    if (gate->pending) {
        receive(*gate->pending);
    }
    return CD_OK;
}

bool InputInstance::motionAllowed() const {
    BOOL enabled = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0);
    return !hidden_ && !settings.at("reduced_motion").integer && enabled;
}

void InputInstance::receive(const CdInputStats& value) {
    if (hidden_) {
        return;
    }
    const uint64_t now = environment_.snapshot().monotonic_ms;
    const bool changed = drums_.retarget(value, now, motionAllowed());
    stats_ = value;
    if (drums_.active(now)) {
        if (timer_.replace(16, tick, this) != CD_OK) {
            drums_.finish();
            timer_.stop();
        }
    } else {
        timer_.stop();
    }
    if (changed) {
        invalidate();
    }
}

void CD_CALL InputInstance::tick(void* value, uint64_t) noexcept {
    auto& self = *static_cast<InputInstance*>(value);
    try {
        if (self.hidden_) {
            self.timer_.stop();
            return;
        }
        if (!self.motionAllowed() || !self.drums_.active(self.environment_.snapshot().monotonic_ms)) {
            self.drums_.finish();
            self.timer_.stop();
        }
        self.invalidate();
    } catch (...) {
        self.drums_.finish();
        self.timer_.stop();
    }
}

CdResult InputInstance::render(const CdFrame& frame) {
    redraw_.beginFrame();
    return renderer_.render(stats_, positions(), frame, safe_) ? CD_OK : CD_ERROR_ARGUMENT;
}

CdResult InputInstance::applySettings(const CdSettingValue* values, uint32_t count) {
    auto next = settings;
    const auto result = next.apply(values, count);
    if (result != CD_OK) {
        return result;
    }
    settings = std::move(next);
    if (!motionAllowed()) {
        drums_.finish();
        timer_.stop();
    }
    invalidate();
    return CD_OK;
}

void InputInstance::layoutChanged(const CdLayout& layout) {
    if (std::isfinite(layout.logical_width) && std::isfinite(layout.logical_height) && layout.logical_width > 0 &&
        layout.logical_height > 0) {
        safe_ = layout.safe_content;
    }
}

void InputInstance::setPaused(bool paused) {
    if (paused == hidden_) {
        return;
    }
    hidden_ = paused;
    renderer_.pause(paused);
    redraw_.pause(paused);
    if (paused) {
        watch_.reset();
        timer_.stop();
        drums_.reset();
    } else {
        subscribe();
        invalidate();
    }
}

void InputInstance::input(const CdInput& event) {
    if (!hidden_ && !watch_.token() && event.kind == CD_INPUT_DOUBLE_CLICK && (event.buttons & CD_BUTTON_LEFT) &&
        std::isfinite(event.x) && std::isfinite(event.y) && event.x >= safe_.x && event.y >= safe_.y &&
        event.x < safe_.x + safe_.width && event.y < safe_.y + safe_.height) {
        subscribe();
    }
}

void InputInstance::invalidate() {
    redraw_.invalidate();
}

const CdInputStats& InputInstance::stats() const {
    return stats_;
}

widgetplugin::CounterDrums::Positions InputInstance::positions() const {
    return drums_.sample(environment_.snapshot().monotonic_ms);
}
}  // namespace pixeldesk
