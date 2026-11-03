#include "LiquidGlass/Contour.h"

#include <algorithm>

namespace liquidglass {
Contour surfaceContour(double width, double height, double corner, bool attachedTop, double shoulder) {
    Contour result;
    if (width <= 0 || height <= 0) {
        return result;
    }
    const auto add = [&](ContourVerb verb, std::array<double, 6> points = {}) {
        result.segments[result.count++] = {verb, points};
    };
    constexpr double kappa = .5522847498307936;
    if (!attachedTop) {
        const double radius = std::clamp(corner, 0.0, std::min(width, height) / 2), curve = radius * kappa;
        add(ContourVerb::Move, {radius, 0});
        add(ContourVerb::Line, {width - radius, 0});
        add(ContourVerb::Cubic, {width - radius + curve, 0, width, radius - curve, width, radius});
        add(ContourVerb::Line, {width, height - radius});
        add(ContourVerb::Cubic,
            {width, height - radius + curve, width - radius + curve, height, width - radius, height});
        add(ContourVerb::Line, {radius, height});
        add(ContourVerb::Cubic, {radius - curve, height, 0, height - radius + curve, 0, height - radius});
        add(ContourVerb::Line, {0, radius});
        add(ContourVerb::Cubic, {0, radius - curve, radius - curve, 0, radius, 0});
    } else {
        const double flare = std::clamp(shoulder, 0.0, std::min(width / 4, height / 2));
        const double radius = std::clamp(corner, 0.0, std::min((width - 2 * flare) / 2, height - flare));
        const double curve = radius * kappa, shoulderCurve = flare * kappa;
        add(ContourVerb::Move, {0, 0});
        add(ContourVerb::Cubic, {shoulderCurve, 0, flare, flare - shoulderCurve, flare, flare});
        add(ContourVerb::Line, {flare, height - radius});
        add(ContourVerb::Cubic,
            {flare, height - radius + curve, flare + radius - curve, height, flare + radius, height});
        add(ContourVerb::Line, {width - flare - radius, height});
        add(ContourVerb::Cubic, {width - flare - radius + curve, height, width - flare, height - radius + curve,
                                 width - flare, height - radius});
        add(ContourVerb::Line, {width - flare, flare});
        add(ContourVerb::Cubic, {width - flare, flare - shoulderCurve, width - shoulderCurve, 0, width, 0});
    }
    add(ContourVerb::Close);
    return result;
}
}  // namespace liquidglass
