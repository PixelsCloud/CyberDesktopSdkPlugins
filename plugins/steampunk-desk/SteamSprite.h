#pragma once

#include <sdk/Canvas.h>

namespace steamdesk {
// Source rectangles are normalized to the original atlas. WIC may decode a
// smaller bitmap for a smaller widget; registration remains independent of it.
void sprite(cybercanvas::Canvas& canvas, const cybercanvas::ImageData& image, cybercanvas::Size original,
            cybercanvas::Rect source, cybercanvas::Rect destination);
}  // namespace steamdesk
