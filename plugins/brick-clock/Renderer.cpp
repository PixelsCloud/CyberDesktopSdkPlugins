#include <cmath>
#include <numbers>

#include "plugins/brick-clock/Clock.h"

namespace brickclock {
namespace {
using cybercanvas::Canvas;
using cybercanvas::Path;

uint32_t alpha(uint32_t color, double opacity) {
    return (color & 0xffffff00u) | uint32_t(std::clamp(opacity, 0., 1.) * (color & 255u) + .5);
}

}  // namespace

Palette palette(const std::string& name) {
    if (name == "sand") {
        return {0xd1bb91ff, 0xdcc8a2ff, 0xb09a73ff, 0xf1dfb9ff, 0xeee7d4ff,
                0x958260ff, 0x5d543eff, 0xe4cfa5ff, 0xe3c48aff};
    }
    if (name == "slate") {
        return {0x5d7377ff, 0x647d80ff, 0x43575dff, 0x8fa5a4ff, 0xa2b3aaff,
                0x364c53ff, 0x1b3039ff, 0x7c9595ff, 0x91b1b3ff};
    }
    return {0xbf825fff, 0xc98c68ff, 0x9e684eff, 0xe5ad83ff, 0xe4d7baff, 0x865540ff, 0x4d352eff, 0xd9a17cff, 0xd99c65ff};
}

void Renderer::prepare(Canvas& canvas) {
    if (factory_.Get() == canvas.factory.Get()) {
        return;
    }
    factory_ = canvas.factory;
    owner_ = nullptr;
    wall_.reset();
    for (size_t i = 0; i < glyphs_.size(); ++i) {
        auto path = std::make_unique<Path>(factory_.Get());
        for (const auto& contour : glyph(int(i))) {
            path->moveTo(contour.front());
            for (size_t n = 1; n < contour.size(); ++n) {
                path->lineTo(contour[n]);
            }
            path->close();
        }
        path->finish();
        glyphs_[i] = std::move(path);
    }
    chip_ = std::make_unique<Path>(factory_.Get());
    chip_->moveTo({-1, 0}).lineTo({-.35, -1}).lineTo({.8, -.65}).lineTo({1, .45}).lineTo({-.2, .8}).close().finish();
    facet_ = std::make_unique<Path>(factory_.Get());
    facet_->moveTo({-1, 0}).lineTo({-.35, -1}).lineTo({.8, -.65}).lineTo({0, 0}).close().finish();
}

void Renderer::wall(Canvas& canvas, const Palette& colors, double height) {
    const double floor = height - 18;
    Path border(canvas.factory.Get());
    border.moveTo({12, 0})
        .lineTo({988, 0})
        .quadraticTo({1000, 0}, {1000, 12})
        .lineTo({1000, floor})
        .lineTo({0, floor})
        .lineTo({0, 12})
        .quadraticTo({0, 0}, {12, 0})
        .close();
    canvas.masked(border, [&] {
        canvas.box(0, 0, kWidth, floor, 0, colors.mortar);
        const double step = kWidth / 7.2, row = kFloor / 3;
        for (int y = 0; y < int(std::ceil(floor / row)); ++y) {
            for (int x = -1; x < 9; ++x) {
                const double bx = x * step - (y % 2 ? step / 2 : 0) + 2.5, by = y * row + 2.5;
                const double width = step - 5, height = row - 5;
                const bool alternate = (x * 7 + y * 3 + 20) % 3 == 0;
                canvas.box(bx, by, width, height, 3.5, alternate ? colors.alternate : colors.brick);
                canvas.line(bx + 4, by + 2.5, bx + width - 4, by + 2.5, alpha(colors.edge, .7), 1.7);
                canvas.line(bx + 3, by + height - 2, bx + width - 3, by + height - 2, colors.shadow, 2.5);
                if ((x + 9) % 3 == y) {
                    canvas.line(bx + 16, by + height * .6, bx + 25, by + height * .6 - 2.5, alpha(colors.shadow, .55),
                                1.5);
                }
            }
        }
    });
    canvas.box(0, floor, kWidth, 18, 0, colors.shadow);
    canvas.box(0, floor, kWidth, 5, 0, colors.edge);
    for (int i = 1; i < 7; ++i) {
        canvas.line(kWidth * i / 7, floor + 6, kWidth * i / 7, height, colors.mortar, 2.5);
    }
}

void Renderer::number(Canvas& canvas, size_t index, int digit, const Palette& colors, double reveal, double opacity) {
    if (reveal <= 0 || opacity <= 0) {
        return;
    }
    const auto origin = digitOrigin(index);
    auto& path = *glyphs_[size_t(digit)];
    canvas.saved([&] {
        canvas.translate(origin.x, origin.y);
        canvas.scale(kGlyphScale, kGlyphScale);
        canvas.clipped({-2, -2, 64, 104 * std::clamp(reveal, 0., 1.)}, [&] {
            canvas.withOpacity(opacity, [&] {
                canvas.masked(path, [&] {
                    canvas.fill(path, colors.cutShadow);
                    canvas.saved([&] {
                        canvas.translate(3.5, 3.5);
                        canvas.fill(path, colors.cut);
                    });
                    canvas.saved([&] {
                        canvas.translate(-1, -1);
                        canvas.stroke(path, colors.cutEdge, {.width = 2});
                    });
                });
            });
        });
    });
}

void Renderer::draw(Canvas& canvas, const Model& model, const std::string& name, double scale, double panelHeight) {
    prepare(canvas);
    const auto colors = palette(name);
    const auto dpi = canvas.devicePixelRatio();
    const double top = panelHeight - kHeight;
    const double pixelWidth = std::ceil(kWidth * scale * dpi), pixelHeight = std::ceil(panelHeight * scale * dpi);
    // Cache at the actual widget DPI, without enlarging the plugin texture. Oversized layouts draw directly.
    if (pixelWidth >= 1 && pixelHeight >= 1 && pixelWidth <= 4096 && pixelHeight <= 4096 &&
        pixelWidth * pixelHeight <= 2 * 1024 * 1024) {
        if (!wall_.bitmap() || owner_ != canvas.target.Get() || width_ != pixelWidth || height_ != pixelHeight ||
            dpi_ != dpi || scale_ != scale || palette_ != name) {
            canvas.renderLayer(wall_, {uint32_t(pixelWidth), uint32_t(pixelHeight), dpi}, [&](auto& cached) {
                cached.scale(scale, scale);
                wall(cached, colors, panelHeight);
            });
            owner_ = canvas.target.Get();
            width_ = uint32_t(pixelWidth);
            height_ = uint32_t(pixelHeight);
            dpi_ = dpi;
            scale_ = scale;
            palette_ = name;
        }
        canvas.target->DrawBitmap(wall_.bitmap(), D2D1::RectF(0, float(-top), float(kWidth), float(kHeight)), 1,
                                  D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    } else {
        canvas.saved([&] {
            canvas.translate(0, -top);
            wall(canvas, colors, panelHeight);
        });
    }
    for (size_t i = 0; i < model.digits().size(); ++i) {
        const auto& carving = model.carvings()[i];
        if (carving.elapsed < .08) {
            number(canvas, i, carving.previous, colors, 1, 1 - carving.elapsed / .08);
        }
        const double reveal = std::clamp((carving.elapsed - .08) / .24, 0., 1.);
        number(canvas, i, model.digits()[i], colors, reveal);
        if (carving.elapsed < .36) {
            const auto origin = digitOrigin(i);
            const double dust = std::sin(carving.elapsed / .36 * std::numbers::pi);
            const double front = origin.y + reveal * 140;
            canvas.oval(origin.x + 40, front, 40, 13, alpha(colors.mortar, dust * .25));
            canvas.oval(origin.x + 62, front + 9, 22, 10, alpha(colors.edge, dust * .16));
            canvas.line(origin.x + 30, front - 12, origin.x + 38, front - 3, alpha(colors.cutShadow, dust * .7), 1.3);
        }
    }
    // Colons never animate or emit debris.
    for (double x : {154 + 148 * kGlyphScale, 154 + 328 * kGlyphScale}) {
        for (double y : {46 + 28 * kGlyphScale, 46 + 62 * kGlyphScale}) {
            canvas.box(x + 4 * kGlyphScale, y, 12 * kGlyphScale, 12 * kGlyphScale, 0, colors.cutShadow);
            canvas.box(x + 4 * kGlyphScale + 2, y + 2, 12 * kGlyphScale - 2, 12 * kGlyphScale - 2, 0, colors.cut);
            canvas.line(x + 4 * kGlyphScale + 2, y + 12 * kGlyphScale, x + 16 * kGlyphScale, y + 12 * kGlyphScale,
                        colors.cutEdge, 1.5);
        }
    }
    for (const auto& chip : model.chips()) {
        const auto state = model.state(chip);
        const double fade = std::clamp((kChipLifetime - model.chipAge(chip)) / .5, 0., 1.);
        const double radius = -chip.vertices.front().x;
        // A small cast shadow and a constant-width dark rim separate stone from the matching wall.
        canvas.saved([&] {
            canvas.translate(state.position.x * 100 + 3, state.position.y * 100 + 4);
            canvas.rotate(state.angle * 180 / std::numbers::pi);
            canvas.scale(radius, radius);
            canvas.fill(*chip_, alpha(colors.cutShadow, fade * .3));
        });
        canvas.saved([&] {
            canvas.translate(state.position.x * 100, state.position.y * 100);
            canvas.rotate(state.angle * 180 / std::numbers::pi);
            canvas.scale(radius, radius);
            canvas.fill(*chip_, alpha(colors.chip, fade));
            canvas.fill(*facet_, alpha(colors.edge, fade));
            canvas.stroke(*chip_, alpha(colors.cutShadow, fade * .9), {.width = 1.6 / radius});
        });
    }
}
}  // namespace brickclock
