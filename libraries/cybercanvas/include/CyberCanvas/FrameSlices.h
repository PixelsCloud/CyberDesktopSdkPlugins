#pragma once

#include <span>

#include "CyberCanvas/Types.h"

struct ID2D1Bitmap;

namespace cybercanvas {
class Canvas;

// Extend quiet strips of a frame. Characters, circles and ornaments between
// the strips retain their original proportions; only the connecting rails grow.
double frameX(double x, double extension, std::span<const double> cuts);

cybercanvas::Rect frameRegion(cybercanvas::Rect r, double extension, std::span<const double> cuts);

void drawFrame(cybercanvas::Canvas& canvas, ID2D1Bitmap* bitmap, cybercanvas::Size original,
               cybercanvas::Rect destination, std::span<const double> cuts, double sourceScale = 1);

}  // namespace cybercanvas
