#include <cmath>
#include <stdexcept>

#include "plugins/deep-whale/DeepWhale.h"

namespace deepwhale {
ClockInstance::ClockInstance(const CdCreateContext& context)
    : settings(deepwhale::defaults(specification(context.plugin_id).kind)),
      renderer_(context, specification(context.plugin_id)) {
    environment_.bind(context);
    timer_.bind(context);
    redraw_.bind(context);
    renderer_.onChange([this] {
        invalidate();
    });
    synchronize();
    if (schedule(settings) != CD_OK) {
        throw std::runtime_error("Cannot schedule Deep Whale clock");
    }
}

ClockInstance::~ClockInstance() {
    timer_.stop();
    renderer_.onChange({});
}

CdResult ClockInstance::schedule(const cdsdk::Settings& next) {
    if (paused_) {
        timer_.stop();
        return CD_OK;
    }
    const uint64_t period = next.at("seconds").integer || next.at("blink_colon").integer ? 1000 : 60000;
    const auto now = environment_.snapshot();
    return timer_.replace(std::max(16u, uint32_t(period - now.unix_ms % period)), wake, this);
}

bool ClockInstance::synchronize() {
    const auto now = environment_.snapshot();
    const auto text = mechanicalpixels::clockText(now, settings);
    const bool colon = !settings.at("blink_colon").integer || (now.unix_ms / 1000) % 2 == 0;
    const bool changed = text != text_ || colon != colon_;
    text_ = text;
    colon_ = colon;
    return changed;
}

void CD_CALL ClockInstance::wake(void* value, uint64_t) noexcept {
    auto& self = *static_cast<ClockInstance*>(value);
    try {
        if (self.synchronize()) {
            self.invalidate();
        }
        if (self.schedule(self.settings) != CD_OK) {
            self.timer_.stop();
        }
    } catch (...) {
        self.timer_.stop();
    }
}

CdResult ClockInstance::render(const CdFrame& frame) {
    redraw_.beginFrame();
    return renderer_.render(settings, text_, colon_, frame, safe_) ? CD_OK : CD_ERROR_ARGUMENT;
}

void ClockInstance::layoutChanged(const CdLayout& layout) {
    if (!std::isfinite(layout.logical_width) || !std::isfinite(layout.logical_height) || layout.logical_width <= 0 ||
        layout.logical_height <= 0) {
        return;
    }
    const auto safe = layout.safe_content;
    safe_ = std::isfinite(safe.x) && std::isfinite(safe.y) && std::isfinite(safe.width) && std::isfinite(safe.height) &&
                    safe.x >= 0 && safe.y >= 0 && safe.width > 0 && safe.height > 0 &&
                    safe.x + safe.width <= layout.logical_width && safe.y + safe.height <= layout.logical_height
                ? safe
                : CdRect{0, 0, layout.logical_width, layout.logical_height};
}

void ClockInstance::setPaused(bool paused) {
    paused_ = paused;
    redraw_.pause(paused);
    renderer_.pause(paused);
    if (!paused) {
        synchronize();
    }
    schedule(settings);
    if (!paused) {
        invalidate();
    }
}

CdResult ClockInstance::applySettings(const CdSettingValue* values, uint32_t count) {
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
    synchronize();
    invalidate();
    return CD_OK;
}

void ClockInstance::invalidate() {
    redraw_.invalidate();
}
}  // namespace deepwhale
