#pragma once

#include <CyberCanvas/DesktopWidgets.h>

#include "sdk/Canvas.h"
#include "sdk/WindowsFonts.h"

namespace cdsdk {
cybercanvas::TextStyle textStyle(const WindowsFonts& fonts, double size, bool centered = true);

cybercanvas::Rect rect(D2D1_RECT_F area);

cybercanvas::Label label(const WindowsFonts& fonts, const std::wstring& text, double size, D2D1_RECT_F area,
                         uint32_t color, bool center = true, bool fit = false);
}  // namespace cdsdk
