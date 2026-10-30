#include "plugins/pixel-desk/PixelDigits.h"

namespace pixeldesk {
void pixelDigits(cybercanvas::Canvas& canvas, std::string_view text, cybercanvas::Rect area, uint32_t color,
                 bool separated, cybercanvas::TextAlign align) {
    std::vector<cybercanvas::Point> cells;
    unsigned pen = 0;
    double minimum = std::numeric_limits<double>::max(), maximum = 0;
    for (const char ch : text) {
        const unsigned width = ch == '.' || ch == ':' ? 1 : ch == ' ' ? 3 : 5;
        const std::string glyph(1, ch);
        for (unsigned row = 0; row < 7; ++row) {
            for (unsigned column = 0; column < width; ++column) {
                const bool lit = ch == '.'   ? row == 6
                                 : ch == '-' ? row == 3
                                             : mechanicalpixels::clockCell(glyph, column + 1, row + 1);
                if (lit) {
                    cells.push_back({double(pen + column), double(row)});
                    minimum = std::min(minimum, double(pen + column));
                    maximum = std::max(maximum, double(pen + column + 1));
                }
            }
        }
        pen += width + 1;
    }
    if (cells.empty() || area.width <= 0 || area.height <= 0) {
        return;
    }
    const double unit = std::min(area.width / (maximum - minimum), area.height / 7);
    const double content = (maximum - minimum) * unit;
    const double shift = align == cybercanvas::TextAlign::End     ? area.width - content
                         : align == cybercanvas::TextAlign::Start ? 0
                                                                  : (area.width - content) / 2;
    const double left = area.x + shift - minimum * unit, top = area.y + (area.height - unit * 7) / 2;
    cybercanvas::Path glyph(canvas.factory.Get(), cybercanvas::FillRule::Winding);
    for (const auto point : cells) {
        const double x = left + point.x * unit, y = top + point.y * unit;
        const double inset = separated ? unit * .07 : 0;
        glyph.moveTo({x + inset, y + inset})
            .lineTo({x + unit - inset, y + inset})
            .lineTo({x + unit - inset, y + unit - inset})
            .lineTo({x + inset, y + unit - inset})
            .close();
    }
    canvas.fill(glyph, color);
}
}  // namespace pixeldesk
