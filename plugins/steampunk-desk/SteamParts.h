#pragma once

#include <sdk/Canvas.h>

namespace steamdesk {
inline constexpr uint32_t kInk = 0x181613ff, kCream = 0xefe1bfff, kMint = 0x8d7860ff, kAmber = 0xb98a4fff,
                          kClay = 0x924b39ff;

// Shared renderer call sites use continuous contours sampled at the host DPI.
void pixelDisc(cybercanvas::Canvas& canvas, double x, double y, double radius, uint32_t color);

void stepped(cybercanvas::Canvas& canvas, cybercanvas::Rect area, double radius, uint32_t color);

void pixelBolt(cybercanvas::Canvas& canvas, double x, double y, double radius);
}  // namespace steamdesk
