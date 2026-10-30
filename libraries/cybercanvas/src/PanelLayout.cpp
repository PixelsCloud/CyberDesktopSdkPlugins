#include "CyberCanvas/PanelLayout.h"

namespace cybercanvas {
auto SceneViewport::toScene(Point panel) const -> Point {
    return {(panel.x - origin.x) / scale, (panel.y - origin.y) / scale};
}

auto SceneViewport::toPanel(Point scene) const -> Point {
    return {origin.x + scene.x * scale, origin.y + scene.y * scale};
}

SceneViewport sceneViewport(Rect available, Size scene) {
    detail::layoutRect(available);
    if (!std::isfinite(scene.width) || !std::isfinite(scene.height) || scene.width <= 0 || scene.height <= 0 ||
        available.width <= 0 || available.height <= 0) {
        throw std::invalid_argument("Invalid scene viewport");
    }
    const double scale = std::min(available.width / scene.width, available.height / scene.height);
    const auto bounds = aligned(available, {scene.width * scale, scene.height * scale});
    return {bounds, {bounds.x, bounds.y}, scale};
}

auto PanelLayout::make(Rect area, unsigned columns) -> PanelLayout {
    detail::layoutRect(area);
    if (!columns || area.width <= 0 || area.height <= 0) {
        throw std::invalid_argument("Invalid panel layout");
    }
    const double base = area.width / columns;
    return {area, base,
            base < 90.0    ? PanelDensity::Compact
            : base < 120.0 ? PanelDensity::Standard
                           : PanelDensity::Detailed};
}

auto PanelLayout::detailed() const -> bool {
    return density == PanelDensity::Detailed;
}

auto PanelLayout::content(double padding) const -> Rect {
    return inset(bounds, {padding, padding, padding, padding});
}
}  // namespace cybercanvas
