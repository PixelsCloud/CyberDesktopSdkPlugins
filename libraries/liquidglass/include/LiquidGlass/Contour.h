#pragma once

#include <array>
#include <cstdint>

namespace liquidglass {
enum class ContourVerb { Move, Line, Cubic, Close };

struct ContourSegment {
    ContourVerb verb = ContourVerb::Close;
    std::array<double, 6> points{};
};

struct Contour {
    std::array<ContourSegment, 12> segments{};
    uint32_t count = 0;
};

Contour surfaceContour(double width, double height, double corner, bool attachedTop, double shoulder = 12);
}  // namespace liquidglass
