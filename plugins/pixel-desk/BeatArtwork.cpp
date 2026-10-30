#include "plugins/pixel-desk/BeatArtwork.h"

namespace pixeldesk {
void beatPart(cybercanvas::Canvas& layer, const cybercanvas::ImageData& image, unsigned index, cybercanvas::Rect area) {
    const auto crop = kBeatParts.at(index);
    const double scale = std::min(area.width / crop.width, area.height / crop.height);
    const double x = area.x + (area.width - crop.width * scale) / 2;
    const double y = area.y + (area.height - crop.height * scale) / 2;
    layer.clipped({x, y, crop.width * scale, crop.height * scale}, [&] {
        layer.image(
            image,
            {x - crop.x * scale, y - crop.y * scale, kBeatPartsSize.width * scale, kBeatPartsSize.height * scale},
            {.fit = cybercanvas::ImageFit::Stretch, .clip = false, .sampling = cybercanvas::ImageSampling::Nearest});
    });
}
}  // namespace pixeldesk
