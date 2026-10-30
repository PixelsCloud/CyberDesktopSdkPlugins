#pragma once

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "CyberCanvas/Canvas.h"
#ifdef CYBERCANVAS_FLUID_BOX2D
#include "CyberCanvas/Physics2D.h"
#endif

namespace cybercanvas {
struct FluidSettings {
    uint32_t maximum = 1024;
    double radius = 2, pixelsPerMeter = 100;
    double density = 1000, viscosity = .02, surfaceTension = .01, adhesion = 0;
    Point gravity{0, 980};
    unsigned maximumSteps = 8;
    unsigned substeps = 4;
};

struct FluidSample {
    Point position, velocity;
};

struct FluidStats {
    uint64_t steps = 0, droppedSteps = 0, revision = 0;
    size_t particles = 0, boundaryPoints = 0;
};

struct FluidSnapshot {
    std::vector<FluidSample> particles;
    FluidStats stats;
    double radius = 0;
    std::string error;
    void draw(Canvas& canvas, uint32_t color = 0x56bce8ff) const;
};

// SI impulse (kg*m/s) and angular impulse about the GLOBAL origin (kg*m*m/s).
// takeReaction consumes accumulated reactions, so applying twice cannot kick twice.
struct FluidReaction {
    Point impulse;
    double moment = 0;
};

// CPU 2D IISPH via Salva. DIP positions/velocities, fixed 120 Hz, bounded work.
// One owning thread, no timer. Use FluidWorker for independent desktop effects.
// Boundary particles should have spacing <= 2*radius, preferably two layers.
// Fluids persist until reset; this is a liquid pool, not a lifetime emitter.
class FluidSystem {
public:
    explicit FluidSystem(FluidSettings settings = {});
    ~FluidSystem();
    FluidSystem(const FluidSystem&) = delete;
    FluidSystem& operator=(const FluidSystem&) = delete;

    void add(std::span<const FluidSample> particles);
    uint64_t boundary(std::span<const FluidSample> samples);
    void updateBoundary(uint64_t id, std::span<const FluidSample> samples);
    void removeBoundary(uint64_t id);
    FluidReaction takeReaction(uint64_t id);
    bool advance(double elapsed);
    void activity(bool visible, bool paused = false, bool reducedMotion = false);
    void reset();
    FluidSnapshot snapshot() const;
    FluidStats stats() const;
    const FluidSettings& settings() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Owns one simulation thread. Commands are FIFO, at most 64 pending commands;
// submit/advance return false on overload. Capture only worker-owned data in a
// command, never Canvas, UI objects or a UI-owned Box2D World. Hidden/pause changes
// discard pending elapsed time and reject stale in-flight snapshot publication.
// Poll snapshot revisions and invalidate only the owning plugin's content area.
class FluidWorker {
public:
    explicit FluidWorker(FluidSettings settings = {});
    ~FluidWorker();
    FluidWorker(const FluidWorker&) = delete;
    FluidWorker& operator=(const FluidWorker&) = delete;
    bool submit(std::function<void(FluidSystem&)> command);
    bool advance(double elapsed);
    void activity(bool visible, bool paused = false, bool reducedMotion = false);
    std::shared_ptr<const FluidSnapshot> snapshot() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#ifdef CYBERCANVAS_FLUID_BOX2D
// Boundary binding on the SAME owning thread as both solvers. Fluid/World must
// outlive this object. For each 1/120s: synchronize(), fluid.advance(), apply(),
// world.advance(). Local samples are DIP relative to the Box2D body origin.
class FluidBodyBinding {
public:
    FluidBodyBinding(FluidSystem& fluid, physics::World& world, physics::Body body,
                     std::span<const Point> localSamples);
    ~FluidBodyBinding();
    FluidBodyBinding(const FluidBodyBinding&) = delete;
    FluidBodyBinding& operator=(const FluidBodyBinding&) = delete;
    void synchronize();
    void apply();

private:
    std::vector<FluidSample> samples() const;
    FluidSystem& fluid_;
    physics::World& world_;
    physics::Body body_;
    std::vector<Point> local_;
    uint64_t boundary_ = 0;
};
#endif
}  // namespace cybercanvas
