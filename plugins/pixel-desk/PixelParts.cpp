#include "plugins/pixel-desk/PixelParts.h"

namespace pixeldesk {
void pixelDisc(cybercanvas::Canvas& canvas, double x, double y, double radius, uint32_t color) {
    constexpr unsigned inset[]{4, 3, 2, 1, 1, 0, 0, 0, 1, 1, 2, 3, 4};
    const double unit = radius * 2 / std::size(inset);
    auto path = canvas.path();
    path.moveTo({x - radius + inset[0] * unit, y - radius}).lineTo({x + radius - inset[0] * unit, y - radius});
    for (unsigned row = 0; row < std::size(inset); ++row) {
        const double bottom = y - radius + (row + 1) * unit;
        path.lineTo({x + radius - inset[row] * unit, bottom});
        if (row + 1 < std::size(inset)) {
            path.lineTo({x + radius - inset[row + 1] * unit, bottom});
        }
    }
    path.lineTo({x - radius + inset[std::size(inset) - 1] * unit, y + radius});
    for (int row = int(std::size(inset)) - 1; row >= 0; --row) {
        const double top = y - radius + row * unit;
        path.lineTo({x - radius + inset[row] * unit, top});
        if (row) {
            path.lineTo({x - radius + inset[row - 1] * unit, top});
        }
    }
    path.close();
    canvas.fill(path, color);
}

void stepped(cybercanvas::Canvas& canvas, cybercanvas::Rect r, double step, uint32_t color) {
    auto path = canvas.path();
    path.moveTo({r.x + step, r.y})
        .lineTo({r.x + r.width - step, r.y})
        .lineTo({r.x + r.width - step, r.y + step})
        .lineTo({r.x + r.width, r.y + step})
        .lineTo({r.x + r.width, r.y + r.height - step})
        .lineTo({r.x + r.width - step, r.y + r.height - step})
        .lineTo({r.x + r.width - step, r.y + r.height})
        .lineTo({r.x + step, r.y + r.height})
        .lineTo({r.x + step, r.y + r.height - step})
        .lineTo({r.x, r.y + r.height - step})
        .lineTo({r.x, r.y + step})
        .lineTo({r.x + step, r.y + step})
        .close();
    canvas.fill(path, color);
}

void pixelGauge(cybercanvas::Canvas& canvas, double x, double y, double radius) {
    const auto previous = canvas.target->GetAntialiasMode();
    canvas.target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    // Rebuild the complete inner bezel as well as the face, covering every
    // baked mark. One shared grid makes the lip, aperture and scale concentric;
    // independent circles/polygons can rasterize onto different pixel edges.
    constexpr int half = 48;
    const double step = radius / half;
    const auto colorAt = [](int column, int row) -> uint32_t {
        const double dx = column + .5, dy = row + .5;
        const double distance = std::hypot(dx, dy);
        if (distance > half) {
            return 0;
        }
        if (distance > 46.5) {
            return kCream;
        }
        if (distance > 44.25) {
            return kMint;
        }
        if (distance >= 33.5 && distance <= 40) {
            double angle = std::atan2(dy, dx) * 180 / std::numbers::pi;
            if (angle < 147) {
                angle += 360;
            }
            const int index = int(std::round((angle - 150) / 12));
            if (index >= 0 && index < 21 && std::abs(angle - (150 + index * 12)) <= 2.8) {
                return index < 13 ? 0x69a98bff : index < 17 ? kAmber : 0xd56748ff;
            }
        }
        return kInk;
    };
    // Coalesce adjacent equal cells; only static, locally cached artwork is
    // rebuilt. Each tick keeps a clear angular gap at the smallest host size.
    for (int row = -half; row < half; ++row) {
        for (int column = -half; column < half;) {
            const uint32_t color = colorAt(column, row);
            const int begin = column++;
            while (column < half && colorAt(column, row) == color) {
                ++column;
            }
            if (color) {
                canvas.box(x + begin * step, y + row * step, (column - begin) * step, step, 0, color);
            }
        }
    }
    canvas.target->SetAntialiasMode(previous);
}

void pixelLeaf(cybercanvas::Canvas& canvas, cybercanvas::Rect area, uint32_t color) {
    constexpr const char* rows[]{"000000011110", "000001111110", "000111111110", "001111111100",
                                 "011111111100", "011111111000", "111111110000", "111111100000",
                                 "011111000000", "001100000000", "011000000000", "110000000000"};
    const double unit = std::min(area.width, area.height) / 12;
    const double x = area.x + (area.width - unit * 12) / 2, y = area.y + (area.height - unit * 12) / 2;
    const auto previous = canvas.target->GetAntialiasMode();
    canvas.target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    for (unsigned row = 0; row < 12; ++row) {
        for (unsigned column = 0; column < 12; ++column) {
            if (rows[row][column] == '1') {
                canvas.box(x + column * unit, y + row * unit, unit, unit, 0, color);
            }
        }
    }
    for (unsigned i = 0; i < 5; ++i) {
        canvas.box(x + (3 + i) * unit, y + (8 - i) * unit, unit, unit, 0, kInk);
    }
    canvas.target->SetAntialiasMode(previous);
}

void pixelBolt(cybercanvas::Canvas& canvas, double x, double y, double size) {
    stepped(canvas, {x - size, y - size, size * 2, size * 2}, size * .5, kInk);
    stepped(canvas, {x - size * .65, y - size * .65, size * 1.3, size * 1.3}, size * .3, kAmber);
    canvas.box(x - size * .35, y - size * .4, size * .55, size * .35, 0, 0xfff5dcff);
}
}  // namespace pixeldesk
