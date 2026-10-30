#pragma once

#include <CyberCanvas/Canvas.h>
#include <CyberCanvas/Surface.h>

#include "plugins/koi-suite/Pond.h"

namespace koiplugin {
// Shared procedural koi artwork for native pond skins. An offscreen owner may
// supply its target and mask surface; ordinary callers draw on the active Canvas.
void drawFish(cybercanvas::Canvas& canvas, const Fish& fish, double night, uint32_t koiColor,
              ID2D1RenderTarget* target = nullptr, ID2D1SolidColorBrush* brush = nullptr,
              cybercanvas::OffscreenSurface* surface = nullptr);
// The mechanical pond's plan-view artwork, following the same articulated spine.
void drawPlanFish(cybercanvas::Canvas& canvas, const Fish& fish, uint32_t accent);
}  // namespace koiplugin
