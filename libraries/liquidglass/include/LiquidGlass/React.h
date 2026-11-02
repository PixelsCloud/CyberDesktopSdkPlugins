#pragma once

namespace liquidglass::react {
enum class RefractionMode { Standard, Polar, Prominent, Shader };

struct Parameters {
    float displacementScale = 70, blurAmount = .0625f, saturation = 140;
    float aberrationIntensity = 2, elasticity = .15f;
    RefractionMode mode = RefractionMode::Standard;
    bool overLight = false;
    // Transient CSS state. Only component settings are persisted.
    float mouseOffsetX = 0, mouseOffsetY = 0;
    float hoverGlowOpacity = 0, pressGlowOpacity = 0, interactionGlowOpacity = 0;
    float scaleX = 1, scaleY = 1, offsetX = 0, offsetY = 0;
    float layoutWidth = 0, layoutHeight = 0;
    void normalize();
    float blurSigma() const;
    bool operator==(const Parameters&) const = default;
};

struct PointerState {
    double x = 0, y = 0;
    bool hovered = false, pressed = false, interactive = true;
};

struct Bounds {
    double x = 0, y = 0, width = 0, height = 0;
};

struct Transform {
    double x = 0, y = 0, scaleX = 1, scaleY = 1;
};

Transform interactionTransform(PointerState pointer, Bounds displayed, double layoutWidth, double layoutHeight,
                               double elasticity);
}  // namespace liquidglass::react
