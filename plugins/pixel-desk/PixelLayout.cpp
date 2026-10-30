#include "plugins/pixel-desk/PixelLayout.h"

namespace pixeldesk {
cybercanvas::Rect fitBody(double width, double height, double bodyWidth, double bodyHeight) {
    if (!std::isfinite(width) || !std::isfinite(height) || !std::isfinite(bodyWidth) || !std::isfinite(bodyHeight) ||
        width <= 2 * kOuterInset || height <= 2 * kOuterInset || bodyWidth <= 0 || bodyHeight <= 0) {
        throw std::invalid_argument("Invalid Pixel Desk body bounds");
    }
    // The image worker can decode to a rounded cache size before D2D samples it
    // again. Reserve one logical pixel for the two reconstruction filter edges;
    // the visible, non-shadow pixels still keep the required six-pixel inset.
    const double inset = kOuterInset + 1;
    const double scale = std::min((width - 2 * inset) / bodyWidth, (height - 2 * inset) / bodyHeight);
    return {(width - bodyWidth * scale) / 2, (height - bodyHeight * scale) / 2, bodyWidth * scale, bodyHeight * scale};
}
}  // namespace pixeldesk
