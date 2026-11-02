#include <algorithm>
#include <cmath>

#include "LiquidGlass/React.h"

namespace liquidglass::react {
namespace {
float bounded(float value, float minimum, float maximum, float fallback) {
    return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
}
}  // namespace

void Parameters::normalize() {
    const Parameters defaults;
    displacementScale = bounded(displacementScale, 0, 200, defaults.displacementScale);
    blurAmount = bounded(blurAmount, 0, 1, defaults.blurAmount);
    saturation = bounded(saturation, 0, 300, defaults.saturation);
    aberrationIntensity = bounded(aberrationIntensity, 0, 20, defaults.aberrationIntensity);
    elasticity = bounded(elasticity, 0, 1, defaults.elasticity);
    mode = RefractionMode(std::clamp(int(mode), 0, 3));
    mouseOffsetX = bounded(mouseOffsetX, -10000, 10000, 0);
    mouseOffsetY = bounded(mouseOffsetY, -10000, 10000, 0);
    hoverGlowOpacity = bounded(hoverGlowOpacity, 0, 1, 0);
    pressGlowOpacity = bounded(pressGlowOpacity, 0, 1, 0);
    interactionGlowOpacity = bounded(interactionGlowOpacity, 0, 1, 0);
    scaleX = bounded(scaleX, .01f, 10, 1);
    scaleY = bounded(scaleY, .01f, 10, 1);
    offsetX = bounded(offsetX, -10000, 10000, 0);
    offsetY = bounded(offsetY, -10000, 10000, 0);
    layoutWidth = bounded(layoutWidth, 0, 16384, 0);
    layoutHeight = bounded(layoutHeight, 0, 16384, 0);
}

float Parameters::blurSigma() const {
    return (overLight ? 12.f : 4.f) + blurAmount * 32.f;
}

Transform interactionTransform(PointerState pointer, Bounds displayed, double layoutWidth, double layoutHeight,
                               double elasticity) {
    Transform result;
    if (pointer.x != 0 && pointer.y != 0) {
        const double dx = pointer.x - (displayed.x + displayed.width / 2);
        const double dy = pointer.y - (displayed.y + displayed.height / 2);
        const double edgeX = std::max(0.0, std::abs(dx) - layoutWidth / 2);
        const double edgeY = std::max(0.0, std::abs(dy) - layoutHeight / 2);
        const double edgeDistance = std::hypot(edgeX, edgeY);
        if (edgeDistance <= 200) {
            const double fade = 1 - edgeDistance / 200;
            result.x = dx * elasticity * .1 * fade;
            result.y = dy * elasticity * .1 * fade;
            const double distance = std::hypot(dx, dy);
            if (distance > 0) {
                const double intensity = std::min(distance / 300, 1.0) * elasticity * fade;
                const double nx = dx / distance, ny = dy / distance;
                result.scaleX = std::max(.8, 1 + std::abs(nx) * intensity * .3 - std::abs(ny) * intensity * .15);
                result.scaleY = std::max(.8, 1 + std::abs(ny) * intensity * .3 - std::abs(nx) * intensity * .15);
            }
        }
    }
    if (pointer.pressed && pointer.interactive) {
        result.scaleX = result.scaleY = .96;
    }
    return result;
}
}  // namespace liquidglass::react
