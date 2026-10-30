#include "plugins/steampunk-desk/SteamDigits.h"

namespace steamdesk {
void pixelDigits(cybercanvas::Canvas& canvas, std::string_view value, cybercanvas::Rect area, uint32_t color) {
    if (value.empty() || area.width <= 0 || area.height <= 0) {
        return;
    }
    const std::wstring text(value.begin(), value.end());
    cybercanvas::TextStyle style;
    style.family = L"Georgia";
    style.size = area.height * 1.2;
    style.tabularFigures = true;
    style.align = cybercanvas::TextAlign::Center;
    style.vertical = cybercanvas::VerticalAlign::Center;
    const auto fit = canvas.fitTextInk(text, style, area);
    canvas.ink(color);
    canvas.target->DrawTextLayout(D2D1::Point2F(float(fit.origin.x), float(fit.origin.y)), fit.layout.Get(),
                                  canvas.brush.Get());
}
}  // namespace steamdesk
