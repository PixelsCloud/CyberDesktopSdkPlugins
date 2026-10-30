#include "plugins/pixel-desk/PixelBrick.h"

#include <numbers>

#include "plugins/pixel-desk/PixelParts.h"

namespace pixeldesk {
namespace {
void glyph(cybercanvas::Canvas& layer, int digit, uint32_t color, uint32_t edge) {
    auto path = layer.path(cybercanvas::FillRule::Alternate);
    for (const auto& contour : brickclock::glyph(digit)) {
        path.moveTo(contour[0]);
        for (size_t i = 1; i < contour.size(); ++i) {
            path.lineTo(contour[i]);
        }
        path.close();
    }
    // A chipped bright rim outside and a dark upper/left inside edge make this
    // a recess through the masonry, rather than printed ink on a display plate.
    layer.stroke(path, edge, {.width = 2.5});
    layer.fill(path, kInk);
    layer.masked(path, [&] {
        layer.saved([&] {
            layer.translate(3, 3);
            layer.fill(path, color);
        });
    });
}
}  // namespace

bool drawPixelBrick(BasicFace& face, const brickclock::Model& model, const cdsdk::Settings& settings,
                    const CdFrame& frame, CdRect safe) {
    return face.renderContent(frame, safe, [&](cybercanvas::Canvas& layer) {
        // Digits are cut directly into the brick courses. The lower transparent
        // strip is reserved for the masonry fragments, not a separate UI panel.
        constexpr double unit = kPixelBrick.visible.width / brickclock::kWidth;
        constexpr double originX = kPixelBrick.visible.x;
        constexpr double originY = kPixelBrick.visible.y + kPixelBrick.visible.height * .075;
        constexpr double rubbleFloor = kPixelBrick.visible.y + kPixelBrick.visible.height - 12;
        constexpr double extraDrop = (rubbleFloor - originY) / unit - brickclock::kFloor;
        const auto palette = settings.at("palette").text;
        const uint32_t ink = palette == "slate" ? 0x243d43ff : palette == "sand" ? 0x654936ff : 0x243d43ff;
        const uint32_t edge = palette == "slate" ? kMint : palette == "sand" ? kCream : 0xe6a478ff;
        const uint32_t chip = palette == "slate" ? 0x829b9dff : palette == "sand" ? 0xbda77fff : kClay;
        layer.saved([&] {
            layer.translate(originX, originY);
            layer.scale(unit, unit);
            for (size_t i = 0; i < model.digits().size(); ++i) {
                const auto origin = brickclock::digitOrigin(i);
                const auto& carving = model.carvings()[i];
                layer.saved([&] {
                    layer.translate(origin.x, origin.y);
                    layer.scale(brickclock::kGlyphScale, brickclock::kGlyphScale);
                    const double reveal = std::clamp((carving.elapsed - .08) / .24, 0., 1.);
                    if (reveal < 1) {
                        glyph(layer, carving.previous, ink, edge);
                    }
                    layer.clipped({-2, -2, 65, 104 * reveal}, [&] {
                        glyph(layer, model.digits()[i], ink, edge);
                    });
                });
            }
            for (double x : {154 + 152 * brickclock::kGlyphScale, 154 + 332 * brickclock::kGlyphScale}) {
                for (double y : {46 + 28 * brickclock::kGlyphScale, 46 + 62 * brickclock::kGlyphScale}) {
                    layer.box(x - 1, y - 1, 12 * brickclock::kGlyphScale + 2, 12 * brickclock::kGlyphScale + 2, 0,
                              edge);
                    layer.box(x, y, 12 * brickclock::kGlyphScale, 12 * brickclock::kGlyphScale, 0, kInk);
                }
            }
            for (const auto& piece : model.chips()) {
                const auto state = model.state(piece);
                const double age = model.chipAge(piece),
                             opacity = std::clamp((brickclock::kChipLifetime - age) / .6, 0., 1.);
                layer.saved([&] {
                    // Keep each chip registered to its actual source cut at birth, then
                    // extend its path to the ground below the wall. Chip shapes retain
                    // uniform scale and the original Box2D contacts/bounces.
                    const double progress = std::clamp(
                        (state.position.y * 100 - piece.source.y) / std::max(1., brickclock::kFloor - piece.source.y),
                        0., 1.);
                    layer.translate(state.position.x * 100, state.position.y * 100 + extraDrop * progress);
                    layer.rotate(state.angle * 180 / std::numbers::pi);
                    auto path = layer.path();
                    path.moveTo(piece.vertices[0]);
                    for (size_t i = 1; i < piece.vertices.size(); ++i) {
                        path.lineTo(piece.vertices[i]);
                    }
                    path.close();
                    layer.fill(path, (chip & 0xffffff00) | uint32_t(opacity * 255));
                });
            }
        });
    });
}
}  // namespace pixeldesk
