#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>

#include "CyberCanvas/Types.h"

namespace cybercanvas {
enum class Ease { Linear, InCubic, OutCubic, InOutCubic, InOutSine, OutBack };
enum class Repeat { Once, Loop, PingPong };

struct Timing {
    double duration = 1;
    Ease easing = Ease::Linear;
    Repeat repeat = Repeat::Once;
    uint32_t cycles = 1;  // Zero means infinite; PingPong counts complete round trips.
};

namespace animation_detail {
void validateTiming(const Timing& timing);

struct Phase {
    double progress;
    bool finished;
};

Phase sampleTiming(const Timing& timing, double seconds);
}  // namespace animation_detail

double eased(double progress, Ease easing);

double mix(double from, double to, double progress);

Point mix(Point from, Point to, double progress);

Size mix(Size from, Size to, double progress);

Pose mix(const Pose& from, const Pose& to, double progress);

template <typename Value>
struct AnimationSample {
    Value value;
    bool active = false, finished = false;
};

// Stateless sampling keeps results independent of frame count. The caller owns
// time and redraw scheduling; no timer, window or ABI is retained by a track.
template <typename Value>
class Tween {
public:
    Tween(Value from, Value to, Timing timing = {}) : from_(from), to_(to), timing_(timing) {
        animation_detail::validateTiming(timing);
    }

    AnimationSample<Value> sample(double seconds) const {
        const auto phase = animation_detail::sampleTiming(timing_, seconds);
        if (phase.finished) {
            return {timing_.repeat == Repeat::PingPong ? from_ : to_, false, true};
        }
        return {mix(from_, to_, phase.progress), true, false};
    }

private:
    Value from_, to_;
    Timing timing_;
};

struct AnimationTick {
    double seconds = 0, delta = 0, unscaledDelta = 0;
};

// Use host monotonic milliseconds with tick(), or explicit seconds with advance()
// for deterministic rendering. A resumed clock establishes a fresh time anchor.
class AnimationClock {
public:
    explicit AnimationClock(double seconds = 0);

    AnimationTick tick(uint64_t milliseconds);

    AnimationTick advance(double seconds);

    void synchronize(uint64_t milliseconds);

    void resetAnchor();

    void seek(double seconds);

    void setSpeed(double speed);

    void setPaused(bool paused);

    void setMaximumStep(double seconds);

    double seconds() const;

    double speed() const;

    bool paused() const;

private:
    std::optional<uint64_t> anchor_;
    double elapsed_ = 0, speed_ = 1, maximumStep_ = .25;
    bool paused_ = false;
};
}  // namespace cybercanvas
