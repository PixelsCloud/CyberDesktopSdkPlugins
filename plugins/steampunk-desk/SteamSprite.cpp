#include "plugins/steampunk-desk/SteamSprite.h"

namespace steamdesk {
void sprite(cybercanvas::Canvas& canvas, const cybercanvas::ImageData& image, cybercanvas::Size original,
            cybercanvas::Rect source, cybercanvas::Rect destination) {
    const double sx = destination.width / source.width, sy = destination.height / source.height;
    canvas.clipped(destination, [&] {
        canvas.image(
            image,
            {destination.x - source.x * sx, destination.y - source.y * sy, original.width * sx, original.height * sy},
            {.fit = cybercanvas::ImageFit::Stretch, .clip = false});
    });
}
}  // namespace steamdesk
