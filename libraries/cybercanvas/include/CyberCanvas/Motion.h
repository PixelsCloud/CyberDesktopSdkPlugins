#pragma once

#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <vector>

#include "CyberCanvas/Animation.h"

namespace cybercanvas {
namespace motion_detail {
void valid(double value);

void valid(Point value);

void valid(Size value);

void valid(const Pose& value);
}  // namespace motion_detail

template <class Value>
struct Keyframe {
    double seconds;
    Value value;
    Ease easing = Ease::Linear;  // The segment ending at this key.
};

template <class Value>
class Keyframes {
public:
    explicit Keyframes(std::vector<Keyframe<Value>> keys) : keys_(std::move(keys)) {
        if (keys_.empty() || keys_.size() > 256 || keys_.front().seconds != 0) {
            throw std::invalid_argument("Keyframes need 1..256 keys starting at zero");
        }
        double previous = -1;
        for (const auto& key : keys_) {
            motion_detail::valid(key.value);
            if (!std::isfinite(key.seconds) || key.seconds < 0 || key.seconds > 1e6 || key.seconds <= previous ||
                static_cast<unsigned>(key.easing) > static_cast<unsigned>(Ease::OutBack)) {
                throw std::invalid_argument("Invalid keyframe time or easing");
            }
            previous = key.seconds;
        }
    }

    double duration() const {
        return keys_.back().seconds;
    }

    Value sample(double seconds) const {
        motion_detail::valid(seconds);
        if (seconds <= 0) {
            return keys_.front().value;
        }
        if (seconds >= duration()) {
            return keys_.back().value;
        }
        const auto next = std::upper_bound(keys_.begin(), keys_.end(), seconds, [](double time, const auto& key) {
            return time < key.seconds;
        });
        const auto& previous = *(next - 1);
        const double p = (seconds - previous.seconds) / (next->seconds - previous.seconds);
        return mix(previous.value, next->value, eased(p, next->easing));
    }

private:
    std::vector<Keyframe<Value>> keys_;
};

struct MotionOptions {
    double delay = 0;
    Repeat repeat = Repeat::Once;
    uint32_t cycles = 1;
    // When supplied, expired ownership cancels both sampling and notifications.
    std::optional<std::weak_ptr<void>> owner;
    std::function<void()> completed;
};

// Single-threaded, host-clock-driven animation scheduler. No timers or windows.
// Property keys identify a single writer. Schedule append/join tracks with
// distinct keys; animate() replaces an existing key from its current value.
class Timeline {
public:
    using Property = uint64_t;
    Timeline() = default;
    Timeline(const Timeline&) = delete;
    Timeline& operator=(const Timeline&) = delete;

    template <class Value, class Setter>
    void at(double start, Property property, Keyframes<Value> frames, Setter setter, MotionOptions options = {}) {
        const auto duration = frames.duration();
        atImpl(
            start, property, duration,
            [frames = std::move(frames), setter = std::move(setter)](double seconds) mutable {
                setter(frames.sample(seconds));
            },
            std::move(options));
    }

    template <class Value, class Setter>
    void append(Property property, Keyframes<Value> frames, Setter setter, MotionOptions options = {}) {
        const auto start = duration();
        at(start, property, std::move(frames), std::move(setter), std::move(options));
        joinedAt_ = start;
    }

    template <class Value, class Setter>
    void join(Property property, Keyframes<Value> frames, Setter setter, MotionOptions options = {}) {
        at(joinedAt_, property, std::move(frames), std::move(setter), std::move(options));
    }

    template <class Getter, class Setter, class Value>
    void animate(Property property, Getter getter, Setter setter, Value target, double seconds,
                 Ease easing = Ease::InOutSine, MotionOptions options = {}) {
        auto source = getter();
        std::vector<Keyframe<Value>> keys{{0, source}};
        if (seconds == 0) {
            keys[0].value = target;
        } else {
            keys.push_back({seconds, target, easing});
        }
        at(position_, property, Keyframes<Value>(std::move(keys)), std::move(setter), std::move(options));
    }

    void cancel(Property property);

    void clear();

    void paused(bool value);

    void speed(double value);

    // Seek samples without completion notifications. A following forward play
    // can complete again after explicitly seeking back before a track's end.
    void seek(double seconds);

    bool advance(double seconds);

    // Invoke at a safe update boundary, never from a render callback. Cancelled
    // or replaced tracks and expired owners suppress queued notifications.
    void dispatch();

    double position() const;

    double duration() const;

    bool active() const;

private:
    struct Track;
    void atImpl(double start, Property property, double duration, std::function<void(double)> sample,
                MotionOptions options);

    std::map<Property, std::shared_ptr<Track>> tracks_;
    std::vector<std::pair<Property, std::weak_ptr<Track>>> notifications_;
    double position_ = 0, joinedAt_ = 0, speed_ = 1;
    bool paused_ = false, sampling_ = false;

    static bool alive(const Track& track);

    void mutableTimeline() const;

    void reap();

    void sample(bool notify, bool seeking);
};
}  // namespace cybercanvas
