#include "CyberCanvas/FrameSlices.h"

#include <algorithm>

#include "CyberCanvas/Canvas.h"

namespace cybercanvas {
double frameX(double x, double extension, std::span<const double> cuts) {
    if (cuts.empty()) {
        return x;
    }
    double result = x;
    for (const double cut : cuts) {
        result += extension / cuts.size() * std::clamp((x - cut + .5), 0., 1.);
    }
    return result;
}

cybercanvas::Rect frameRegion(cybercanvas::Rect r, double extension, std::span<const double> cuts) {
    const double left = frameX(r.x, extension, cuts);
    return {left, r.y, frameX(r.x + r.width, extension, cuts) - left, r.height};
}

void drawFrame(cybercanvas::Canvas& canvas, ID2D1Bitmap* bitmap, cybercanvas::Size original,
               cybercanvas::Rect destination, std::span<const double> cuts, double sourceScale) {
    const double extension = destination.width - original.width;
    const auto draw = [&](double left, double right) {
        if (right <= left) {
            return;
        }
        const auto source =
            D2D1::RectF(float(left * sourceScale), 0, float(right * sourceScale), float(original.height * sourceScale));
        const auto target = D2D1::RectF(float(destination.x + frameX(left, extension, cuts)), float(destination.y),
                                        float(destination.x + frameX(right, extension, cuts)),
                                        float(destination.y + destination.height));
        canvas.target->DrawBitmap(bitmap, target, 1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, source);
    };
    double left = 0;
    for (const double cut : cuts) {
        draw(left, cut - .5);
        draw(cut - .5, cut + .5);
        left = cut + .5;
    }
    draw(left, original.width);
}
}  // namespace cybercanvas
