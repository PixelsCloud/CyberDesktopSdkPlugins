#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace liquidglass {
struct Region;

// Watches compositor dirty rectangles on one output. The acquired GPU frame is
// never copied to the CPU. Identical consumers share one instance per output.
class FrameChanges final {
public:
    FrameChanges(uintptr_t monitor, std::function<void(Region)> changed, std::function<void()> status);
    ~FrameChanges();
    bool ready() const;
    std::string error() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}  // namespace liquidglass
