#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace cybercanvas::mechanical {
namespace detail {
class NeedleMotion {
public:
    void sample(std::span<const double> from, std::span<const double> target, std::span<double> result,
                uint64_t now) const;
    bool retarget(std::span<double> from, std::span<double> target, std::span<double> values, uint64_t now,
                  bool animate);
    bool active(std::span<const double> from, std::span<const double> target, uint64_t now) const;
    void reset(std::span<double> from, std::span<double> target);

private:
    uint64_t started_ = 0;
    bool initialized_ = false;
};
}  // namespace detail

// Owner supplies monotonic milliseconds and schedules only while active().
// Sampling is deterministic, independent of frame rate, and starts a replacement
// transition from its current position rather than its previous destination.
template <size_t Count>
class NeedleBank {
public:
    using Values = std::array<double, Count>;

    Values sample(uint64_t now) const {
        Values result{};
        motion_.sample(from_, target_, result, now);
        return result;
    }

    bool retarget(Values values, uint64_t now, bool animate) {
        return motion_.retarget(from_, target_, values, now, animate);
    }

    bool active(uint64_t now) const {
        return motion_.active(from_, target_, now);
    }

    void reset() {
        motion_.reset(from_, target_);
    }

private:
    Values from_{}, target_{};
    detail::NeedleMotion motion_;
};
}  // namespace cybercanvas::mechanical
