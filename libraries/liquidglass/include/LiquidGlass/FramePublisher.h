#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "LiquidGlass/Capture.h"

namespace liquidglass {
// Optional in-process GPU source for application-owned desktop surfaces. No
// window affinity changes, screen readback, database or cross-process access.
class FramePublisher final {
public:
    FramePublisher(uintptr_t window, Region bounds, std::function<void()> requestFrame);
    ~FramePublisher();
    bool subscribed() const;
    void publish(void* device, void* context, void* texture);
    void setUnavailable(std::string reason);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}  // namespace liquidglass
