#pragma once

#include <memory>

#include "CyberCanvas/Particles.h"
#include "CyberCanvas/Physics2D.h"

namespace cybercanvas {
enum class ParticleCollider { Circle, Box };

struct PhysicsParticleSettings {
    ParticleSettings particles{.maximum = 64, .space = ParticleSpace::World};
    double pixelsPerMeter = 100;
    double radius = 2, density = 1, friction = .3, restitution = .5;
    double angularVelocity = 0, damping = .05;
    ParticleCollider collider = ParticleCollider::Circle;
    Size halfExtent{2, 2};
    bool collideWithParticles = true, continuousCollision = true, sleepingAllowed = true;
    // Particle visuals do not retain collision notifications by default. Set
    // true only when consuming world().takeEvents() after emitter updates.
    bool collisionEvents = false;
};

// Owns one Box2D world and its clock; never advance world() separately. Add
// static/dynamic props through world() in METERS. Particle/visual inputs remain
// DIP. Bodies keep a fixed collision radius as visual lifetime curves fade.
class PhysicsParticleEmitter {
public:
    explicit PhysicsParticleEmitter(PhysicsParticleSettings settings = {});
    ~PhysicsParticleEmitter();
    PhysicsParticleEmitter(const PhysicsParticleEmitter&) = delete;
    PhysicsParticleEmitter& operator=(const PhysicsParticleEmitter&) = delete;
    ParticleEmitter& emitter();
    const ParticleEmitter& emitter() const;
    physics::World& world();
    physics::Body body(uint64_t particleId) const;

private:
    class Dynamics;
    PhysicsParticleEmitter(PhysicsParticleSettings settings, std::unique_ptr<Dynamics> dynamics);
    // Observes the dynamics owned by emitter_, which shares this object's lifetime.
    Dynamics& dynamics_;
    ParticleEmitter emitter_;
};
}  // namespace cybercanvas
