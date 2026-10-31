#include <cmath>

#include "plugins/brick-clock/Clock.h"

namespace brickclock {

std::array<uint64_t, 4> Instance::physicsCounters() const {
    const auto value = model_.stats();
    return {value.steps, value.advanceNanoseconds, value.awakeBodies, value.maximumAdvanceNanoseconds};
}

Instance::Instance(const CdCreateContext& context)
    : settings{{"palette", CD_SETTING_CHOICE, 0, 0, "clay", 0, 0, {"clay", "sand", "slate"}},
               {"animate", CD_SETTING_TOGGLE, 1},
               {"reduced_motion", CD_SETTING_TOGGLE, 0},
               {"debris_count", CD_SETTING_INTEGER, 12, 0, {}, 8, 20}} {
    canvas_.bindGpu(context);
    environment_.bind(context);
    redraw_.bind(context);
    updates_.bind(context);
    updates_.onInvalidate([this] {
        redraw_.invalidate();
    });
    updates_.onUpdate([this](cybercanvas::AnimationTick tick) {
        // Advance the previous frame before starting a newly changed digit. The idle poll's
        // longer delta must not skip the initial carving phase of a new second.
        bool changed = !frozen() && model_.advance(tick.delta);
        changed |= synchronize(!frozen());
        schedule();
        return cdsdk::UpdateStep{changed, true};
    });
    synchronize(false);
    schedule();
}

Instance::~Instance() {
    updates_.stop();
}

bool Instance::frozen() const {
    return hidden_ || !settings.at("animate").integer || settings.at("reduced_motion").integer;
}

bool Instance::synchronize(bool animate) {
    const auto environment = environment_.snapshot();
    const auto second = environment.unix_ms / 1000;
    if (lastSecond_ && second != *lastSecond_ && second != *lastSecond_ + 1) {
        model_.settle();
        animate = false;
    }
    lastSecond_ = second;
    const auto time = cdsdk::Environment::localTime(environment);
    return model_.setTime(time.tm_hour, time.tm_min, time.tm_sec, animate);
}

void Instance::schedule() {
    if (hidden_) {
        updates_.stop();
        return;
    }
    const auto now = environment_.snapshot();
    const auto interval = !frozen() && model_.active() ? 33u : uint32_t(1000 - now.unix_ms % 1000);
    updates_.start(interval, now.monotonic_ms);
}

CdResult Instance::render(const CdFrame& frame) {
    if (!canvas_.valid(frame)) {
        return CD_ERROR_ARGUMENT;
    }
    updates_.beginFrame();
    redraw_.beginFrame();
    canvas_.render(frame, safe_, [&](auto& canvas, CdRect safe) {
        const double scale = std::min(safe.width / kWidth, safe.height / kHeight);
        canvas.translate((safe.width - kWidth * scale) / 2, safe.height - kHeight * scale);
        canvas.scale(scale, scale);
        renderer_.draw(canvas, model_, settings.at("palette").text, scale, safe.height / scale);
    });
    return CD_OK;
}

void Instance::input(const CdInput& input) {
    if (!frozen() && input.kind == CD_INPUT_DOUBLE_CLICK && (input.buttons & CD_BUTTON_LEFT)) {
        model_.replay();
        schedule();
        redraw_.invalidate();
    }
}

void Instance::layoutChanged(const CdLayout& layout) {
    if (!std::isfinite(layout.logical_width) || !std::isfinite(layout.logical_height) || layout.logical_width <= 0 ||
        layout.logical_height <= 0) {
        return;
    }
    const auto safe = layout.safe_content;
    safe_ = std::isfinite(safe.x) && std::isfinite(safe.y) && std::isfinite(safe.width) && std::isfinite(safe.height) &&
                    safe.width > 0 && safe.height > 0
                ? safe
                : CdRect{0, 0, layout.logical_width, layout.logical_height};
    redraw_.invalidate();
}

void Instance::setPaused(bool paused) {
    hidden_ = paused;
    updates_.pause(paused);
    redraw_.pause(paused);
    model_.settle();
    if (!paused) {
        synchronize(false);
        redraw_.invalidate();
    }
    schedule();
}

CdResult Instance::applySettings(const CdSettingValue* values, uint32_t count) {
    auto next = settings;
    const auto result = next.apply(values, count);
    if (result != CD_OK) {
        return result;
    }
    const bool motionChanged = next.at("animate").integer != settings.at("animate").integer ||
                               next.at("reduced_motion").integer != settings.at("reduced_motion").integer;
    settings = std::move(next);
    model_.setChipCount(unsigned(settings.at("debris_count").integer));
    if (motionChanged) {
        model_.settle();
        synchronize(false);
    }
    schedule();
    redraw_.invalidate();
    return CD_OK;
}

void Instance::invalidate() {
    redraw_.invalidate();
}
}  // namespace brickclock
