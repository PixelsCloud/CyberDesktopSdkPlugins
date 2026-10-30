#include "plugins/pixel-desk/PixelImages.h"

namespace pixeldesk {
bool requestPixelImage(cdsdk::Images& images, const std::string& slot, const std::filesystem::path& path,
                       uint64_t revision, uint32_t actualEdge, double dpi) {
    if (!std::isfinite(dpi) || dpi <= 0) {
        throw std::invalid_argument("Invalid pixel image DPI");
    }
    const auto edge = std::clamp(actualEdge, 1u, 4096u);
    return images.set(slot, path, revision, edge, {}, {}, cybercanvas::ImageSampling::Nearest);
}
}  // namespace pixeldesk
