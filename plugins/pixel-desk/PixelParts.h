#pragma once

#include <sdk/Canvas.h>

#include <cmath>
#include <numbers>

namespace pixeldesk {
inline constexpr uint32_t kInk = 0x14242eff, kCream = 0xf5e7cdff, kMint = 0x9bcdb4ff, kAmber = 0xeac17bff,
                          kClay = 0xc77d60ff;

void pixelDisc(cybercanvas::Canvas& canvas, double x, double y, double radius, uint32_t color);

void stepped(cybercanvas::Canvas& canvas, cybercanvas::Rect r, double step, uint32_t color);

void pixelGauge(cybercanvas::Canvas& canvas, double x, double y, double radius);

void pixelLeaf(cybercanvas::Canvas& canvas, cybercanvas::Rect area, uint32_t color);

void pixelBolt(cybercanvas::Canvas& canvas, double x, double y, double size);
}  // namespace pixeldesk
