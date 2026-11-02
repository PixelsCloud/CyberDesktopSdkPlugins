#pragma once

#include <CyberCanvas/Physics2D.h>

#include "plugins/hourglass-suite/Hourglass.h"

namespace hourglassplugin {
struct SandTiming {
    double angle = 0;
    size_t upper = 0;
    uint64_t cycles = 0;
};

class SandBackground;
class SandBackgroundState;

// Timed metering moves one grain through the narrow valve; Box2D simulates
// both chambers, all released grains, collisions, settling and the rotating jar.
class PhysicalSand {
public:
    PhysicalSand();
    explicit PhysicalSand(const CdCreateContext& context);
    ~PhysicalSand();
    void consumeBackground();
    void pauseBackground(bool paused);
    bool backgrounded() const;
    void reset();
    void advance(double seconds, const Model& timer, bool projectedFlip = false);
    std::vector<cybercanvas::Point> sample() const;
    size_t count() const;
    size_t released() const;
    cybercanvas::physics::PhysicsStats stats() const;

private:
    friend class SandBackgroundState;
    void advanceTiming(double seconds, const SandTiming& timer, bool projectedFlip);
    std::unique_ptr<SandBackground> background_;

    struct Grain {
        cybercanvas::physics::Body body;
        bool released = false;
    };

    std::unique_ptr<cybercanvas::physics::World> world_;
    cybercanvas::physics::Body jar_;
    std::vector<Grain> grains_;
    double angle_ = 0;
    uint64_t cycle_ = 0;
};
}  // namespace hourglassplugin
