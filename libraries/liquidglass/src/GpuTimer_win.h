#pragma once

#include <cstdint>
#include <memory>

namespace liquidglass::detail {
struct GpuInterval {
    uint64_t nanoseconds = 0;
    bool valid = false, disjoint = false, timedOut = false;
};

// Explicit bounded diagnostics for this process's GPU command interval.
// Excludes the compositor's later presentation work and is not utilization.
class GpuTimer final {
public:
    explicit GpuTimer(void* device);
    ~GpuTimer();
    void begin();
    void seal();
    GpuInterval resolve();
    GpuInterval end();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}  // namespace liquidglass::detail
