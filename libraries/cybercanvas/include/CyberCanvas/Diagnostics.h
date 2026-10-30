#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>

#include "CyberCanvas/Types.h"

namespace cybercanvas {
using MonotonicTime = std::chrono::steady_clock::time_point;

uint64_t nanosecondsSince(MonotonicTime start);

struct WorkStats {
    uint64_t count = 0, nanoseconds = 0;
};

// Owner-thread counters. Durations are inclusive, so nested stages must not be
// added as if they were disjoint. ImageStore publishes worker counters safely.
class ScopedDuration {
public:
    explicit ScopedDuration(uint64_t& destination);

    ~ScopedDuration();

    ScopedDuration(const ScopedDuration&) = delete;
    ScopedDuration& operator=(const ScopedDuration&) = delete;

private:
    uint64_t& destination_;
    MonotonicTime start_;
};

struct CanvasStats {
    WorkStats frames, textMeasures, textLayouts, uploads, pixelCopies, legacyDecodes;
    uint64_t textCacheHits = 0, bitmapCacheHits = 0, surfaceRebuilds = 0, cancelledFrames = 0, textDraws = 0;
    uint64_t uploadedBytes = 0, surfaceBytes = 0, resourceBytes = 0, pixelBytes = 0, legacyBitmapBytes = 0,
             legacySourceBytes = 0, trackedBytes = 0, peakTrackedBytes = 0;
    size_t textEntries = 0, bitmapEntries = 0;
    uint64_t transientBytes = 0;
    // Rasterization is EndDraw execution, included in frames.nanoseconds.
    uint64_t rasterNanoseconds = 0, opacityLayers = 0;
    uint64_t spriteBatches = 0, spriteInstances = 0;
};

struct DebugNode {
    uint32_t id = 0, kind = 0;
    Rect bounds, clip, hit;
    bool visible = false, enabled = false, interactive = false, focused = false, layoutDirty = false;
    bool loading = false, failed = false;
    std::wstring name, resource;
    std::string error;
};
}  // namespace cybercanvas
