#pragma once

#include <cstdint>
#include <memory>

#include "LiquidGlass/React.h"

namespace liquidglass {
struct ShadowImage;
}

namespace liquidglass::react {
enum class LightBackgroundBlend { React, Adaptive };

struct EffectGeometry {
    int width = 0, height = 0;
    float sourceX = 0, sourceY = 0, dpiScale = 1, corner = 24;
    bool attachedTop = false;
};

class Effect;

class EffectTarget final {
public:
    EffectTarget();
    ~EffectTarget();
    void* bitmap() const;
    void* texture() const;
    int width() const;
    int height() const;
    uint64_t allocatedBytes() const;
    uint64_t assetReadbackBytes() const;
    std::shared_ptr<const ShadowImage> shadowImage() const;

private:
    friend class Effect;
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

class Effect final {
public:
    Effect();
    ~Effect();
    void initialize(void* d3dDevice, void* d2dContext);
    void render(EffectTarget& target, void* sourceTexture, EffectGeometry geometry, Parameters parameters,
                uint64_t contentRevision, bool exportShadow = true,
                LightBackgroundBlend lightBlend = LightBackgroundBlend::React);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}  // namespace liquidglass::react
