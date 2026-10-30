#pragma once

#include "plugins/pixel-desk/PixelParts.h"
#include "plugins/pixel-desk/PixelWeather.h"

namespace pixeldesk {
// Weather illustrations are original content artwork, separate from functional
// UI icons. Condition classification stays in the existing weather model.
void weatherArt(cybercanvas::Canvas& canvas, cybercanvas::Rect bounds, int code, bool day, uint32_t backdrop = kInk);
}  // namespace pixeldesk
