#include "plugins/deep-whale/SpectrumSprites.h"

#include <CyberCanvas/Canvas.h>

#include <algorithm>
#include <cmath>

namespace deep_spectrum {
auto SpectrumSprites::begin(cybercanvas::Canvas& c, double width, uint32_t edge, uint32_t middle, uint32_t top,
                            double glow, bool radial) -> void {
    c.target->GetTransform(&base_);
    density_ = std::hypot(double(base_._11), double(base_._12)) * c.devicePixelRatio();
    const unsigned tile = unsigned(std::ceil((width * 1.8 + (glow > 0 ? 2 : 0)) * density_)) + 2;
    span_ = tile / density_;
    padding_ = (span_ - width) / 2;
    cap_ = std::clamp(unsigned(std::ceil((padding_ + width * .13) * density_)), 1u, tile / 2);
    tile_ = tile;
    const auto key = std::make_tuple(width, density_, edge, middle, top, glow, radial);
    if (!key_ || *key_ != key) {
        atlas_.reset();
        key_ = key;
    }
    atlas_.prepare(c, {tile * 2, tile, 1}, 0, [&](auto& layer) {
        const double pad = padding_ * density_, body = width * density_;
        if (glow > 0) {
            layer.box(pad - density_, pad - density_, body + 2 * density_, body + 2 * density_, density_,
                      (middle & 0xffffff00) | uint32_t(glow * 90));
        }
        const D2D1_GRADIENT_STOP stops[]{
            {0, cybercanvas::color(edge)}, {.48f, cybercanvas::color(middle)}, {1, cybercanvas::color(edge)}};
        const auto brush = layer.linearGradient({pad, pad}, {pad + body, pad}, stops);
        layer.target->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(float(pad), float(pad), float(pad + body), float(pad + body)),
                              float(body * .13), float(body * .13)),
            brush.Get());
        const double cx = tile * 1.5, cy = tile * .5, side = width * (radial ? .8 : .6) * density_;
        auto peak = layer.path();
        peak.moveTo({cx, cy - side}).lineTo({cx + side, cy}).lineTo({cx, cy + side}).lineTo({cx - side, cy}).close();
        layer.fill(peak, top);
        layer.stroke(peak, edge, {.width = .35 * density_});
    });
    sprites_.begin(c);
}

auto SpectrumSprites::rod(cybercanvas::Canvas& c, cybercanvas::Rect body) -> void {
    sprites_.vertical({body.x - padding_, body.y - padding_, span_, body.height + 2 * padding_},
                      D2D1::RectU(0, 0, tile_, tile_), cap_, cap_ / density_, sprites_.pose(c));
}

auto SpectrumSprites::gem(cybercanvas::Canvas& c, double x, double y) -> void {
    sprites_.add({x - span_ / 2, y - span_ / 2, span_, span_}, D2D1::RectU(tile_, 0, tile_ * 2, tile_),
                 sprites_.pose(c));
}

auto SpectrumSprites::flush(cybercanvas::Canvas& c) -> void {
    sprites_.flush(c, atlas_.bitmap());
}
}  // namespace deep_spectrum
