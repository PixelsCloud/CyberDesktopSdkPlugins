#include "CyberCanvas/Mechanical.h"

#include <CyberCanvas/Canvas.h>

#include <array>
#include <numbers>

namespace cybercanvas::mechanical {
auto Instruments::text(const std::wstring& value, Rect area, double size, uint32_t color, TextAlign align) -> void {
    if (area.width <= 0 || area.height < 8) {
        return;
    }
    auto style = font_;
    style.size = std::max(9., size);
    style.weight = size >= 13 ? FontWeight::SemiBold : FontWeight::Normal;
    style.align = align;
    style.vertical = VerticalAlign::Center;
    style.ellipsis = true;
    style.wrap = false;
    style.tabularFigures = true;
    const auto source = filter_ ? filter_(value) : value;
    if (source.empty()) {
        return;
    }
    const auto ink = c_.measureTextInk(source, style);
    const double fit = std::min({1., area.width / std::max(1., ink.width), area.height / std::max(1., ink.height)});
    if (style.size * fit < 8.5) {
        // A long title can be elided; reducing an entire readout to a few pixels
        // hides the problem and makes all of the remaining text illegible.
        style.size = std::min(style.size, 10.);
        c_.clipped(area, [&] {
            c_.text(source, style, area, color);
        });
        return;
    }
    const auto fitted = c_.fitTextInk(source, style, area);
    c_.clipped(area, [&] {
        c_.ink(color);
        c_.target->DrawTextLayout(D2D1::Point2F(float(fitted.origin.x), float(fitted.origin.y)), fitted.layout.Get(),
                                  c_.brush.Get());
    });
}

auto Instruments::metal(Rect r, double radius, bool brass, bool pressed) -> void {
    const D2D1_GRADIENT_STOP silver[]{{0, color(0xD6DAD5FF)},
                                      {.18f, color(0xB2BAB5FF)},
                                      {.48f, color(0x929C96FF)},
                                      {.55f, color(0xA0A9A2FF)},
                                      {1, color(0x737F79FF)}};
    const D2D1_GRADIENT_STOP gold[]{
        {0, color(0xF3DDA0FF)}, {.3f, color(0xC3A15DFF)}, {.55f, color(0x927039FF)}, {1, color(0xDFC17EFF)}};
    const auto gradient = c_.linearGradient(
        {r.x, r.y}, {r.x, r.y + r.height},
        brass ? std::span<const D2D1_GRADIENT_STOP>(gold) : std::span<const D2D1_GRADIENT_STOP>(silver));
    const auto bounds = D2D1::RoundedRect(
        D2D1::RectF(float(r.x), float(r.y), float(r.x + r.width), float(r.y + r.height)), float(radius), float(radius));
    c_.target->FillRoundedRectangle(bounds, gradient.Get());
    c_.strokeBox(r, radius, pressed ? 0x060A0DFF : 0xE1E5DBFF, {.width = .65});
    if (pressed) {
        c_.box(r.x, r.y, r.width, r.height, radius, 0x00000044);
    }
}

auto Instruments::screw(Point p, double radius) -> void {
    c_.oval(p.x, p.y + .5, radius + .6, radius + .6, 0x000000CC);
    c_.oval(p.x, p.y, radius, radius, kBrass);
    c_.oval(p.x, p.y, radius * .72, radius * .72, 0x6E572FFF, .6);
    c_.line(p.x - radius * .4, p.y + radius * .35, p.x + radius * .4, p.y - radius * .35, 0x332C21FF,
            std::max(.7, radius * .25));
    c_.line(p.x - radius * .4, p.y - radius * .65, p.x + radius * .1, p.y - radius * .7, 0xFFEEBAAA, .5);
}

auto Instruments::window(Rect r) -> void {
    c_.gradientBox(r, 3, 0x05090BFF, 0x202629FF);
    c_.strokeBox(r, 3, 0x62675BFF, {.width = .65});
    c_.line(r.x + 3, r.y + r.height + .5, r.x + r.width - 3, r.y + r.height + .5, 0xFFFFFF25, .6);
}

auto Instruments::lamp(Point p, bool on, uint32_t color) -> void {
    c_.oval(p.x, p.y, 2.4, 2.4, 0x04080AFF);
    c_.oval(p.x, p.y, 1.6, 1.6, on ? color : 0x4C5049FF);
    c_.oval(p.x - .4, p.y - .5, .5, .5, on ? 0xFFFFFFB0 : 0xFFFFFF20);
}

auto Instruments::gauge(Rect bounds, double fraction, const std::wstring& label, const std::wstring& value, bool valid,
                        double maximum, bool ivory) -> void {
    const double r = std::max(1., std::min(bounds.width, bounds.height) * .5 - 1.5);
    const Point center{bounds.x + bounds.width * .5, bounds.y + bounds.height * .5};
    const auto point = [&](double degrees, double length) {
        const double a = degrees * std::numbers::pi / 180;
        return Point{center.x + std::cos(a) * length, center.y + std::sin(a) * length};
    };
    c_.oval(center.x, center.y + 1, r, r, 0x000000AA);
    c_.oval(center.x, center.y, r, r, 0xABB4AFFF);
    c_.oval(center.x, center.y, r - .8, r - .8, 0x363F3FFF);
    c_.oval(center.x, center.y, r - 1.8, r - 1.8, 0xD4D4BEFF, .7);
    const double face = std::max(.5, r - 3);
    const D2D1_GRADIENT_STOP stops[]{{0, color(ivory ? 0xF4E7BDFF : 0x303638FF)},
                                     {1, color(ivory ? 0xC9B985FF : 0x0C1215FF)}};
    const auto gradient =
        c_.radialGradient({center.x - face * .3, center.y - face * .35}, {face * 1.5, face * 1.5}, stops);
    c_.target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(float(center.x), float(center.y)), float(face), float(face)),
                           gradient.Get());
    const auto tickColor = ivory ? 0x423C2FFF : kInk;
    const bool compact = r < 24;
    const int divisions = compact ? 10 : 25;
    for (int i = 0; i <= divisions; ++i) {
        const double a = 140 + 260. * i / divisions;
        const bool major = i % (compact ? 2 : 5) == 0;
        const auto p = point(a, face * .92), q = point(a, face * (major ? .78 : .85));
        const auto tint = !ivory && i >= divisions * .88 ? kRed : tickColor;
        c_.line(p.x, p.y, q.x, q.y, tint, major ? .85 : .5);
        if (major && r >= 48 && maximum > 0 && i > 0 && i < divisions) {
            const auto n = point(a, face * .64);
            text(std::to_wstring(int(std::lround(maximum * i / divisions))), {n.x - r * .17, n.y - 4, r * .34, 8},
                 std::clamp(r * .14, 8., 10.), tickColor);
        }
    }
    const double a = 140 + 260 * (valid && std::isfinite(fraction) ? std::clamp(fraction, 0., 1.) : 0);
    const auto tip = point(a, face * .77), tail = point(a + 180, face * .15);
    c_.line(tail.x + .7, tail.y + 1, tip.x + .7, tip.y + 1, 0x000000AA, std::max(1.5, r * .045));
    c_.line(tail.x, tail.y, tip.x, tip.y, valid ? kBrass : kMuted, std::max(1., r * .035));
    c_.line(center.x, center.y, tip.x, tip.y, 0xFCE8A9FF, .5);
    c_.oval(center.x, center.y, std::max(1.5, r * .085), std::max(1.5, r * .085), 0x8F713DFF);
    c_.oval(center.x - .4, center.y - .4, std::max(1., r * .06), std::max(1., r * .06), kBrass);
    const double valueSize = std::clamp(r * .34, r < 22 ? 11. : 13., 23.);
    const double valueHeight = std::max(11., valueSize * 1.1);
    const double labelHeight = std::clamp(r * .19, 9., 11.);
    const double valueY = center.y + face * .87 - valueHeight;
    const double labelY = valueY - labelHeight - 2;
    if (!value.empty() && face >= 12) {
        const Rect display{center.x - face * .75, labelY - 1, face * 1.5, labelHeight + valueHeight + 4};
        c_.box(display.x, display.y, display.width, display.height, display.height * .5,
               ivory ? 0xD4C38FFF : 0x11191BFF);
    }
    text(label, {center.x - face * .7, labelY, face * 1.4, labelHeight}, labelHeight, ivory ? 0x544A35FF : kMuted);
    text(value, {center.x - face * .8, valueY, face * 1.6, valueHeight}, valueSize, ivory ? 0x302B20FF : kInk);
}

