#include <numbers>

#include "plugins/mechanical-desk/Brick.h"

namespace mechanicaldesk {
const BasicSpec kMechanicalBrick = basicFolder("brick-4x1");

namespace {
void glyph(cybercanvas::Canvas& canvas, int digit, double reveal = 1) {
    auto path = canvas.path(cybercanvas::FillRule::Alternate);
    for (const auto& contour : brickclock::glyph(digit)) {
        path.moveTo(contour[0]);
        for (size_t i = 1; i < contour.size(); ++i) {
            path.lineTo(contour[i]);
        }
        path.close();
    }
    canvas.clipped({-3, -3, 70, 110 * reveal}, [&] {
        canvas.stroke(path, 0xc9ccbdff, {.width = 2.1});
        canvas.fill(path, 0x04080bff);
        canvas.masked(path, [&] {
            canvas.saved([&] {
                canvas.translate(1.8, 2.0);
                canvas.fill(path, 0x909c9bff);
            });
        });
    });
}
}  // namespace

bool drawMechanicalBrick(BasicFace& face, const brickclock::Model& model, const cdsdk::Settings&, const CdFrame& frame,
                         CdRect safe) {
    return face.renderContent(frame, safe, [&](cybercanvas::Canvas& canvas) {
        const auto r = kMechanicalBrick.readouts[0];
        const double s = std::min(r.width / 730, r.height / 146);
        const double x = r.x + (r.width - 730 * s) / 2 - 154 * s;
        const double y = r.y + (r.height - 146 * s) / 2 - 46 * s;
        canvas.saved([&] {
            canvas.translate(x, y);
            canvas.scale(s, s);
            for (size_t i = 0; i < model.digits().size(); ++i) {
                const auto origin = brickclock::digitOrigin(i);
                const auto carving = model.carvings()[i];
                const double reveal = std::clamp((carving.elapsed - .08) / .24, 0., 1.);
                canvas.saved([&] {
                    canvas.translate(origin.x, origin.y);
                    canvas.scale(brickclock::kGlyphScale, brickclock::kGlyphScale);
                    if (reveal < 1) {
                        glyph(canvas, carving.previous);
                    }
                    glyph(canvas, model.digits()[i], reveal);
                });
            }
            for (double a : {154 + 152 * brickclock::kGlyphScale, 154 + 332 * brickclock::kGlyphScale}) {
                canvas.oval(a, 90, 4, 4, 0xb9c0b5ff);
                canvas.oval(a, 137, 4, 4, 0xb9c0b5ff);
            }
            for (const auto& chip : model.chips()) {
                const auto state = model.state(chip);
                const double opacity = std::clamp((brickclock::kChipLifetime - model.chipAge(chip)) / .6, 0., 1.);
                canvas.saved([&] {
                    canvas.translate(state.position.x * 100, state.position.y * 100);
                    canvas.rotate(state.angle * 180 / std::numbers::pi);
                    auto path = canvas.path();
                    path.moveTo(chip.vertices[0]);
                    for (size_t i = 1; i < chip.vertices.size(); ++i) {
                        path.lineTo(chip.vertices[i]);
                    }
                    path.close();
                    canvas.fill(path, 0xb7bebc00 | uint32_t(opacity * 255));
                });
            }
        });
    });
}
}  // namespace mechanicaldesk
