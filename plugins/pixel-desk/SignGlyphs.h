#pragma once

#include <limits>

#include "plugins/mechanical-pixels/Pixels.h"
#include "plugins/pixel-desk/PixelParts.h"

namespace pixeldesk {
// Original 5x7 letter artwork. Unicode graphemes outside this small display
// alphabet retain the existing native text fallback and are never discarded.
bool signGlyph(cybercanvas::Canvas& canvas, std::wstring_view text, cybercanvas::Rect bounds);
}  // namespace pixeldesk
