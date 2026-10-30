// Controller binding reused from plugins/pixel-desk/BasicClock.cpp.
#include <algorithm>
#include <cmath>

#include "plugins/deep-whale/BasicClock.h"

namespace deep_basic {
BasicClockInstance::BasicClockInstance(const CdCreateContext& context)
    : settings(basicDefaults(basicSpec(context.plugin_id))),
      spec_(basicSpec(context.plugin_id)),
      renderer_(context, spec_) {
    environment_.bind(context);
    timer_.bind(context);
    redraw_.bind(context);
    renderer_.onChange([this] {
        invalidate();
    });
    synchronize();
    timeline_.reset(time_, clockplugin::options(settings, {20, 36, 46, 255}), environment_.snapshot().monotonic_ms);
    if (schedule(settings) != CD_OK) {
        throw std::runtime_error("Cannot schedule Pixel clock");
    }
}

BasicClockInstance::~BasicClockInstance() {
    timer_.stop();
    renderer_.onChange({});
}

void BasicClockInstance::invalidate() {
    redraw_.invalidate();
}

void BasicClockInstance::synchronize() {
    const auto now = environment_.snapshot();
    const auto options = clockplugin::options(settings, {20, 36, 46, 255});
    time_ = clockplugin::currentTime(options, now);
    pose_ = timeline_.sample(time_, options, now.monotonic_ms);
}

CdResult BasicClockInstance::schedule(const cdsdk::Settings& values) {
    if (paused_) {
        timer_.stop();
        return CD_OK;
    }
    const auto now = environment_.snapshot();
    const bool animated =
        (spec_.kind == BasicKind::Flip && timeline_.active(now.monotonic_ms, values.at("flip_duration").number)) ||
        (spec_.kind == BasicKind::Analog && values.at("seconds").integer &&
         values.at("seconds_motion").text == "smooth");
    const uint64_t period = animated                                                          ? 33
                            : values.at("seconds").integer || spec_.kind == BasicKind::Analog ? 1000
                                                                                              : 60000;
    return timer_.replace(animated ? 33 : uint32_t(std::max(uint64_t(16), period - now.unix_ms % period)), tick, this);
}

void CD_CALL BasicClockInstance::tick(void* pointer, uint64_t) noexcept {
    auto& self = *static_cast<BasicClockInstance*>(pointer);
    try {
        self.synchronize();
        self.invalidate();
        if (self.schedule(self.settings) != CD_OK) {
            self.timer_.stop();
        }
    } catch (...) {
        self.timer_.stop();
    }
}

CdResult BasicClockInstance::render(const CdFrame& frame) {
    redraw_.beginFrame();
    if (!paused_) {
        synchronize();
    }
    const bool okay = renderer_.render(settings, time_, pose_, frame, safe_);
    if (okay && !paused_) {
        schedule(settings);
    }
    return okay ? CD_OK : CD_ERROR_ARGUMENT;
}

CdResult BasicClockInstance::applySettings(const CdSettingValue* values, uint32_t count) {
    auto next = settings;
    const auto result = next.apply(values, count);
    if (result != CD_OK) {
        return result;
    }
    const auto scheduled = schedule(next);
    if (scheduled != CD_OK) {
        return scheduled;
    }
    const bool reset = next.at("format").text != settings.at("format").text ||
                       next.at("timezone").text != settings.at("timezone").text ||
                       next.at("utc_offset").integer != settings.at("utc_offset").integer;
    settings = std::move(next);
    if (!paused_) {
        synchronize();
    }
    if (reset) {
        timeline_.reset(time_, clockplugin::options(settings, {20, 36, 46, 255}),
                        paused_ ? pausedAt_ : environment_.snapshot().monotonic_ms);
    }
    invalidate();
    return CD_OK;
}

void BasicClockInstance::layoutChanged(const CdLayout& layout) {
    const auto area = layout.safe_content;
    if (std::isfinite(area.width) && std::isfinite(area.height) && area.width > 12 && area.height > 12 && area.x >= 0 &&
        area.y >= 0 && area.x + area.width <= layout.logical_width && area.y + area.height <= layout.logical_height) {
        safe_ = area;
    } else if (layout.logical_width > 12 && layout.logical_height > 12) {
        safe_ = {0, 0, layout.logical_width, layout.logical_height};
    }
}

void BasicClockInstance::setPaused(bool paused) {
    if (paused_ == paused) {
        return;
    }
    paused_ = paused;
    pausedAt_ = environment_.snapshot().monotonic_ms;
    renderer_.pause(paused);
    redraw_.pause(paused);
    if (!paused) {
        synchronize();
        timeline_.reset(time_, clockplugin::options(settings, {20, 36, 46, 255}), pausedAt_);
        pose_ = timeline_.sample(time_, clockplugin::options(settings, {20, 36, 46, 255}), pausedAt_);
        invalidate();
    }
    schedule(settings);
}
}  // namespace deep_basic
