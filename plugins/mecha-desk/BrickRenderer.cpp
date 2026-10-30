#include <algorithm>
#include <numbers>

#include "plugins/mecha-desk/BrickClock.h"

namespace aeris_brick {
Renderer::Renderer(const CdCreateContext& context) {
    skin_.bind(context);
}

void Renderer::pause(bool paused) {
    skin_.pause(paused);
}

aerisskin::View Renderer::view(CdRect safe) {
    return skin_.view(safe, {1000, 156});
}

namespace {
void glyph(cybercanvas::Canvas& c, int digit, uint32_t color) {
    auto p = c.path(cybercanvas::FillRule::Alternate);
    for (const auto& contour : brickclock::glyph(digit)) {
        p.moveTo(contour.front());
        for (size_t i = 1; i < contour.size(); ++i) {
            p.lineTo(contour[i]);
        }
        p.close();
    }
    c.stroke(p, 0xe8d7b7ff, {.width = 2.6});
    c.fill(p, 0x284567ff);
    c.masked(p, [&] {
        c.saved([&] {
            c.translate(2, 2);
            c.fill(p, color);
        });
    });
}
}  // namespace

void Renderer::draw(cybercanvas::Canvas& c, const Model& model, const std::string& colors, double) {
    skin_.worldBackdrop(c, false);
    const uint32_t tint = skin_.terminal()    ? 0x00e7efff
                          : colors == "slate" ? 0x24394cff
                          : colors == "sand"  ? 0x6b5639ff
                                              : 0x31546aff;
    c.saved([&] {
        c.translate(0, -40);
        for (size_t i = 0; i < model.digits().size(); ++i) {
            const auto origin = brickclock::digitOrigin(i);
            const auto& carving = model.carvings()[i];
            c.saved([&] {
                c.translate(origin.x, origin.y);
                c.scale(kGlyphScale, kGlyphScale);
                const double reveal = std::clamp((carving.elapsed - .08) / .24, 0., 1.);
                if (reveal < 1) {
                    glyph(c, carving.previous, tint);
                }
                c.clipped({-2, -2, 65, 104 * reveal}, [&] {
                    glyph(c, model.digits()[i], tint);
                });
            });
        }
        for (double x : {154 + 152 * kGlyphScale, 154 + 332 * kGlyphScale}) {
            for (double y : {46 + 28 * kGlyphScale, 46 + 62 * kGlyphScale}) {
                c.oval(x + 6, y + 6, 6, 6, tint);
            }
        }
        for (const auto& chip : model.chips()) {
            const auto state = model.state(chip);
            c.saved([&] {
                c.translate(state.position.x * 100, state.position.y * 100);
                c.rotate(state.angle * 180 / std::numbers::pi);
                auto p = c.path();
                p.moveTo(chip.vertices.front());
                for (size_t i = 1; i < chip.vertices.size(); ++i) {
                    p.lineTo(chip.vertices[i]);
                }
                p.close();
                const double opacity = std::clamp((kChipLifetime - model.chipAge(chip)) / .8, 0., 1.);
                c.fill(p, (tint & 0xffffff00) | uint32_t(opacity * 255));
            });
        }
    });
}
}  // namespace aeris_brick
