#include "plugins/pixel-desk/PixelPanels.h"

namespace pixeldesk {
void panelStrip(cybercanvas::Canvas& canvas, const cybercanvas::ImageData& image, cybercanvas::Rect source,
                double extent, double first, double last, bool vertical) {
    const double original = vertical ? source.height : source.width;
    const double cross = vertical ? source.width : source.height;
    const double end = extent - (original - last);
    const auto part = [&](double from, double to, double length) {
        if (length <= 0) {
            return;
        }
        const cybercanvas::Rect clip =
            vertical ? cybercanvas::Rect{0, to, cross, length} : cybercanvas::Rect{to, 0, length, cross};
        // Adjacent sprite pieces share a hard pixel boundary. Fractional coverage
        // on each separate clip would composite two translucent edges, exposing
        // the desktop through an otherwise opaque rail and through its interior.
        canvas.target->PushAxisAlignedClip(
            D2D1::RectF(float(clip.x), float(clip.y), float(clip.x + clip.width), float(clip.y + clip.height)),
            D2D1_ANTIALIAS_MODE_ALIASED);
        struct ClipScope {
            ID2D1RenderTarget* target;
            ~ClipScope() {
                target->PopAxisAlignedClip();
            }
        } scope{canvas.target.Get()};
        auto target = source;
        (vertical ? target.y : target.x) += to - from;
        canvas.image(
            image, target,
            {.fit = cybercanvas::ImageFit::Stretch, .clip = false, .sampling = cybercanvas::ImageSampling::Nearest});
    };
    part(0, 0, first);
    for (double position = first; position < end; position += 4) {
        part(first, position, std::min(4., end - position));
    }
    part(last, end, original - last);
}
}  // namespace pixeldesk
