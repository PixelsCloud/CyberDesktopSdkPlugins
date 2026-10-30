#include "plugins/pixel-desk/WeatherArt.h"

namespace pixeldesk {
void weatherArt(cybercanvas::Canvas& canvas, cybercanvas::Rect bounds, int code, bool day, uint32_t backdrop) {
    const auto kind = weatherplugin::iconName(code, day);
    const double scale = std::min(bounds.width, bounds.height) / 128;
    canvas.saved([&] {
        canvas.translate(bounds.x + (bounds.width - 128 * scale) / 2, bounds.y + (bounds.height - 128 * scale) / 2);
        canvas.scale(scale, scale);
        const auto box = [&](double x, double y, double width, double height, uint32_t color) {
            canvas.box(x, y, width, height, 0, color);
        };
        const auto sun = [&] {
            for (const auto r : {cybercanvas::Rect{51, 3, 6, 13},
                                 {51, 83, 6, 13},
                                 {7, 47, 13, 6},
                                 {89, 47, 13, 6},
                                 {18, 15, 7, 7},
                                 {25, 22, 6, 6},
                                 {83, 15, 7, 7},
                                 {77, 22, 6, 6},
                                 {18, 78, 7, 7},
                                 {25, 72, 6, 6},
                                 {83, 78, 7, 7},
                                 {77, 72, 6, 6}}) {
                box(r.x, r.y, r.width, r.height, 0xf4bd4aff);
            }
            pixelDisc(canvas, 54, 49, 33, 0x976431ff);
            pixelDisc(canvas, 54, 47, 30, 0xf6ba47ff);
            pixelDisc(canvas, 51, 43, 25, 0xffd370ff);
            stepped(canvas, {34, 26, 17, 9}, 3, 0xfff2bbff);
            box(29, 35, 7, 12, 0xffedb3ff);
            for (unsigned i = 0; i < 8; ++i) {
                box(65 + (i % 3) * 4, 51 + (i / 3) * 4, 3, 3, i % 2 ? 0xf6ba47ff : 0xffd370ff);
            }
        };
        const auto moon = [&] {
            pixelDisc(canvas, 52, 47, 30, 0x80a9c5ff);
            pixelDisc(canvas, 50, 44, 27, kCream);
            pixelDisc(canvas, 64, 31, 27, backdrop);
            box(24, 51, 7, 7, 0xd4dcc9ff);
            for (const auto p : {cybercanvas::Point{93, 26}, {84, 54}, {22, 19}}) {
                box(p.x - 2, p.y - 6, 4, 12, kCream);
                box(p.x - 6, p.y - 2, 12, 4, kCream);
            }
        };
        const auto cloud = [&](double y, bool dark) {
            canvas.saved([&] {
                canvas.translate(0, y);
                pixelDisc(canvas, 42, 62, 25, kInk);
                pixelDisc(canvas, 75, 53, 31, kInk);
                pixelDisc(canvas, 102, 70, 23, kInk);
                stepped(canvas, {16, 62, 109, 32}, 8, kInk);
                stepped(canvas, {20, 64, 101, 26}, 5, dark ? 0x7497b2ff : 0xb3cad7ff);
                pixelDisc(canvas, 42, 61, 21, dark ? 0x8cabbfff : 0xdce3dbff);
                pixelDisc(canvas, 74, 52, 27, dark ? 0x9db7c5ff : 0xfff4dbff);
                pixelDisc(canvas, 101, 69, 19, dark ? 0x7c9cb4ff : 0xdce3dbff);
                box(30, 76, 70, 10, dark ? 0x7497b2ff : 0xc3d4d9ff);
                box(49, 36, 10, 9, dark ? 0xb3c7ceff : 0xfff9e9ff);
                box(61, 27, 15, 6, dark ? 0xb3c7ceff : 0xfff9e9ff);
                for (unsigned i = 0; i < 8; ++i) {
                    box(42 + i * 7, 86 - (i % 2) * 4, 4, 4, dark ? 0x587c9bff : 0x9bb9ccff);
                }
            });
        };
        const auto snowflake = [&](double x, double y, double size) {
            box(x - size, y - 1.5, size * 2, 3, kCream);
            box(x - 1.5, y - size, 3, size * 2, kCream);
            for (double side : {-1., 1.}) {
                box(x + side * size * .55 - 1.5, y - size * .55 - 1.5, 3, 3, 0x9bcdddff);
                box(x + side * size * .55 - 1.5, y + size * .55 - 1.5, 3, 3, 0x9bcdddff);
            }
        };
        if (kind == "not-available") {
            stepped(canvas, {26, 35, 76, 60}, 10, 0x72848fff);
            stepped(canvas, {31, 40, 66, 50}, 8, backdrop);
            box(44, 62, 40, 6, 0x98aebaff);
            return;
        }
        if (kind == "snowflake") {
            snowflake(64, 62, 34);
            return;
        }
        if (kind.find("clear") != std::string::npos || kind.find("partly-cloudy") != std::string::npos) {
            day ? sun() : moon();
            const bool clear = kind == "clear-day" || kind == "clear-night";
            canvas.saved([&] {
                if (clear) {
                    canvas.translate(35, 51);
                    canvas.scale(.65, .65);
                } else {
                    canvas.translate(0, 20);
                }
                cloud(0, false);
            });
            return;
        }
        const bool fog = kind == "fog", snow = kind == "snow", sleet = kind == "sleet",
                   storm = kind.find("thunderstorms") != std::string::npos,
                   wet = kind == "rain" || kind == "drizzle" || sleet || storm;
        cloud(-9, wet || snow || fog);
        if (fog) {
            box(18, 88, 75, 5, 0x9bbbcaff);
            box(37, 101, 76, 5, kCream);
            box(14, 114, 79, 5, 0x9bbbcaff);
        } else if (snow || sleet) {
            for (double x : {34., 66., 97.}) {
                snowflake(x, 105 + (x == 66 ? 10 : 0), 8);
            }
        } else if (wet && !storm) {
            for (unsigned i = 0; i < (kind == "drizzle" ? 2u : 3u); ++i) {
                const double x = 38 + i * 29;
                box(x, 88, 6, kind == "drizzle" ? 9 : 18, 0x70bce5ff);
                box(x - 3, 103, 6, 8, 0x70bce5ff);
                box(x + 2, 89, 3, 7, 0xd5eff1ff);
            }
        }
        if (storm) {
            auto bolt = canvas.path();
            bolt.moveTo({69, 78})
                .lineTo({50, 105})
                .lineTo({64, 105})
                .lineTo({56, 126})
                .lineTo({89, 96})
                .lineTo({73, 96})
                .lineTo({83, 78})
                .close();
            canvas.fill(bolt, kAmber);
            canvas.stroke(bolt, kInk, {.width = 2});
        }
    });
}
}  // namespace pixeldesk
