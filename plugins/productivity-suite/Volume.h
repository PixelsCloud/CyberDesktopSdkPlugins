#pragma once

namespace cdsdk {
struct VolumeSnapshot;
}

namespace productivity {
struct Data;

struct VolumeChange {
    bool changed = false, capacityChanged = false, wasValid = false;
    double used = 0;
};

// Shared data transition for every visual theme. Rendering/motion stay with
// each renderer; capacity, loading and stale-value policy do not get copied.
VolumeChange applyVolume(Data& data, const cdsdk::VolumeSnapshot& value);
}  // namespace productivity
