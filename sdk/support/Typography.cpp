#include "sdk/Typography.h"

#include "sdk/Language.h"

namespace cdsdk {
cybercanvas::TextStyle textStyle(const WindowsFonts& fonts, double size, bool centered) {
    cybercanvas::TextStyle style;
    style.family = fonts.family();
    style.weight = cybercanvas::FontWeight(fonts.weight());
    style.locale = drawingLocale();
    style.size = size;
    style.align = centered ? cybercanvas::TextAlign::Center : cybercanvas::TextAlign::Start;
    style.ellipsis = true;
    return style;
}

cybercanvas::Rect rect(D2D1_RECT_F area) {
    return {area.left, area.top, std::max(0.f, area.right - area.left), std::max(0.f, area.bottom - area.top)};
}

cybercanvas::Label label(const WindowsFonts& fonts, const std::wstring& text, double size, D2D1_RECT_F area,
                         uint32_t color, bool center, bool fit) {
    return {rect(area), displayText(text), textStyle(fonts, size, center), color, fit};
}
}  // namespace cdsdk
