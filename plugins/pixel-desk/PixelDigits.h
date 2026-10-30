#pragma once

#include <algorithm>
#include <limits>
#include <vector>

#include "plugins/mechanical-pixels/Pixels.h"

namespace pixeldesk {
// Reuse the established 5x7 numerals. Only decimal punctuation is added for
// calendar dates; one winding path keeps joined pixels free of AA seams.
void pixelDigits(cybercanvas::Canvas& canvas, std::string_view text, cybercanvas::Rect area, uint32_t color,
                 bool separated = false, cybercanvas::TextAlign align = cybercanvas::TextAlign::Center);
}  // namespace pixeldesk
