#include "CyberCanvas/InstrumentMotion.h"

#include <algorithm>
#include <cmath>

#include "CyberCanvas/Animation.h"

namespace cybercanvas::mechanical::detail {
void NeedleMotion::sample(std::span<const double> from, std::span<const double> target, std::span<double> result,
                          uint64_t now) const {
    const double seconds = now >= started_ ? (now - started_) * .001 : 0;
    for (size_t i = 0; i < result.size(); ++i) {
        result[i] = Tween<double>(from[i], target[i], {.6, Ease::OutCubic}).sample(seconds).value;
    }
}

bool NeedleMotion::retarget(std::span<double> from, std::span<double> target, std::span<double> values, uint64_t now,
                            bool animate) {
    for (auto& value : values) {
        value = std::isfinite(value) ? std::clamp(value, 0., 1.) : 0;
    }
    if (initialized_ && std::equal(values.begin(), values.end(), target.begin()) && animate) {
        return active(from, target, now);
    }
    if (initialized_ && animate) {
        sample(from, target, from, now);
    } else {
        std::copy(values.begin(), values.end(), from.begin());
    }
    std::copy(values.begin(), values.end(), target.begin());
    started_ = now;
    initialized_ = true;
    return active(from, target, now);
}

bool NeedleMotion::active(std::span<const double> from, std::span<const double> target, uint64_t now) const {
    return initialized_ && now >= started_ && now - started_ < 600 &&
           !std::equal(from.begin(), from.end(), target.begin());
}

void NeedleMotion::reset(std::span<double> from, std::span<double> target) {
    initialized_ = false;
    std::fill(from.begin(), from.end(), 0.);
    std::fill(target.begin(), target.end(), 0.);
    started_ = 0;
}
}  // namespace cybercanvas::mechanical::detail
