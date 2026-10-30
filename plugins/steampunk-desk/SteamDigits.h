#pragma once

#include <sdk/Canvas.h>

namespace steamdesk {
// Mechanical readouts use centered enamel/printed figures, rather than a
// pixel font. Dot matrices have their own emitter renderer.
void pixelDigits(cybercanvas::Canvas& canvas, std::string_view value, cybercanvas::Rect area, uint32_t color);
}  // namespace steamdesk
