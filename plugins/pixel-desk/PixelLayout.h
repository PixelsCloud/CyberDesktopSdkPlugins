#pragma once

#include <CyberCanvas/Canvas.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace pixeldesk {
inline constexpr double kOuterInset = 6;

// Coordinates are logical pixels. DPI conversion belongs to the canvas, so
// the required inset must never be multiplied by DPI here.
cybercanvas::Rect fitBody(double width, double height, double bodyWidth, double bodyHeight);
}  // namespace pixeldesk
