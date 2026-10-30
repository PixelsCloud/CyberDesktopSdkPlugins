#pragma once
#include <memory>
#include <span>
#include <utility>

#include "CyberCanvas/Native.h"
#include "CyberCanvas/Types.h"

namespace cybercanvas {
struct GradientCacheStats {
    size_t targets = 0, stopCollections = 0, brushes = 0;
    uint64_t stopCreates = 0, brushCreates = 0, stopHits = 0, brushHits = 0;
};

// Owner-thread native extension. Returned brushes are read-only shared resources:
// do not change their points, transform or opacity. Full geometry participates in
// the key, so simultaneously held brushes cannot change each other's appearance.
class GradientCache {
public:
    GradientCache();
    static constexpr size_t kTargets = 4, kStopsPerTarget = 32, kBrushesPerTarget = 64, kStopsPerGradient = 32;

    ~GradientCache();

    ComPtr<ID2D1LinearGradientBrush> linear(ID2D1RenderTarget* target, Point start, Point end,
                                            std::span<const D2D1_GRADIENT_STOP> stops);

    ComPtr<ID2D1RadialGradientBrush> radial(ID2D1RenderTarget* target, Point center, Size radii,
                                            std::span<const D2D1_GRADIENT_STOP> stops, Point offset = {});

    void forget(ID2D1RenderTarget* target) noexcept;

    void clear() noexcept;

    GradientCacheStats stats() const;

    GradientCache(const GradientCache&) = delete;

    GradientCache& operator=(const GradientCache&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace cybercanvas