auto Instruments::button(Rect r, bool pressed, bool enabled) -> void {
    c_.box(r.x, r.y + 1, r.width, r.height, std::min(r.width, r.height) * .48, 0x000000BB);
    metal(r, std::min(r.width, r.height) * .48, false, pressed);
    if (!enabled) {
        c_.box(r.x, r.y, r.width, r.height, std::min(r.width, r.height) * .48, 0x10191C88);
    }
}

auto Instruments::rail(Rect r, double fraction) -> void {
    window(r);
    const double x = r.x + 2 + std::clamp(fraction, 0., 1.) * std::max(0., r.width - 4);
    c_.line(r.x + 2, r.y + r.height * .5, x, r.y + r.height * .5, kBrass, 1.5);
    metal({x - 2, r.y - 2, 4, r.height + 4}, 1, true);
}

Instruments::Instruments(Canvas& canvas, TextStyle style, TextFilter filter)
    : c_(canvas), font_(std::move(style)), filter_(filter) {}

auto Instruments::chassis(double width, double height, const std::wstring& title) -> Rect {
    const double edge = std::clamp(std::min(width, height) * .035, 2., 5.);
    const double radius = std::min(8., std::min(width, height) * .08);
    c_.box(1, 2, width - 2, height - 3, radius, 0x00000070);
    metal({1, 1, width - 3, height - 3}, radius);
    c_.box(edge, edge, width - edge * 2 - 1, height - edge * 2 - 1, radius * .65, 0x080C0EFF);
    c_.gradientBox({edge + 1, edge + 1, width - edge * 2 - 3, height - edge * 2 - 3}, radius * .6, 0x292F31FF, kFace);
    // Deliberately sparse machining marks; never an undersized texture upscaled
    // over the complete widget. Each line remains a logical subpixel at any DPI.
    for (double y = 2.5; y < edge; y += 1.2) {
        c_.line(10, y, width - 10, y, 0xFFFFFF22, .35);
        c_.line(10, height - y - 1, width - 10, height - y - 1, 0xFFFFFF18, .35);
    }
    if (width >= 85 && height >= 65) {
        const double screwRadius = std::clamp(std::min(width, height) * .019, 1.8, 3.2);
        for (double x : {edge + 3, width - edge - 4}) {
            for (double y : {edge + 3, height - edge - 4}) {
                screw({x, y}, screwRadius);
            }
        }
    }
    const double heading = title.empty() ? 0 : 24.;
    if (heading) {
        const double plateWidth = std::min(width - 32, std::max(width * .52, 68.));
        const Rect plate{(width - plateWidth) * .5, edge + 2, plateWidth, 18};
        metal(plate, 3);
        text(title, plate, 11, 0x1B2224FF);
    }
    const double padding = std::min(width, height) < 64 ? 2 : 6;
    return {edge + padding, edge + padding * .6 + heading, std::max(1., width - edge * 2 - padding * 2 - 1),
            std::max(1., height - edge * 2 - padding * 1.6 - heading)};
}
}  // namespace cybercanvas::mechanical
