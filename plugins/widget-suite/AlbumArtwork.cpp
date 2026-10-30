#include "plugins/widget-suite/AlbumArtwork.h"

namespace widgetplugin {
cybercanvas::Rect albumPhotoRect(cybercanvas::Rect bounds, double seconds, bool cover, size_t index) {
    // Tweeny's sine easing and ping-pong sampling keep both ends continuous,
    // including a single photo that remains on screen indefinitely.
    const cybercanvas::Tween<double> drift{0, 1, {12, cybercanvas::Ease::InOutSine, cybercanvas::Repeat::PingPong, 0}};
    const double amount = drift.sample(std::max(0., seconds)).value;
    const double delta = (cover ? .04 : -.02) * amount;
    const double extraWidth = bounds.width * delta, extraHeight = bounds.height * delta;
    const double biasX = index % 2 ? .7 : .3, biasY = index % 3 ? .4 : .6;
    return {bounds.x - extraWidth * biasX, bounds.y - extraHeight * biasY, bounds.width + extraWidth,
            bounds.height + extraHeight};
}

AlbumLayout arrangeAlbum(double width, double height) {
    // Coordinates register the transparent opening and the blank PNG keycaps.
    constexpr double artworkHeight = 992000. / 1586;
    const double margin = std::min(width, height) * .025;
    const auto viewport = cybercanvas::sceneViewport(
        {margin, margin, std::max(1., width - margin * 2), std::max(1., height - margin * 2)}, {1000, artworkHeight});
    const mechanicalart::Stage stage{viewport.scale, viewport.origin};
    AlbumLayout layout;
    layout.stage = stage;
    layout.photo = stage.map({98, 112, 804, 403});
    layout.dock = stage.map({300, 430, 390, 249. * 390 / 1160});
    layout.counter = stage.map({733, 459, 155, 200. * 155 / 554});
    layout.digits = stage.map({760, 474, 101, 27});
    constexpr double centers[]{339, 649, 958};
    for (unsigned i = 0; i < 3; ++i) {
        constexpr double scale = 390. / 1160;
        layout.buttons[i] =
            stage.map({300 + (centers[i] - 73 - 85) * scale, 430 + (414 - 279 - 85) * scale, 170 * scale, 170 * scale});
    }
    return layout;
}

void albumSprite(cybercanvas::Canvas& canvas, const cybercanvas::ImageData& image, cybercanvas::Rect source,
                 cybercanvas::Rect destination) {
    const double scale = destination.width / source.width;
    canvas.clipped(destination, [&] {
        canvas.image(image,
                     {destination.x - source.x * scale, destination.y - source.y * scale, 1983 * scale, 793 * scale},
                     {.fit = cybercanvas::ImageFit::Stretch, .clip = false});
    });
}
}  // namespace widgetplugin
