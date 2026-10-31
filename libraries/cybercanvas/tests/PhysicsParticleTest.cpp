#include <CyberCanvas/PhysicsParticles.h>

#include <iostream>

namespace {
bool passed = true;

void check(const char* name, bool value) {
    passed &= value;
    std::cout << (value ? "PASS " : "FAIL ") << name << '\n';
}

template <class F>
bool rejects(F action) {
    try {
        action();
        return false;
    } catch (const std::exception&) {
        return true;
    }
}

cybercanvas::PhysicsParticleSettings settings() {
    cybercanvas::PhysicsParticleSettings result;
    result.particles.rate = 0;
    result.particles.minimumLife = result.particles.maximumLife = 10;
    result.particles.minimumSpeed = result.particles.maximumSpeed = 80;
    result.particles.direction = 90;
    result.particles.spread = 0;
    result.particles.gravity = {0, 500};
    result.restitution = .9;
    result.damping = 0;
    result.angularVelocity = 180;
    return result;
}
}  // namespace

int main() {
    using namespace cybercanvas;
    try {
        std::vector<ParticleView> reference;
        for (int fps : {30, 60, 120, 144}) {
            PhysicsParticleEmitter physical(settings());
            auto& emitter = physical.emitter();
            auto& world = physical.world();
            const auto floor = world.createBody({.type = physics::BodyType::Static, .position = {.5, 1.}});
            world.box(floor, {2., .05}, {}, 0, {.restitution = .9});
            emitter.origin({20, 10});
            emitter.burst(1);
            const auto id = emitter.particles()[0].id;
            bool bounced = false;
            for (int i = 0; i < fps * 2; ++i) {
                emitter.advance(1. / fps);
                bounced |= world.state(physical.body(id)).velocity.y < -1e-3;
            }
            check("real floor contact produces rebound", bounced);
            check("visual particle defaults do not retain collision events", world.takeEvents().empty());
            check("one shared 120Hz clock", emitter.stats().steps == 240 && world.stats().steps == 240);
            check("sprite rotation follows body", std::abs(emitter.particles()[0].rotation) > 20);
            if (reference.empty()) {
                reference = emitter.particles();
            }
            check("contact trajectory independent of display rate",
                  std::hypot(reference[0].position.x - emitter.particles()[0].position.x,
                             reference[0].position.y - emitter.particles()[0].position.y) < 1e-4);
            emitter.reset(1);
            check("reset releases particles and preserves props",
                  world.stats().bodies == 1 && !world.valid(physical.body(id)));
            emitter.burst(1);
            check("reset cannot alias stale particle IDs", emitter.particles()[0].id != id);
            const auto count = emitter.stats().steps;
            emitter.visible(false);
            emitter.advance(60);
            check("hidden physics world does not step", emitter.stats().steps == count);
            emitter.visible(true);
            emitter.paused(true);
            emitter.advance(60);
            emitter.paused(false);
            emitter.reducedMotion(true);
            emitter.advance(60);
            check("pause and reduced motion freeze physics", emitter.stats().steps == count);
        }
        auto options = settings();
        {
            options.collisionEvents = true;
            PhysicsParticleEmitter observed(options);
            const auto floor = observed.world().createBody({.type = physics::BodyType::Static, .position = {.5, 1.}});
            observed.world().box(floor, {2., .05});
            observed.emitter().origin({20, 10});
            observed.emitter().burst(1);
            for (int frame = 0; frame < 240; ++frame) {
                observed.emitter().advance(1. / 120);
            }
            check("particle callers can opt into readable collision events", !observed.world().takeEvents().empty());
            options.collisionEvents = false;
        }
        options.particles.maximum = 4;
        options.particles.minimumLife = options.particles.maximumLife = .1;
        PhysicsParticleEmitter physical(options);
        physical.emitter().burst(9);
        check("rigid particle capacity enforced",
              physical.world().stats().bodies == 4 && physical.emitter().stats().dropped == 5);
        for (int i = 0; i < 30; ++i) {
            physical.emitter().advance(1. / 120);
        }
        check("lifetime releases all bodies and stops",
              physical.world().stats().bodies == 0 && !physical.emitter().active());
        options.particles.gravity = {};
        options.particles.minimumLife = options.particles.maximumLife = 10;
        options.particles.minimumSpeed = options.particles.maximumSpeed = 0;
        PhysicsParticleEmitter wind(options);
        wind.emitter().burst(1);
        wind.emitter().wind([](Point, double) {
            return Point{100, 0};
        });
        for (int i = 0; i < 120; ++i) {
            wind.emitter().advance(1. / 120);
        }
        check("wind acceleration converted to SI once", std::abs(wind.emitter().particles()[0].velocity.x - 100) < .02);
        options.collider = ParticleCollider::Box;
        options.halfExtent = {2, 5};
        PhysicsParticleEmitter debris(options);
        debris.emitter().burst(1);
        debris.emitter().wind([](Point, double) {
            return Point{100, 0};
        });
        for (int n = 0; n < 120; ++n) {
            debris.emitter().advance(1. / 120);
        }
        check("box debris mass preserves acceleration units",
              std::abs(debris.emitter().particles()[0].velocity.x - 100) < .02);
        {
            auto sprite = settings();
            sprite.particles.gravity = {};
            sprite.particles.minimumSpeed = sprite.particles.maximumSpeed = 0;
            sprite.particles.size = Keyframes<double>{{{0, 16}}};
            sprite.particles.opacity = Keyframes<double>{{{0, 1}}};
            sprite.angularVelocity = 90;
            sprite.sleepingAllowed = false;
            std::vector<uint8_t> atlas(16 * 16 * 4);
            for (size_t y = 0; y < 16; ++y) {
                for (size_t x = 0; x < 4; ++x) {
                    const auto pixel = (y * 16 + x) * 4;
                    atlas[pixel + 2] = atlas[pixel + 3] = 255;
                }
            }
            sprite.particles.atlas = ImageData::fromPremultipliedBgra(16, 16, atlas);
            PhysicsParticleEmitter spinning(sprite);
            spinning.emitter().origin({32, 32});
            spinning.emitter().burst(1);
            for (int n = 0; n < 121; ++n) {
                spinning.emitter().advance(1. / 120);
            }
            Canvas canvas;
            for (double dpi : {1., 1.5, 2.}) {
                const auto edge = static_cast<uint32_t>(64 * dpi);
                std::vector<uint8_t> pixels(size_t(edge) * edge * 4);
                canvas.render({edge, edge, edge * 4, pixels.size(), pixels.data(), dpi}, {}, [&](Canvas& draw, Rect) {
                    spinning.emitter().draw(draw);
                });
                const auto top = (size_t(26 * dpi) * edge + size_t(32 * dpi)) * 4;
                const auto left = (size_t(32 * dpi) * edge + size_t(26 * dpi)) * 4;
                check("atlas rotates around particle center at actual DPI",
                      pixels[top] > 240 && pixels[top + 3] > 240 && pixels[left + 3] < 10 && pixels[3] == 0);
            }
        }
        options.particles.space = ParticleSpace::Local;
        check("ambiguous local-space physics rejected", rejects([&] {
                  PhysicsParticleEmitter invalid(options);
              }));
        options = settings();
        options.particles.maximum = 257;
        check("rigid body budget validated", rejects([&] {
                  PhysicsParticleEmitter invalid(options);
              }));
    } catch (const std::exception& error) {
        std::cout << "FAIL exception: " << error.what() << '\n';
        return 1;
    }
    return passed ? 0 : 1;
}
