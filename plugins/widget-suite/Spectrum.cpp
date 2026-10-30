#include <algorithm>
#include <cmath>

#include "plugins/widget-suite/Widget.h"

namespace widgetplugin {
void Spectrum::feed(const float* data, uint32_t size) {
    input.fill(0);
    if (!data || !size)
        return;
    for (size_t i = 0; i < count; ++i) {
        const double position = double(i) * (size - 1) / (count - 1);
        const size_t a = std::min(size_t(position), size_t(size - 1)), b = std::min(a + 1, size_t(size - 1));
        const float low = std::isfinite(data[a]) ? std::clamp(data[a], 0.f, 1.f) : 0;
        const float high = std::isfinite(data[b]) ? std::clamp(data[b], 0.f, 1.f) : 0;
        input[i] = std::lerp(low, high, float(position - a));
    }
}

bool Spectrum::advance(float seconds, bool stale, double gain, double releaseMs, PeakMotion motion) {
    seconds = std::clamp(seconds, 0.f, .25f);
    const float peakHold = motion == PeakMotion::FastBall ? .04f : .18f;
    const float peakFall = motion == PeakMotion::FastBall ? 1.08f : .9f;
    bool changed = false;
    for (size_t i = 0; i < count; ++i) {
        const float target = stale ? 0 : std::pow(std::clamp(float(input[i] * gain), 0.f, 1.f), .72f);
        const float response = target > level[i] ? .012f : float(releaseMs * .001);
        level[i] += (target - level[i]) * (1 - std::exp(-seconds / response));
        if (level[i] < .0005f)
            level[i] = 0;
        if (level[i] >= peak[i]) {
            peak[i] = level[i];
            hold[i] = peakHold;
        } else if (hold[i] > 0)
            hold[i] = std::max(0.f, hold[i] - seconds);
        else
            peak[i] = std::max(level[i], peak[i] - seconds * peakFall);
        if (peak[i] < .0005f)
            peak[i] = 0;
        changed |= std::abs(level[i] - publishedLevel[i]) > .00005f || std::abs(peak[i] - publishedPeak[i]) > .00005f;
    }
    // Accumulate changes since the previous notification, including slow fades.
    if (changed) {
        publishedLevel = level;
        publishedPeak = peak;
    }
    return changed;
}

float Spectrum::sample(double position, bool peaks) const {
    const auto& values = peaks ? peak : level;
    position = std::clamp(position, 0.0, double(count - 1));
    const auto a = size_t(position), b = std::min(a + 1, count - 1);
    return std::lerp(values[a], values[b], float(position - a));
}

float Spectrum::range(double first, double last, bool peaks) const {
    first = std::clamp(first, 0.0, double(count - 1));
    last = std::clamp(last, first, double(count - 1));
    float value = std::max(sample(first, peaks), sample(last, peaks));
    const auto& values = peaks ? peak : level;
    for (int i = int(std::ceil(first)); i <= int(std::floor(last)); ++i)
        value = std::max(value, values[size_t(i)]);
    return value;
}
}  // namespace widgetplugin
