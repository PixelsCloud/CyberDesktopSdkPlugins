#pragma once

#include <CyberCanvas/Physics2D.h>

#include <memory>
#include <vector>

struct CdCreateContext;

namespace beatmachine {
struct Signal;
}

namespace streetlink {
namespace ph = cybercanvas::physics;
using cybercanvas::Point;
using cybercanvas::Rect;
class PhysicsBackground;
class PhysicsBackgroundState;

// Authored geometry and readonly published particles are also used by the UI.
// Only the explicit reference constructor or the backend owner accesses World.
class PhysicsModel {
public:
    struct Particle {
        ph::Body body;
        unsigned chamber = 0;
        double radius = 0;
    };

    PhysicsModel();
    explicit PhysicsModel(const CdCreateContext& context);
    ~PhysicsModel();
    void reset(unsigned count);
    void spawn(unsigned index, bool preload = false);
    void advancePhysics(double dt, const beatmachine::Signal& signal, bool audioActive, double phase);
    void kick(double strength, double phase);
    bool beginDrag(size_t index, Point target);
    void drag(Point target);
    void cancelDrag();
    void consumeBackground();
    void pauseBackground(bool paused);
    bool backgrounded() const;
    bool active() const;
    ph::BodyState state(ph::Body body) const;
    ph::PhysicsStats stats() const;

    bool clock = false;
    std::vector<Particle> particles;
    std::vector<Rect> chambers;
    std::vector<std::vector<Point>> lanes;

private:
    friend class PhysicsBackground;
    friend class PhysicsBackgroundState;
    Point dragTarget(Point point, size_t index) const;
    void containParticles();
    std::unique_ptr<PhysicsBackground> background_;
    std::unique_ptr<ph::World> world;
    ph::Body ground;
    ph::Joint mouse;
    size_t dragged = 0;
};
}  // namespace streetlink
