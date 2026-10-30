#pragma once
#include <CyberCanvas/Sprites.h>

#include <optional>
#include <tuple>

namespace deep_spectrum {
// Retained nine-slice rods and peak gems. The middle is one texel row;
// stretching it keeps the cap radius and the horizontal metal gradient.
class SpectrumSprites {
public:
    void begin(cybercanvas::Canvas& c, double width, uint32_t edge, uint32_t middle, uint32_t top, double glow,
               bool radial);

    void rod(cybercanvas::Canvas& c, cybercanvas::Rect body);

    void gem(cybercanvas::Canvas& c, double x, double y);

    void flush(cybercanvas::Canvas& c);

private:
    cybercanvas::SpriteAtlas atlas_;
    cybercanvas::SpriteList sprites_;
    std::optional<std::tuple<double, double, uint32_t, uint32_t, uint32_t, double, bool>> key_;
    ID2D1RenderTarget* owner_ = nullptr;
    D2D1_MATRIX_3X2_F base_{};
    double density_ = 1, span_ = 1, padding_ = 0;
    unsigned tile_ = 2, cap_ = 1;
};
}  // namespace deep_spectrum
