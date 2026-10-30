#pragma once

#include <memory>
#include <span>

#include "CyberCanvas/Types.h"

namespace cybercanvas {
class ImageData;
class MemoryBudget;
}  // namespace cybercanvas

namespace cybercanvas {
enum class EffectKind { Blur, Shadow, Glow, InnerShadow };
// Automatic prefers WARP for CPU snapshots with at most 512 * 512 padded
// physical pixels. Larger surfaces prefer WARP on UMA and hardware otherwise.
// Crossing the area threshold recreates native resources; published snapshots
// remain valid. Creation failure tries the other backend. Explicit requests
// retain their backend regardless of surface size.
enum class EffectBackend { Automatic, Hardware, Warp };

struct EffectStyle {
    EffectKind kind = EffectKind::Shadow;
    double sigma = 4;
    Point offset{0, 2};
    uint32_t color = 0x00000080;  // Straight RGBA, consistent with Canvas colors.
    bool includeSource = true;    // Shadow/glow can also produce just the effect layer.
};

struct EffectResult {
    std::shared_ptr<const ImageData> image;
    Point offset;  // Logical origin relative to the uneffected input.
    double devicePixelRatio = 1;
};

struct EffectStats {
    uint64_t renders = 0, cacheHits = 0, deviceRebuilds = 0, explicitSurfaceBytes = 0;
    EffectBackend backend = EffectBackend::Automatic;
};

// Input and output are immutable premultiplied BGRA. Effects are applied in
// order, with 3*sigma padding per stage. This is content blur, not desktop blur.
// Owner-thread only; reset() discards GPU resources without invalidating copies
// of published CPU results. Driver-managed internal memory is not in stats.
class EffectSurface {
public:
    explicit EffectSurface(EffectBackend backend = EffectBackend::Automatic, uint64_t maximumBytes = 16 * 1024 * 1024,
                           std::shared_ptr<MemoryBudget> sharedBudget = {});
    ~EffectSurface();
    EffectSurface(const EffectSurface&) = delete;
    EffectSurface& operator=(const EffectSurface&) = delete;
    EffectResult render(const ImageData& input, std::span<const EffectStyle> effects, double devicePixelRatio = 1);
    EffectStats stats() const;
    void reset();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace cybercanvas
