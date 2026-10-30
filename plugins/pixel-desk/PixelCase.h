#pragma once

#include <array>

#include "plugins/pixel-desk/PixelParts.h"

namespace pixeldesk {
// Resize straight panels while keeping pixel corner parts uniformly scaled.
void pixelCase(cybercanvas::Canvas& canvas, const cybercanvas::ImageData& body, cybercanvas::Size source,
               cybercanvas::Rect visible, cybercanvas::Size world, double partHeight = 188);
}  // namespace pixeldesk
