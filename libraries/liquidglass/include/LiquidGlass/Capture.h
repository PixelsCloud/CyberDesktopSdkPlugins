#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace liquidglass {
namespace react {
struct Parameters;
}

using Parameters = react::Parameters;

struct Region {
    int x = 0, y = 0, width = 0, height = 0;
    bool operator==(const Region&) const = default;
};

struct CaptureStatistics {
    uint64_t captures = 0, unchanged = 0, failed = 0, uploadedBytes = 0, captureWallMicroseconds = 0;
    uint64_t gpuCopyBytes = 0;
    uint64_t readbackBytes = 0;
    // Generated CSS shadow masks only, cached until geometry changes.
    uint64_t assetReadbackBytes = 0;
    // Timestamp query intervals for our compare/render/copy commands. Excludes
    // later DWM presentation; rejected/disjoint samples never count as zero.
    uint64_t gpuSamples = 0, gpuNanoseconds = 0, gpuDisjointSamples = 0, gpuTimeouts = 0;
    // Allocation estimates for owned region textures and shared frame pools;
    // excludes compositor/driver overhead and is not a hardware VRAM reading.
    uint64_t retainedRegionBytes = 0, sharedFramePoolBytes = 0;
    // CPU wall-clock phases, including waits. These are not GPU execution time.
    uint64_t compareWallMicroseconds = 0, drawWallMicroseconds = 0;
    uint64_t surfaceAcquireWallMicroseconds = 0, surfaceCommitWallMicroseconds = 0;
};

struct ShadowImage;

class CaptureCoordinator;

class CaptureSource final {
public:
    ~CaptureSource();
    void configure(const Parameters& parameters, float dpiScale, float corner, bool attachedTop = false);
    bool synchronize(Region visiblePanel);
    void setActive(bool active);
    void* brush() const;
    std::string error() const;
    std::shared_ptr<const ShadowImage> shadowImage() const;
    // Disabled by default. Reset counters and sample at most 1024 attempts;
    // zero stops measurement. Wall time measures submission, not actual GPU time.
    void sampleStatistics(uint32_t attemptLimit = 128);
    CaptureStatistics statistics() const;

private:
    friend class CaptureCoordinator;
    struct Impl;
    explicit CaptureSource(std::shared_ptr<Impl> state);
    std::shared_ptr<Impl> m_impl;
};

// One background worker owns GPU resources and cropped surface copies. Visible
// consumers share one capture-session frame pool per output; there is no CPU
// desktop readback. Requires capture-session window exclusion on this Windows.
// Exclusions apply to this sampler only and never change screenshot/recording
// visibility of the application's real windows.
class CaptureCoordinator final {
public:
    // Optional worker observer runs immediately before composing a real frame.
    // Diagnostics can throw here to exercise the normal device-failure path.
    explicit CaptureCoordinator(void* compositor, std::function<void()> beforeCompose = {});
    ~CaptureCoordinator();
    std::shared_ptr<CaptureSource> subscribe(std::function<void()> ready);
    void exclude(std::vector<uintptr_t> windows);
    void invalidate();
    // Explicit recovery boundary, for display/device changes or a user retry.
    // Ordinary geometry/exclusion updates do not restart an exhausted cycle.
    void reset();
    void setSuspended(bool suspended);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}  // namespace liquidglass
