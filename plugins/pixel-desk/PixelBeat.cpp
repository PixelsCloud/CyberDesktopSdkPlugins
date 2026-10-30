#include "plugins/pixel-desk/PixelBeat.h"

namespace pixeldesk {
cybercanvas::Rect beatCase(bool compact) {
    const auto& spec = kBeatCatalog[compact ? 0 : 1];
    return {compact ? -74. : -93., compact ? -22. : -44., spec.visible.width, spec.visible.height};
}

cybercanvas::SceneViewport beatView(CdRect safe, bool compact) {
    const auto world = beatCase(compact);
    auto bounds = fitBody(safe.width, safe.height, world.width, world.height);
    bounds.x += safe.x;
    bounds.y += safe.y;
    const double scale = bounds.width / world.width;
    return {bounds, {bounds.x - world.x * scale, bounds.y - world.y * scale}, scale};
}
}  // namespace pixeldesk
