#include "CyberCanvas/Motion.h"

namespace cybercanvas {
struct Timeline::Track {
    double start = 0, period = 0, span = 0;
    bool finished = false;
    MotionOptions options;
    std::function<void(double)> sample;
};

void Timeline::atImpl(double start, Property property, double duration, std::function<void(double)> sample,
                      MotionOptions options) {
    mutableTimeline();
    if (!property || !std::isfinite(start) || start < 0 || start > 1e6 || !std::isfinite(options.delay) ||
        options.delay < 0 || options.delay > 1e6 || static_cast<unsigned>(options.repeat) > 2 ||
        (options.repeat == Repeat::Once && options.cycles != 1) || (duration == 0 && options.repeat != Repeat::Once) ||
        (tracks_.size() >= 1024 && !tracks_.contains(property))) {
        throw std::invalid_argument("Invalid timeline track or capacity");
    }
    auto track = std::make_shared<Track>();
    track->start = start + options.delay;
    track->period = duration * (options.repeat == Repeat::PingPong ? 2 : 1);
    track->span = options.cycles ? track->period * options.cycles : std::numeric_limits<double>::infinity();
    track->options = std::move(options);
    track->sample = std::move(sample);
    tracks_[property] = std::move(track);
}
}  // namespace cybercanvas

namespace cybercanvas::motion_detail {
void valid(double value) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument("Non-finite animation value");
    }
}

void valid(Point value) {
    valid(value.x);
    valid(value.y);
}

void valid(Size value) {
    valid(value.width);
    valid(value.height);
}

void valid(const Pose& value) {
    valid(value.translation);
    valid(value.pivot);
    valid(value.scale);
    valid(value.rotation);
    valid(value.opacity);
}
}  // namespace cybercanvas::motion_detail

namespace cybercanvas {
auto Timeline::cancel(Property property) -> void {
    mutableTimeline();
    tracks_.erase(property);
}

auto Timeline::clear() -> void {
    mutableTimeline();
    tracks_.clear();
    notifications_.clear();
    position_ = joinedAt_ = 0;
}

auto Timeline::paused(bool value) -> void {
    mutableTimeline();
    paused_ = value;
}

auto Timeline::speed(double value) -> void {
    mutableTimeline();
    if (!std::isfinite(value) || std::abs(value) > 100) {
        throw std::invalid_argument("Timeline speed must be finite and within -100..100");
    }
    speed_ = value;
}

auto Timeline::seek(double seconds) -> void {
    mutableTimeline();
    if (!std::isfinite(seconds) || seconds < 0 || seconds > 1e9) {
        throw std::invalid_argument("Invalid timeline seek");
    }
    position_ = seconds;
    notifications_.clear();
    sample(false, true);
}

auto Timeline::advance(double seconds) -> bool {
    mutableTimeline();
    if (!std::isfinite(seconds) || seconds < 0 || seconds > 1e6) {
        throw std::invalid_argument("Invalid timeline delta");
    }
    reap();
    if (paused_ || speed_ == 0 || tracks_.empty()) {
        return false;
    }
    const auto next = std::clamp(position_ + seconds * speed_, 0., duration());
    const bool changed = next != position_;
    position_ = next;
    sample(speed_ >= 0, false);
    return changed;
}

auto Timeline::dispatch() -> void {
    mutableTimeline();
    auto notifications = std::move(notifications_);
    notifications_.clear();
    for (const auto& [property, weak] : notifications) {
        const auto track = weak.lock();
        const auto found = tracks_.find(property);
        if (track && found != tracks_.end() && found->second == track && alive(*track) && track->options.completed) {
            track->options.completed();
        }
    }
}

auto Timeline::position() const -> double {
    return position_;
}

auto Timeline::duration() const -> double {
    double result = 0;
    for (const auto& [property, track] : tracks_) {
        if (alive(*track)) {
            result = std::max(result, track->start + track->span);
        }
    }
    return result;
}

auto Timeline::active() const -> bool {
    if (paused_ || speed_ == 0) {
        return false;
    }
    for (const auto& [property, track] : tracks_) {
        if (alive(*track) && (speed_ > 0 ? !track->finished : position_ > 0)) {
            return true;
        }
    }
    return false;
}

auto Timeline::alive(const Track& track) -> bool {
    return !track.options.owner || !track.options.owner->expired();
}

auto Timeline::mutableTimeline() const -> void {
    if (sampling_) {
        throw std::logic_error("Mutate animations from deferred notifications, not property setters");
    }
}

auto Timeline::reap() -> void {
    std::erase_if(tracks_, [](const auto& entry) {
        return !alive(*entry.second);
    });
}

auto Timeline::sample(bool notify, bool seeking) -> void {
    reap();
    sampling_ = true;
    try {
        for (const auto& [property, track] : tracks_) {
            const auto elapsed = position_ - track->start;
            const bool finished = elapsed >= track->span;
            if (elapsed < 0 && !seeking && speed_ >= 0) {
                continue;
            }
            double phase = elapsed <= 0 ? 0 : finished ? track->period : std::fmod(elapsed, track->period);
            if (track->options.repeat == Repeat::PingPong && phase > track->period / 2) {
                phase = track->period - phase;
            }
            track->sample(phase);
            if (notify && finished && !track->finished) {
                notifications_.emplace_back(property, track);
            }
            track->finished = finished;
        }
        sampling_ = false;
    } catch (...) {
        sampling_ = false;
        throw;
    }
}
}  // namespace cybercanvas
