#pragma once

#include <CyberCanvas/Canvas.h>

#include <algorithm>

namespace pixeldesk {
// Recompose straight enclosure rails without stretching corners, pets or
// instruments. The middle is tiled at its original pixel scale.
void panelStrip(cybercanvas::Canvas& canvas, const cybercanvas::ImageData& image, cybercanvas::Rect source,
                double extent, double first, double last, bool vertical = false);
}  // namespace pixeldesk
