#include <CyberCanvas/PhysicsParticles.h>

#include <map>
#include <numbers>

namespace cybercanvas {
namespace {
physics::WorldOptions worldOptions(const PhysicsParticleSettings& settings) {
    const auto valid = [](double value, double low, double high) {
        return std::isfinite(value) && value >= low && value <= high;
    };
    if (settings.particles.space != ParticleSpace::World || settings.particles.maximum > 256 ||
        !valid(settings.pixelsPerMeter, 1, 10000) || !valid(settings.radius, .1, 100) ||
        !valid(settings.density, .001, 10000) || !valid(settings.friction, 0, 1) ||
        !valid(settings.restitution, 0, 1) || !valid(settings.angularVelocity, -36000, 36000) ||
        !valid(settings.damping, 0, 100)) {
        throw std::invalid_argument("Invalid physical particle settings");
    }
    if (static_cast<unsigned>(settings.collider) > 1 || !valid(settings.halfExtent.width, .1, 100) ||
        !valid(settings.halfExtent.height, .1, 100)) {
        throw std::invalid_argument("Invalid particle collision shape");
    }
    physics::WorldOptions options;
    options.gravity = {settings.particles.gravity.x / settings.pixelsPerMeter,
                       settings.particles.gravity.y / settings.pixelsPerMeter};
    options.collectEvents = settings.collisionEvents;
    options.fixedStep = 1. / 120;
    options.maximumSteps = 1;
    options.maximumBodies = settings.particles.maximum + 64;
    options.maximumDynamicBodies = settings.particles.maximum + 32;
    options.maximumShapes = settings.particles.maximum + 128;
    return options;
}
}  // namespace

class PhysicsParticleEmitter::Dynamics final : public ParticleDynamics {
public:
    explicit Dynamics(PhysicsParticleSettings settings)
        : settings_(std::move(settings)), world_(worldOptions(settings_)) {}

    void create(const ParticleMotion& particle) override {
        const double scale = settings_.pixelsPerMeter, radius = settings_.radius / scale;
        const auto body = world_.createBody({.position = {particle.position.x / scale, particle.position.y / scale},
                                             .velocity = {particle.velocity.x / scale, particle.velocity.y / scale},
                                             .angularVelocity = settings_.angularVelocity * std::numbers::pi / 180,
                                             .linearDamping = settings_.damping,
                                             .angularDamping = settings_.damping,
                                             .gravityScale = 0,
                                             .bullet = settings_.continuousCollision,
                                             .sleepingAllowed = settings_.sleepingAllowed});
        try {
            const physics::Material material{.density = settings_.density,
                                             .friction = settings_.friction,
                                             .restitution = settings_.restitution,
                                             .group = settings_.collideWithParticles ? 0 : -1};
            if (settings_.collider == ParticleCollider::Box) {
                world_.box(body, {settings_.halfExtent.width / scale, settings_.halfExtent.height / scale}, {}, 0,
                           material);
            } else {
                world_.circle(body, {}, radius, material);
            }
            bodies_.emplace(particle.id, body);
        } catch (...) {
            world_.destroy(body);
            throw;
        }
    }

    void erase(uint64_t id) noexcept override {
        const auto it = bodies_.find(id);
        if (it != bodies_.end()) {
            if (world_.valid(it->second)) {
                world_.destroy(it->second);
            }
            bodies_.erase(it);
        }
    }

    void advance(std::span<ParticleMotion> particles, std::span<const Point> acceleration, double seconds) override {
        const double scale = settings_.pixelsPerMeter;
        const double radius = settings_.radius / scale;
        const double area = settings_.collider == ParticleCollider::Box
                                ? 4 * settings_.halfExtent.width * settings_.halfExtent.height / (scale * scale)
                                : std::numbers::pi * radius * radius;
        const double mass = area * settings_.density;
        for (size_t i = 0; i < particles.size(); ++i) {
            world_.force(bodies_.at(particles[i].id),
                         {mass * acceleration[i].x / scale, mass * acceleration[i].y / scale});
        }
        world_.advance(seconds);
        for (auto& particle : particles) {
            const auto state = world_.state(bodies_.at(particle.id));
            particle.position = {state.position.x * scale, state.position.y * scale};
            particle.velocity = {state.velocity.x * scale, state.velocity.y * scale};
            // Unwrap Box2D's bounded angle so visual interpolation crosses +/-pi smoothly.
            particle.rotation += std::remainder(state.angle * 180 / std::numbers::pi - particle.rotation, 360.);
        }
    }

    void reset() noexcept override {
        while (!bodies_.empty()) {
            erase(bodies_.begin()->first);
        }
    }

    bool active() const override {
        return world_.active();
    }

    PhysicsParticleSettings settings_;
    physics::World world_;
    std::map<uint64_t, physics::Body> bodies_;
};

PhysicsParticleEmitter::PhysicsParticleEmitter(PhysicsParticleSettings settings)
    : PhysicsParticleEmitter(settings, std::make_unique<Dynamics>(settings)) {}

PhysicsParticleEmitter::PhysicsParticleEmitter(PhysicsParticleSettings settings, std::unique_ptr<Dynamics> dynamics)
    : dynamics_(*dynamics), emitter_(std::move(settings.particles), std::move(dynamics)) {}

PhysicsParticleEmitter::~PhysicsParticleEmitter() = default;

ParticleEmitter& PhysicsParticleEmitter::emitter() {
    return emitter_;
}

const ParticleEmitter& PhysicsParticleEmitter::emitter() const {
    return emitter_;
}

physics::World& PhysicsParticleEmitter::world() {
    return dynamics_.world_;
}

physics::Body PhysicsParticleEmitter::body(uint64_t particleId) const {
    const auto it = dynamics_.bodies_.find(particleId);
    return it == dynamics_.bodies_.end() ? physics::Body{} : it->second;
}
}  // namespace cybercanvas
