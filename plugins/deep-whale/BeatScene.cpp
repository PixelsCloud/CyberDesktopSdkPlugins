#include "plugins/deep-whale/BeatScene.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include "plugins/beat-machine/Machine.h"

namespace deep_motion {
auto BeatScene::resetBeat(Rect area, unsigned count, bool compact) -> void {
    field = area;
    world = std::make_unique<ph::World>(ph::WorldOptions{.gravity = {0, 10.}});
    ground = world->createBody({.type = ph::BodyType::Static});
    balls.clear();
    pendulums.clear();
    time = pulse = 0;
    strokeAge = 10;
    strokeStrength = 0;
    beat = accent = 0;
    const double w = field.width / kUnits, h = field.height / kUnits;
    world->box(ground, {.07, h / 2}, {.07, h / 2}, 0, {.restitution = .65});
    world->box(ground, {.07, h / 2}, {w - .07, h / 2}, 0, {.restitution = .65});
    world->box(ground, {w / 2, .07}, {w / 2, .07}, 0, {.restitution = .65});
    world->box(ground, {w / 2, .04}, {w / 2, h - .04}, 0, {.restitution = .65});
    world->capsule(ground, {.12, h * .5}, {w * .18, h - .07}, .065, {.restitution = .66});
    world->capsule(ground, {w - .12, h * .5}, {w * .82, h - .07}, .065, {.restitution = .66});
    floorY = h - .8;
    floor = world->createBody({.type = ph::BodyType::Kinematic, .position = {w / 2, floorY}});
    // Three pearl-shell lips form the real moving launch surface.
    for (unsigned i = 0; i < 3; ++i) {
        const double x = (double(i) - 1) * w * .22;
        world->capsule(floor, {x - w * .095, 0}, {x + w * .095, 0}, .10, {.friction = .12, .restitution = .72});
    }
    const unsigned cols = compact ? 7 : 10;
    for (unsigned i = 0; i < count; ++i) {
        double r = .16 + (i % 3) * .025;
        auto b =
            world->createBody({.position = {w * .22 + (i % cols) * w * .56 / (cols - 1), h * .40 + (i / cols) * .42},
                               .linearDamping = .045,
                               .bullet = true});
        world->circle(b, {}, r, {.density = .35, .friction = .12, .restitution = .62});
        balls.push_back({b, r, 0, {}, {}, i % 3});
    }
    const unsigned n = compact ? 2 : 3;
    for (unsigned i = 0; i < n; ++i) {
        Point a{w * (n == 2 ? (i == 0 ? .30 : .70) : (.22 + i * .28)),
                h * (n == 2 ? (i == 0 ? .025 : .075) : (i == 1 ? .028 : .065))};
        double l = std::min(h * .46, std::array{2.05, 2.6, 1.8}[i]), r = .35 + (i % 2) * .05;
        auto b = world->createBody({.position = a, .angularDamping = .17});
        world->circle(b, {0, l}, r, {.density = 1.6, .friction = .12, .restitution = .7});
        world->revolute(ground, b, a, {}, {.limit = true, .lowerAngle = -1.03, .upperAngle = 1.03});
        world->teleport(b, a, i % 2 ? .13 : -.16);
        pendulums.push_back({b, r, l, a, {}, i % 3});
    }
}

auto BeatScene::stepBeat(double dt, const beatmachine::Signal& signal) -> void {
    time += dt;
    const bool bass = signal.beat != beat, hit = signal.accent != accent;
    beat = signal.beat;
    accent = signal.accent;
    if (bass) {
        strokeStrength = pulse = std::clamp(signal.strength, .25, 1.);
        strokeAge = 0;
        for (const auto& b : balls) {
            const auto s = world->state(b.body);
            if (s.position.y > field.height / kUnits * .48) {
                // Set a bounded launch speed from the available bowl height. Mass scaling
                // gives all pearl sizes the same jump height and avoids accumulating kicks.
                const double height = field.height / kUnits;
                const double rise = height * (.23 + .23 * pulse) * (.87 + .065 * b.color);
                const double speed = std::sqrt(20 * rise);
                const double mass = .35 * std::numbers::pi * b.radius * b.radius;
                world->impulse(
                    b.body, {mass * std::sin(time * 1.7 + b.color) * .65, mass * std::min(0., -speed - s.velocity.y)});
            }
        }
    }
    if (hit) {
        for (size_t i = 0; i < pendulums.size(); ++i) {
            auto s = world->state(pendulums[i].body);
            world->velocity(
                pendulums[i].body, s.velocity,
                std::clamp(s.angularVelocity + ((accent + i) % 2 ? 1 : -1) * (.25 + signal.middle * .9), -2., 2.));
        }
    }
    // A visible short lift and slower return, rather than a sub-pixel twitch.
    strokeAge += dt;
    pulse = strokeStrength *
            (strokeAge < .09 ? std::sin(strokeAge / .09 * std::numbers::pi / 2) : std::exp(-(strokeAge - .09) / .17));
    if (pulse < .0001) {
        pulse = 0;
    }
    const auto y = world->state(floor).position.y;
    const double error = floorY - pulse * .72 - y;
    world->velocity(floor, {0, std::abs(error) < .0001 ? 0 : std::clamp(error * 24, -4.5, 2.4)});
    world->advance(dt);
}
}  // namespace deep_motion
