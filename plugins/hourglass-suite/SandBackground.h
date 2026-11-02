#pragma once

#include <cstddef>
#include <memory>
#include <vector>

struct CdCreateContext;

namespace cybercanvas {
struct Point;

namespace physics {
struct PhysicsStats;
}
}  // namespace cybercanvas

namespace hourglassplugin {
struct SandTiming;

class SandBackground {
public:
    explicit SandBackground(const CdCreateContext& context);
    ~SandBackground();
    void reset();
    void advance(double seconds, const SandTiming& timer, bool projectedFlip);
    void consume();
    void pause(bool paused);
    std::vector<cybercanvas::Point> sample() const;
    size_t count() const;
    size_t released() const;
    cybercanvas::physics::PhysicsStats stats() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace hourglassplugin
