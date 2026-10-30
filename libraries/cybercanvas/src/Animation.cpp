#include "CyberCanvas/Animation.h"

#include <tweeny/easing.h>

namespace cybercanvas::animation_detail {
void validateTiming(const Timing& timing) {
    if (!std::isfinite(timing.duration) || timing.duration < 1e-6 || timing.duration > 1e9 ||
        (timing.repeat == Repeat::Once && timing.cycles != 1)) {
        throw std::invalid_argument("Invalid animation timing");
    }
}

Phase sampleTiming(const Timing& timing, double seconds) {
    if (!std::isfinite(seconds) || seconds < 0) {
        throw std::invalid_argument("Invalid animation time");
    }
    const double period = timing.duration * (timing.repeat == Repeat::PingPong ? 2 : 1);
    if (timing.cycles && seconds >= period * timing.cycles) {
        return {0, true};
    }
    double phase = std::fmod(seconds, period) / timing.duration;
    if (phase > 1) {
        phase = 2 - phase;
    }
    return {eased(phase, timing.easing), false};
}
}  // namespace cybercanvas::animation_detail

namespace cybercanvas {
double eased(double progress, Ease easing) {
    if (!std::isfinite(progress)) {
        throw std::invalid_argument("Invalid animation progress");
    }
    progress = std::clamp(progress, 0., 1.);
    if (progress == 0 || progress == 1) {
        return progress;
    }
    const float p = float(progress);
    switch (easing) {
        case Ease::Linear:
            return tweeny::easing::linear.run(p, 0., 1.);
        case Ease::InCubic:
            return tweeny::easing::cubicIn.run(p, 0., 1.);
        case Ease::OutCubic:
            return tweeny::easing::cubicOut.run(p, 0., 1.);
        case Ease::InOutCubic:
            return tweeny::easing::cubicInOut.run(p, 0., 1.);
        case Ease::InOutSine:
            return tweeny::easing::sinusoidalInOut.run(p, 0., 1.);
        case Ease::OutBack:
            return tweeny::easing::backOut.run(p, 0., 1.);
    }
    throw std::invalid_argument("Invalid easing function");
}

double mix(double from, double to, double progress) {
    return std::lerp(from, to, progress);
}

Point mix(Point from, Point to, double progress) {
    return {mix(from.x, to.x, progress), mix(from.y, to.y, progress)};
}

Size mix(Size from, Size to, double progress) {
    return {mix(from.width, to.width, progress), mix(from.height, to.height, progress)};
}

Pose mix(const Pose& from, const Pose& to, double progress) {
    return {mix(from.translation, to.translation, progress), mix(from.pivot, to.pivot, progress),
            mix(from.scale, to.scale, progress), mix(from.rotation, to.rotation, progress),
            mix(from.opacity, to.opacity, progress)};
}

AnimationClock::AnimationClock(double seconds) {
    seek(seconds);
}

auto AnimationClock::tick(uint64_t milliseconds) -> AnimationTick {
    double delta = 0;
    if (anchor_ && milliseconds >= *anchor_) {
        delta = (milliseconds - *anchor_) / 1000.;
    }
    anchor_ = milliseconds;
    return advance(delta);
}

auto AnimationClock::advance(double seconds) -> AnimationTick {
    if (!std::isfinite(seconds) || seconds < 0) {
        throw std::invalid_argument("Invalid animation delta");
    }
    const double raw = paused_ ? 0 : std::min(seconds, maximumStep_);
    const double delta = raw * speed_;
    const double next = elapsed_ + delta;
    if (!std::isfinite(next)) {
        throw std::overflow_error("Animation clock overflow");
    }
    elapsed_ = next;
    return {elapsed_, delta, raw};
}

auto AnimationClock::synchronize(uint64_t milliseconds) -> void {
    anchor_ = milliseconds;
}

auto AnimationClock::resetAnchor() -> void {
    anchor_.reset();
}

auto AnimationClock::seek(double seconds) -> void {
    if (!std::isfinite(seconds) || seconds < 0) {
        throw std::invalid_argument("Invalid animation seek");
    }
    elapsed_ = seconds;
}

auto AnimationClock::setSpeed(double speed) -> void {
    if (!std::isfinite(speed) || speed < 0 || speed > 100) {
        throw std::invalid_argument("Invalid animation speed");
    }
    speed_ = speed;
}

auto AnimationClock::setPaused(bool paused) -> void {
    if (paused_ != paused) {
        anchor_.reset();
        paused_ = paused;
    }
}

auto AnimationClock::setMaximumStep(double seconds) -> void {
    if (!std::isfinite(seconds) || seconds <= 0 || seconds > 60) {
        throw std::invalid_argument("Invalid animation maximum step");
    }
    maximumStep_ = seconds;
}

auto AnimationClock::seconds() const -> double {
    return elapsed_;
}

auto AnimationClock::speed() const -> double {
    return speed_;
}

auto AnimationClock::paused() const -> bool {
    return paused_;
}
}  // namespace cybercanvas
