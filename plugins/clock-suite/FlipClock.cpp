#include "plugins/clock-suite/FlipClock.h"

#include <algorithm>
#include <cmath>

namespace clockplugin {
namespace {
std::array<int, 3> digits(Time time, const Options& options) {
    return {options.twelveHour ? (time.hour % 12 ? time.hour % 12 : 12) : time.hour, time.minute, time.second};
}
}  // namespace

void FlipTimeline::reset(Time time, const Options& options, uint64_t now) {
    const auto current = digits(time, options);
    for (size_t i = 0; i < values.size(); ++i)
        values[i] = {current[i], current[i], now};
}

FlipPose FlipTimeline::sample(Time time, const Options& options, uint64_t now) {
    const auto current = digits(time, options);
    FlipPose pose;
    for (size_t i = 0; i < values.size(); ++i) {
        auto& value = values[i];
        if (i == 2 && !options.seconds)
            value = {current[i], current[i], now};
        if (value.after < 0)
            value = {current[i], current[i], now};
        else if (value.after != current[i])
            value = {value.after, current[i], now};
        const double progress =
            value.before == value.after
                ? 1
                : std::clamp(double(now >= value.started ? now - value.started : 0) / options.flipDurationMs, 0.0, 1.0);
        pose.values[i] = {value.before, value.after, progress};
        if (progress >= 1)
            value.before = value.after;
    }
    return pose;
}

bool FlipTimeline::active(uint64_t now, double duration) const {
    for (const auto& value : values)
        if (value.before != value.after && value.after >= 0 && now >= value.started && now - value.started < duration)
            return true;
    return false;
}

FlipGeometry arrangeFlip(Spec spec, double width, double height, bool showSeconds) {
    FlipGeometry result;
    if (width <= 0 || height <= 0)
        return result;
    // The host reserves the same 4 logical pixels as the spectrum. Keep the
    // outer card face bounds identical. Seconds are inside the minute card.
    const double left = 1, top = 1, w = std::max(1.0, width - 4), h = std::max(1.0, height - 5);
    if (spec.columns == 4) {
        const double gap = std::clamp(width * .025, 4.0, 10.0);
        const double cardWidth = std::max(1.0, (w - gap) * .5);
        const double side = showSeconds ? std::min(cardWidth * .24, h * .16) : 0;
        result.hours = {left, top, cardWidth, h};
        result.minutes = {left + cardWidth + gap, top, cardWidth, h};
        if (showSeconds)
            result.seconds = {result.minutes.x + cardWidth * .92 - side, top + h * .92 - side, side, side};
        return result;
    }
    const double gap = std::clamp(height * .035, 3.0, 8.0);
    const double cardHeight = std::max(1.0, (h - gap) * .5);
    const double side = showSeconds ? std::min(w * .18, cardHeight * .32) : 0;
    result.hours = {left, top, w, cardHeight};
    result.minutes = {left, top + cardHeight + gap, w, cardHeight};
    if (showSeconds)
        result.seconds = {left + w * .92 - side, result.minutes.y + (cardHeight - side) * .5, side, side};
    return result;
}
}  // namespace clockplugin
