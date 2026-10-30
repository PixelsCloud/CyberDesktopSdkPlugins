#include "plugins/pixel-desk/PixelChime.h"

namespace pixeldesk {
cybercanvas::SceneViewport chimeView(CdRect safe, double height) {
    const bool tall = height > 500;
    const cybercanvas::Rect world =
        tall ? cybercanvas::Rect{-25, -15, 363, 635} : cybercanvas::Rect{-40, -32, 380, height + 52};
    auto bounds = fitBody(safe.width - (tall ? 12 : 0), safe.height, world.width, world.height);
    bounds.x += tall ? 6 : 0;
    bounds.x += safe.x;
    bounds.y += safe.y;
    const double scale = bounds.width / world.width;
    return {bounds, {bounds.x - world.x * scale, bounds.y - world.y * scale}, scale};
}

cybercanvas::Point chimeDragTarget(cybercanvas::Point target, unsigned index, double height) {
    const unsigned group = std::min(index / 2, 2u);
    const bool tall = height > 500;
    const auto lengths = tall ? std::array{.90, 1.65, 1.05} : std::array{.85, .65, .95};
    const double anchor = tall ? std::array{.63, 1.64, 2.58}[group] : .35 + group * .95;
    const double top = tall ? std::array{1.34, 1.20, 1.30}[group] : .70;
    const double half = tall ? .52 : .48;
    const double tail = tall ? (group == 1 ? .45 : .40) : .35;
    const double cord = tall ? std::array{.85, .85, .78}[group] : .65;
    const double rest = top + lengths[group] + half + (index % 2 ? half + cord + tail : 0);
    const double bottom = height / 100 - (index % 2 ? .4 : 1.83);
    return {std::clamp(target.x, std::max(.4, anchor - .35), std::min(tall ? 2.73 : 2.35, anchor + .35)),
            std::clamp(target.y, rest - .35, std::min(bottom, rest + .35))};
}

cybercanvas::Size chimePieceSize(unsigned index, double height) {
    if (height > 500) {
        if (index % 2) {
            return {index == 3 ? 52. : 40., index == 3 ? 96. : 88.};
        }
        return {index == 2 ? 116. : 110., index == 2 ? 134. : 128.};
    }
    return {index % 2 ? 40. : 110., index % 2 ? 80. : 120.};
}

ChimeInstance::ChimeInstance(const CdCreateContext& context) : toys::Instance(context, toys::Kind::Chime, true) {}

ChimeInstance::~ChimeInstance() {
    // The production instance owns its Canvas after its renderer. Release this
    // pixel face's custom textures while that Canvas and WIC storage are alive.
    setPaused(true);
}
}  // namespace pixeldesk
