#pragma once

#include <memory>
#include <numbers>
#include <random>
#include <span>

#include "CyberCanvas/Canvas.h"
#include "CyberCanvas/Motion.h"

namespace cybercanvas {
enum class EmissionShape { Point, Line, Rectangle, Circle };
enum class ParticleSpace { Local, World };

struct ParticleColorKey {
    double fraction;
    uint32_t color;
};

struct ParticleSettings {
    uint32_t maximum = 256, seed = 1;
    double rate = 30, minimumLife = 1, maximumLife = 2;
    double minimumSpeed = 10, maximumSpeed = 30, direction = -90, spread = 45;
    Point gravity{0, 20};
    EmissionShape shape = EmissionShape::Point;
    ParticleSpace space = ParticleSpace::Local;
    Size extent{40, 40};
    Keyframes<double> size{{{0, 3}, {1, 0}}};
    Keyframes<double> opacity{{{0, 1}, {1, 0}}};
    std::vector<ParticleColorKey> colors{{0, 0xffffffff}, {1, 0xffffffff}};
    std::shared_ptr<const ImageData> atlas;
    uint32_t atlasCells = 1;
};

struct ParticleView {
    Point position, velocity;
    double age, lifetime, size;
    uint32_t color;
    uint64_t id = 0;
    double rotation = 0;  // Degrees, clockwise; does not rotate the emitter.
};

struct ParticleMotion {
    uint64_t id;
    Point position, velocity;
    double rotation = 0;
};

// Optional simulation backend. Coordinates/accelerations are DIP and seconds.
// The emitter owns the backend and calls advance once per fixed step for ALL
// live particles. Rendering, emission, lifetime and curves stay in the emitter.
class ParticleDynamics {
public:
    virtual ~ParticleDynamics() = default;
    virtual void create(const ParticleMotion& particle) = 0;
    virtual void erase(uint64_t id) noexcept = 0;
    virtual void advance(std::span<ParticleMotion> particles, std::span<const Point> acceleration, double seconds) = 0;
    virtual void reset() noexcept = 0;

    virtual bool active() const;
};

struct ParticleStats {
    uint64_t steps = 0, emitted = 0, dropped = 0, droppedSteps = 0;
    size_t alive = 0;
};

// Fixed 120 Hz simulation, max 30 catch-up steps. DIP units, deterministic
// seed and explicit elapsed time; no timers. Wind is optional acceleration in
// simulation coordinates. A callback must not mutate this emitter.
class ParticleEmitter {
public:
    explicit ParticleEmitter(ParticleSettings settings = {}, std::unique_ptr<ParticleDynamics> dynamics = {});

    ~ParticleEmitter();

    ParticleEmitter(const ParticleEmitter&) = delete;
    ParticleEmitter& operator=(const ParticleEmitter&) = delete;
    ParticleEmitter(ParticleEmitter&&) = delete;

    void origin(Point value);

    Point origin() const;

    void wind(std::function<Point(Point, double)> value);

    void emitting(bool value);

    void paused(bool value);

    void visible(bool value);

    void reducedMotion(bool value);

    bool active() const;

    void reset(uint32_t seed);

    void burst(uint32_t count);

    void advance(double seconds);

    ParticleStats stats() const;

    std::vector<ParticleView> particles() const;

    void draw(Canvas& canvas) const;

private:
    struct Particle {
        Point position, previous, velocity;
        double age = 0, life = 1;
        uint64_t id = 0;
        double rotation = 0, previousRotation = 0;
    };

    ParticleSettings settings_;
    std::mt19937 random_;
    std::vector<Particle> particles_;
    std::vector<Point> winds_;
    std::vector<ParticleMotion> motions_;
    std::unique_ptr<ParticleDynamics> dynamics_;
    std::function<Point(Point, double)> wind_;
    Point origin_;
    ParticleStats counters_;
    uint64_t nextId_ = 1;
    double accumulator_ = 0, emission_ = 0, elapsed_ = 0;
    static constexpr double step_ = 1. / 120;
    bool emitting_ = true, paused_ = false, visible_ = true, reduced_ = false, stepping_ = false;

    ParticleView sample(const Particle& particle, double alpha) const;

    static void validate(double value, double minimum, double maximum);

    static void point(Point value);

    void guard() const;

    double unit();

    void resetInterpolation();

    uint32_t colorAt(double fraction) const;

    void spawn(uint32_t count);

    void simulate();
};
}  // namespace cybercanvas
