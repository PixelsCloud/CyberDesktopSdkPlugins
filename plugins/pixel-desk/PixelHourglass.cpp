#include "plugins/pixel-desk/PixelHourglass.h"

namespace pixeldesk {
double hourglassHalfWidth(double position) {
    constexpr std::array<std::pair<double, double>, 10> profile{{
        {0, .053},
        {.12, .07},
        {.24, .14},
        {.40, .25},
        {.58, .34},
        {.75, .395},
        {.86, .40},
        {1., .36},
        {1.10, .30},
        {1.16, .25},
    }};
    const double value = std::clamp(std::abs(position), 0., 1.16);
    for (size_t i = 1; i < profile.size(); ++i) {
        if (value <= profile[i].first) {
            const auto [x, y] = profile[i - 1];
            return std::lerp(y, profile[i].second, (value - x) / (profile[i].first - x));
        }
    }
    return profile.back().second;
}
}  // namespace pixeldesk
