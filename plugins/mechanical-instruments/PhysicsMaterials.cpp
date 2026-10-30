#include "plugins/mechanical-instruments/PhysicsMaterials.h"

namespace mechanicalart::physicsmaterials {
Rect sceneBounds(double width, double height) {
    const double margin = std::min(width, height) * .045;
    return {margin, margin, std::max(1., width - margin * 2), std::max(1., height - margin * 2)};
}

Registration registration(std::string_view slot) {
    if (slot == "clock") {
        return {{2172, 724}, {79, 159, 2014, 397}};
    }
    if (slot == "base") {
        return {{2112, 745}, {373, 249, 1367, 353}};
    }
    if (slot == "cap") {
        return {{1983, 793}, {99, 212, 1785, 358}};
    }
    if (slot == "paddle") {
        return {{2172, 724}, {216, 190, 1751, 343}};
    }
    if (slot == "bumper") {
        return {{1254, 1254}, {85, 62, 1081, 1061}};
    }
    if (slot == "launcher") {
        return {{1159, 1358}, {427, 103, 305, 1128}};
    }
    if (slot == "gear") {
        return {{1254, 1254}, {57, 73, 1137, 1097}};
    }
    return {{1254, 1254}, {91, 96, 1071, 1053}};
}

void registeredImage(Canvas& canvas, const cybercanvas::ImageData& image, Registration source, Rect bounds) {
    const double scale = std::min(bounds.width / source.visible.width, bounds.height / source.visible.height);
    const double x = bounds.x + (bounds.width - source.visible.width * scale) / 2;
    const double y = bounds.y + (bounds.height - source.visible.height * scale) / 2;
    canvas.image(image,
                 {x - source.visible.x * scale, y - source.visible.y * scale, source.size.width * scale,
                  source.size.height * scale},
                 {.fit = cybercanvas::ImageFit::Stretch, .clip = false});
}

bool sprite(Canvas& canvas, const cdsdk::Images& images, const char* slot, Rect bounds) {
    const auto image = images.get(slot).image;
    if (!image) {
        return false;
    }
    registeredImage(canvas, *image, registration(slot), bounds);
    return true;
}

void anchored(Canvas& canvas, const cybercanvas::ImageData& image, cybercanvas::Size source, cybercanvas::Point pivot,
              double scale) {
    canvas.image(image, {-pivot.x * scale, -pivot.y * scale, source.width * scale, source.height * scale},
                 {.fit = cybercanvas::ImageFit::Stretch, .clip = false});
}

bool plate(Canvas& canvas, const cdsdk::Images& images, bool clock, bool compact, Rect bounds) {
    const auto image = images.get("chassis").image;
    if (!image) {
        return false;
    }
    const Registration source =
        compact
            ? (clock ? Registration{{1774, 887}, {41, 86, 1693, 722}} : Registration{{1774, 887}, {31, 69, 1712, 737}})
            : (clock ? Registration{{2172, 724}, {17, 19, 2140, 679}} : Registration{{2172, 724}, {44, 23, 2090, 664}});
    const double scale = bounds.width / source.visible.width;
    const double naturalHeight = source.visible.height * scale;
    // Extend only a straight, fastener-free wall section. Corner screws, feet,
    // top/bottom chamfers and all moving parts retain a uniform pixel scale.
    const std::array<double, 4> from{0, naturalHeight * .22, naturalHeight * .27, naturalHeight};
    const double extra = bounds.height - naturalHeight;
    const std::array<double, 4> to{0, from[1], from[2] + extra, bounds.height};
    for (size_t i = 0; i < 3; ++i) {
        const double stretch = (to[i + 1] - to[i]) / (from[i + 1] - from[i]);
        canvas.clipped({bounds.x, bounds.y + to[i], bounds.width, to[i + 1] - to[i]}, [&] {
            canvas.image(
                *image,
                {bounds.x - source.visible.x * scale, bounds.y + to[i] - (source.visible.y * scale + from[i]) * stretch,
                 source.size.width * scale, source.size.height * scale * stretch},
                {.fit = cybercanvas::ImageFit::Stretch, .clip = false});
        });
    }
    return true;
}

void requestPlate(cdsdk::Images& images, const std::filesystem::path& root, bool compact, double physicalWidth) {
    if (root.empty()) {
        return;
    }
    const auto edge = uint32_t(std::clamp(std::ceil(physicalWidth / 128) * 128, 128., 2048.));
    images.set("chassis", root / (compact ? "chassis-4x2.png" : "chassis-6x2.png"), 1, edge);
}
}  // namespace mechanicalart::physicsmaterials
