#pragma once

#include <d2d1.h>

namespace cdsdk {
// Grayscale text keeps transparent plugin surfaces free of ClearType color
// fringes. Render at the host's physical frame size and supplied DPI.
void configureAntialiasing(ID2D1RenderTarget* target);
}  // namespace cdsdk
