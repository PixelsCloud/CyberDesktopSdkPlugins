#include <numbers>

#include "plugins/physics-toys/Toys.h"

namespace toys {
namespace {
using cybercanvas::Canvas;
using cybercanvas::Path;

void star(Canvas& c, double radius, uint32_t fill) {
    Path path(c.factory.Get());
    for (int i = 0; i < 10; ++i) {
        const double angle = i * std::numbers::pi / 5 - std::numbers::pi / 2;
        const double r = radius * (i % 2 ? .48 : 1);
        const Point p{std::cos(angle) * r, std::sin(angle) * r};
        if (!i) {
            path.moveTo(p);
        } else {
            path.lineTo(p);
        }
    }
    path.close();
    c.fill(path, fill);
}

void charm(Canvas& c, unsigned art, uint32_t fill, uint32_t ink) {
    if (art == 0) {
        Path moon(c.factory.Get());
        moon.moveTo({8, -23})
            .cubicTo({-26, -33}, {-34, 17}, {-6, 24})
            .cubicTo({13, 31}, {29, 13}, {22, -2})
            .cubicTo({5, 17}, {-11, -5}, {8, -23})
            .close();
        c.fill(moon, fill);
        c.oval(-9, 6, 1.8, 2, ink);
        c.oval(-17, 13, 3, 1.6, 0xffffff40);
    } else if (art == 1) {
        Path bell(c.factory.Get());
        bell.moveTo({-29, 17})
            .cubicTo({-22, 8}, {-27, -24}, {0, -25})
            .cubicTo({27, -24}, {22, 8}, {29, 17})
            .quadraticTo({0, 31}, {-29, 17})
            .close();
        c.fill(bell, fill);
        c.oval(0, 18, 25, 5, 0x583d3838);
        c.oval(0, 28, 6, 7, 0xecc97fff);
        Path shine(c.factory.Get());
        shine.moveTo({-15, -8}, false).quadraticTo({-14, -18}, {-6, -19});
        c.stroke(shine, 0xffffff90, {.width = 3});
    } else if (art == 2) {
        star(c, 25, fill);
        c.oval(-5, -2, 1.5, 2, ink);
        c.oval(5, -2, 1.5, 2, ink);
        c.line(-2, 6, 2, 6, ink, 1);
    } else if (art == 4) {
        c.box(-10, -18, 20, 36, 5, fill);
        c.line(-4, -8, 4, -8, 0x756d5260, 1.5);
        c.line(-4, -2, 2, -2, 0x756d5260, 1.5);
    } else {
        Path leaf(c.factory.Get());
        leaf.moveTo({0, -18}).cubicTo({-20, -8}, {-13, 11}, {0, 18}).cubicTo({13, 10}, {20, -8}, {0, -18}).close();
        c.fill(leaf, fill);
        c.line(0, -10, 0, 11, 0xffffff70, 1);
    }
}
}  // namespace

void draw(cybercanvas::Canvas& c, const Model& model, const Appearance& appearance) {
    const auto area = model.size();
    const bool clock = model.kind() == Kind::Clock;
    const bool compact = clock && area.height < 150;
    const auto ink = appearance.dark ? 0xd8e6dfff : 0x526e63ff;
    const uint32_t pastel[]{0xf0cd83ff, 0xe5a89aff, 0xb0c9d8ff, 0xa9cbb2ff, 0xf0ddbbff, 0xb0d1c1ff};
    const uint32_t ocean[]{0x9bd8d3ff, 0x8fb7d0ff, 0xc8d9ebff, 0xabcdbfff, 0xd8ece5ff, 0x83b8c6ff};
    const auto* colors = appearance.palette == "ocean" ? ocean : pastel;
    const double railY = clock ? (compact ? 8 : 30) : 34;
    if (!clock) {
        c.line(61, 34, 130, 12, ink, 1.4);
        c.line(130, 12, 199, 34, ink, 1.4);
        c.oval(130, 12, 3.5, 3.5, 0xb4cabbff);
    }
    c.box(clock ? 22 : 38, railY + 4, clock ? 316 : 184, clock ? 9 : 12, 5, 0x152d2b18);
    c.box(clock ? 22 : 38, railY, clock ? 316 : 184, clock ? 9 : 12, 5, appearance.dark ? 0x73988bff : 0xb8cdbbff);
    c.line(clock ? 29 : 45, railY + 2, clock ? 331 : 215, railY + 2, 0xffffff58, 1);
    for (size_t i = 0; i < model.pieces().size(); ++i) {
        const auto& piece = model.pieces()[i];
        auto from = model.anchor(piece), to = model.attachment(piece);
        from = {from.x * 100, from.y * 100};
        to = {to.x * 100, to.y * 100};
        if (clock) {
            Point previous = from;
            const double dx = to.x - from.x, dy = to.y - from.y, length = std::max(1., std::hypot(dx, dy));
            for (int n = 1; n <= 12; ++n) {
                const double offset = n == 12 ? 0 : (n % 2 ? 3 : -3);
                const Point next{from.x + dx * n / 12 - dy / length * offset,
                                 from.y + dy * n / 12 + dx / length * offset};
                c.line(previous.x, previous.y, next.x, next.y, ink, 1.3);
                previous = next;
            }
        } else {
            c.line(from.x, from.y, to.x, to.y, ink, 1.2);
        }
        const auto state = model.state(i);
        c.saved([&] {
            c.translate(state.position.x * 100, state.position.y * 100);
            c.rotate(state.angle * 180 / std::numbers::pi);
            if (clock) {
                const double hw = piece.half.width * 100, hh = piece.half.height * 100;
                const uint32_t tint = colors[(i + 3) % 4];
                c.box(-hw + 1, -hh + 5, hw * 2, hh * 2, 12, 0x172d3024);
                c.box(-hw, -hh, hw * 2, hh * 2, 12, tint);
                c.line(-hw + 10, -hh + 5, hw - 10, -hh + 5, 0xffffff70, 2);
                c.oval(0, -hh + 1, 3, 3, 0xffffffb0);
                cybercanvas::TextStyle text;
                text.family = L"Segoe UI";
                text.size = hh * 1.55;
                text.bold = true;
                text.align = cybercanvas::TextAlign::Center;
                text.vertical = cybercanvas::VerticalAlign::Center;
                const auto digits = model.digits().empty() ? L"000000" : model.digits();
                c.text(std::wstring(1, digits[i]), text, {-hw, -hh, hw * 2, hh * 2}, 0x365650ff);
            } else {
                c.saved([&] {
                    c.translate(1, 3);
                    charm(c, piece.art, 0x193e391a, 0x193e391a);
                });
                charm(c, piece.art, colors[piece.art], 0x4b615aff);
            }
        });
    }
    if (clock) {
        const double y = compact ? 55 : 109;
        for (double x : {126., 234.}) {
            c.oval(x, y - 5, 1.7, 1.7, ink);
            c.oval(x, y + 5, 1.7, 1.7, ink);
        }
        if (appearance.twelve) {
            cybercanvas::TextStyle text;
            text.size = compact ? 8 : 10;
            text.align = cybercanvas::TextAlign::Center;
            c.text(model.afternoon() ? L"PM" : L"AM", text, {155, area.height - 15, 50, 12}, ink);
        }
    }
}
}  // namespace toys
