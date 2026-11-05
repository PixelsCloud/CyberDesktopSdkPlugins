#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include "LiquidGlass/Effect.h"

namespace liquidglass {

float opticalExpansion(const Parameters& parameters, float dpiScale);

struct CompositionRegion {
    int x = 0, y = 0, width = 0, height = 0;
    bool operator==(const CompositionRegion&) const = default;
};

struct CompositionMetrics {
    uint64_t sourceCopies = 0, sourceCopyBytes = 0, materialRenders = 0;
    uint64_t sourceAllocatedBytes = 0, materialAllocatedBytes = 0;
    uint64_t assetReadbackBytes = 0;
};

// A render-thread-confined material pass over a compositor-owned GPU texture.
// Does not obtain desktop/window content or schedule frames. The compositor must
// flush its pending drawing before calling render and supply a new revision when
// any sampled background changes, including content outside the surface bounds.
// With updatedBackground, only those already-expanded regions contain fresh
// pre-glass pixels. Other pixels are retained from the previous background, and
// the caller must composite only its matching dirty/visible output geometry.
class CompositionMaterial final {
public:
    CompositionMaterial();
    ~CompositionMaterial();
    void initialize(void* d3dDevice, void* d2dDevice);
    // Copy/render/readback counters are opt-in. Allocation sizes remain budget bookkeeping.
    void setMetricsEnabled(bool enabled);
    void setExternalContour(bool enabled);
    void render(void* backgroundTexture, CompositionRegion surface, EffectGeometry geometry, Parameters parameters,
                uint64_t backgroundRevision, std::span<const CompositionRegion> updatedBackground = {});
    // Borrowed D2D bitmap; valid until the next render or this object's destruction.
    // The caller composites it using its own geometry, clipping and dirty regions.
    void* bitmap() const;
    void* texture() const;
    CompositionRegion sourceRegion() const;
    CompositionMetrics metrics() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace liquidglass
