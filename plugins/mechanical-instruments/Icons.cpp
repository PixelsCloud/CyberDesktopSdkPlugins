#include "plugins/mechanical-instruments/Icons.h"

namespace mechanicalart {
cybercanvas::Path iconPath(cybercanvas::Canvas& canvas, Icon icon) {
    auto path = canvas.path();
    const auto rectangle = [&](double x, double y, double w, double h) {
        path.moveTo({x, y}).lineTo({x + w, y}).lineTo({x + w, y + h}).lineTo({x, y + h}).close();
    };
    switch (icon) {
        case Icon::Play:
            // Center the filled triangle's visual mass rather than its bounding box.
            path.moveTo({-5.5, -9}).lineTo({11, 0}).lineTo({-5.5, 9}).close();
            break;
        case Icon::Pause:
            rectangle(-8, -9, 5, 18);
            rectangle(3, -9, 5, 18);
            break;
        case Icon::Stop:
            rectangle(-8, -8, 16, 16);
            break;
        case Icon::Previous:
        case Icon::Next: {
            const double direction = icon == Icon::Next ? 1 : -1;
            path.moveTo({-9 * direction, -9}).lineTo({5 * direction, 0}).lineTo({-9 * direction, 9}).close();
            rectangle(icon == Icon::Next ? 6 : -10, -9, 4, 18);
            break;
        }
        case Icon::Reset: {
            // One contour joins the counterclockwise head to its circular shaft.
            // Separate overlapping figures would cancel at the joint under even-odd fill.
            path.moveTo({-11, -5})
                .lineTo({-3, -11})
                .lineTo({-3, -7})
                .lineTo({0, -7})
                .arcTo({-7.794, 6.5}, {9, 9}, 0, cybercanvas::ArcSweep::Clockwise, cybercanvas::ArcSize::Large)
                .lineTo({-4.33, 4.5})
                .arcTo({0, -3}, {5, 5}, 0, cybercanvas::ArcSweep::CounterClockwise, cybercanvas::ArcSize::Large)
                .lineTo({-3, -3})
                .lineTo({-3, 1})
                .close();
            break;
        }
        case Icon::Disk:
            path.moveTo({-6.5, -9})
                .lineTo({6.5, -9})
                .lineTo({10, 5})
                .lineTo({10, 9})
                .lineTo({-10, 9})
                .lineTo({-10, 5})
                .close();
            rectangle(-7, 4.5, 14, 1.8);
            rectangle(5, 6.9, 2, 1.1);
            break;
        case Icon::Humidity:
            path.moveTo({0, -11})
                .cubicTo({-2, -6}, {-8, -1}, {-8, 3})
                .cubicTo({-8, 13}, {8, 13}, {8, 3})
                .cubicTo({8, -1}, {2, -6}, {0, -11})
                .close();
            path.moveTo({-4, 1}).cubicTo({-7, 5}, {-3, 9}, {0, 7}).lineTo({-2, 5}).close();
            break;
        case Icon::Wind:
            for (unsigned i = 0; i < 3; ++i) {
                const double y = -4 + i * 6.;
                const double left = i == 1 ? -11 : -8;
                const double right = i == 1 ? 11 : 8;
                path.moveTo({left, y})
                    .lineTo({right - 3, y})
                    .quadraticTo({right + 1, y}, {right + 1, y - 4})
                    .lineTo({right - 2, y - 4})
                    .quadraticTo({right - 2, y - 3}, {right - 4, y - 3})
                    .lineTo({left, y - 3})
                    .close();
            }
            break;
    }
    return path;
}

void paintIcon(cybercanvas::Canvas& canvas, cybercanvas::Rect bounds, Icon icon, uint32_t ink) {
    const double edge = std::min(bounds.width, bounds.height);
    if (edge <= 0) {
        return;
    }
    canvas.saved([&] {
        canvas.translate(bounds.x + bounds.width / 2, bounds.y + bounds.height / 2);
        canvas.scale(edge / 24, edge / 24);
        auto path = iconPath(canvas, icon);
        const bool dark = ((ink >> 24) & 0xff) < 0x90;
        // Keep the engraved lower lip below one physical pixel at every DPI.
        const double lip = std::min(.85, 24 / (edge * canvas.devicePixelRatio()));
        canvas.saved([&] {
            canvas.translate(0, lip);
            canvas.fill(path, dark ? 0xffedc59a : 0x000000a0);
        });
        canvas.fill(path, ink);
    });
}
}  // namespace mechanicalart
