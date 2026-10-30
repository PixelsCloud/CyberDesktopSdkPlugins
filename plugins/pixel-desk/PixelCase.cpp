#include "plugins/pixel-desk/PixelCase.h"

namespace pixeldesk {
void pixelCase(cybercanvas::Canvas& canvas, const cybercanvas::ImageData& body, cybercanvas::Size source,
               cybercanvas::Rect visible, cybercanvas::Size world, double partHeight) {
    const double scale = partHeight / visible.height, width = visible.width * scale, height = visible.height * scale;
    const std::array<double, 4> fromX{0, width * .2, width * .8, width}, fromY{0, height * .3, height * .7, height};
    const std::array<double, 4> toX{0, fromX[1], fromX[2] + world.width - width, world.width},
        toY{0, fromY[1], fromY[2] + world.height - height, world.height};
    for (unsigned y = 0; y < 3; ++y) {
        for (unsigned x = 0; x < 3; ++x) {
            const double sx = (toX[x + 1] - toX[x]) / (fromX[x + 1] - fromX[x]),
                         sy = (toY[y + 1] - toY[y]) / (fromY[y + 1] - fromY[y]);
            canvas.clipped({toX[x], toY[y], toX[x + 1] - toX[x], toY[y + 1] - toY[y]}, [&] {
                canvas.image(
                    body,
                    {toX[x] - (visible.x * scale + fromX[x]) * sx, toY[y] - (visible.y * scale + fromY[y]) * sy,
                     source.width * scale * sx, source.height * scale * sy},
                    {.fit = cybercanvas::ImageFit::Stretch,
                     .clip = false,
                     .sampling = cybercanvas::ImageSampling::Nearest});
            });
        }
    }
}
}  // namespace pixeldesk
