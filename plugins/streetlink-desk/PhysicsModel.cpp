#include "plugins/streetlink-desk/PhysicsModel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

#include "plugins/beat-machine/Model.h"
#include "plugins/streetlink-desk/PhysicsBackground.h"
#include "sdk/cpp/Background.h"

namespace streetlink {
namespace {
constexpr double kUnits = 20;
}

PhysicsModel::PhysicsModel() = default;

PhysicsModel::PhysicsModel(const CdCreateContext& context) {
    if (!cdsdk::ownerThreadPhysicsReference()) {
        background_ = std::make_unique<PhysicsBackground>(context);
    }
}

bool PhysicsModel::backgrounded() const {
    return bool(background_);
}

PhysicsModel::~PhysicsModel() = default;

Point PhysicsModel::dragTarget(Point point, size_t index) const {
    const auto& particle = particles.at(index);
    point.x *= kUnits;
    point.y *= kUnits;
    if (!lanes.empty()) {
        const auto& lane = lanes.at(particle.chamber);
        point.x = std::clamp(point.x, lane.front().x + 2, lane.back().x - 2);
        for (size_t i = 1; i < lane.size(); ++i) {
            if (point.x <= lane[i].x) {
                const double t = (point.x - lane[i - 1].x) / (lane[i].x - lane[i - 1].x);
                point.y = lane[i - 1].y + t * (lane[i].y - lane[i - 1].y);
                break;
            }
        }
    } else {
        const auto r = chambers.at(particle.chamber);
        const double rx = r.width / 2 - particle.radius - 1;
        const double ry = r.height / 2 - particle.radius - 1;
        const double x = (point.x - r.x - r.width / 2) / rx;
        const double y = (point.y - r.y - r.height / 2) / ry;
        const double scale = std::max(1., std::hypot(x, y));
        point = {r.x + r.width / 2 + x * rx / scale, r.y + r.height / 2 + y * ry / scale};
    }
    return {point.x / kUnits, point.y / kUnits};
}

void PhysicsModel::spawn(unsigned index, bool preload) {
    if (background_) {
        background_->spawn(index, preload);
        return;
    }
    if (particles.size() >= 72) {
        return;
    }
    const unsigned channel = lanes.empty() ? index % chambers.size() : index % lanes.size();
    Point p;
    const double radius = lanes.empty() ? 1.25 : .95;
    if (!lanes.empty()) {
        p = lanes[channel].front();
        p.x += 1.5;
        if (preload) {
            const auto& lane = lanes[channel];
            const double x = lane.front().x + (lane.back().x - lane.front().x - 4) * (index / lanes.size() + 1) / 21.;
            for (size_t j = 1; j < lane.size(); ++j) {
                if (x <= lane[j].x) {
                    const double t = (x - lane[j - 1].x) / (lane[j].x - lane[j - 1].x);
                    p = {x, lane[j - 1].y + t * (lane[j].y - lane[j - 1].y)};
                    break;
                }
            }
        }
    } else {
        const auto r = chambers[channel];
        const double a = index * 2.399963229728653;
        // Keep the complete particle inside the wall. A .51-radius seed placed
        // its centre outside the chamber and the first beat ejected it outward.
        const double ring = .12 + .10 * (index % 3);
        p = {r.x + r.width / 2 + std::cos(a) * r.width * ring, r.y + r.height / 2 + std::sin(a) * r.height * ring};
    }
    const auto body = world->createBody(
        {.position = {p.x / kUnits, p.y / kUnits}, .linearDamping = clock ? .7 : 1.1, .bullet = true});
    world->circle(body, {}, radius / kUnits, {.density = 1, .friction = .05, .restitution = .82});
    particles.push_back({body, channel, radius});
}

void PhysicsModel::reset(unsigned count) {
    if (background_) {
        background_->reset(*this, count);
        particles.clear();
        return;
    }
    cancelDrag();
    particles.clear();
    world = std::make_unique<ph::World>(ph::WorldOptions{.gravity = {0, 0}, .substeps = 2, .collectEvents = false});
    ground = world->createBody({.type = ph::BodyType::Static});
    for (const auto r : chambers) {
        for (unsigned i = 0; i < 64; ++i) {
            const auto point = [&](unsigned n) {
                const double a = n * 2 * std::numbers::pi / 64;
                return Point{(r.x + r.width / 2 + std::cos(a) * r.width / 2) / kUnits,
                             (r.y + r.height / 2 + std::sin(a) * r.height / 2) / kUnits};
            };
            world->capsule(ground, point(i), point(i + 1), .04, {.restitution = .8});
        }
    }
    for (const auto& lane : lanes) {
        // Audio impulses can reverse a particle. Close both ends so it cannot
        // leave the channel before the next simulation step wraps it around.
        for (const auto end : {lane.front(), lane.back()}) {
            world->capsule(ground, {end.x / kUnits, (end.y - 2) / kUnits}, {end.x / kUnits, (end.y + 2) / kUnits}, .025,
                           {.restitution = .3});
        }
        for (size_t i = 1; i < lane.size(); ++i) {
            for (double side : {-1., 1.}) {
                const Point a{lane[i - 1].x / kUnits, (lane[i - 1].y + side * 2) / kUnits};
                const Point b{lane[i].x / kUnits, (lane[i].y + side * 2) / kUnits};
                world->capsule(ground, a, b, .025, {.restitution = .3});
            }
        }
    }
    if (clock) {
        for (unsigned i = 0; i < std::min(count, 59u); ++i) {
            spawn(i, true);
        }
    } else {
        for (unsigned i = 0; i < std::clamp(count, 12u, 48u); ++i) {
            spawn(i);
        }
    }
}

void PhysicsModel::containParticles() {
    for (const auto& p : particles) {
        const auto state = world->state(p.body);
        Point position{state.position.x * kUnits, state.position.y * kUnits};
        Point normal{};
        bool corrected = false;
        if (lanes.empty()) {
            const auto r = chambers[p.chamber];
            const Point center{r.x + r.width / 2, r.y + r.height / 2};
            const double rx = r.width / 2 - p.radius - 1, ry = r.height / 2 - p.radius - 1;
            const double x = (position.x - center.x) / rx, y = (position.y - center.y) / ry;
            const double distance = std::hypot(x, y);
            if (distance > 1) {
                position = {center.x + x * rx / distance, center.y + y * ry / distance};
                normal = {x / rx, y / ry};
                corrected = true;
            }
        } else {
            const auto& lane = lanes[p.chamber];
            Point closest{};
            double nearest = std::numeric_limits<double>::max();
            for (size_t i = 1; i < lane.size(); ++i) {
                const Point a = lane[i - 1], b = lane[i];
                const double dx = b.x - a.x, dy = b.y - a.y;
                const double t =
                    std::clamp(((position.x - a.x) * dx + (position.y - a.y) * dy) / (dx * dx + dy * dy), 0., 1.);
                const Point point{a.x + t * dx, a.y + t * dy};
                const double distance = std::hypot(position.x - point.x, position.y - point.y);
                if (distance < nearest) {
                    nearest = distance;
                    closest = point;
                }
            }
            const double clearance = std::max(.1, 2 - p.radius - .5);
            if (nearest > clearance) {
                normal = {position.x - closest.x, position.y - closest.y};
                position = {closest.x + normal.x * clearance / nearest, closest.y + normal.y * clearance / nearest};
                corrected = true;
            }
        }
        Point velocity = state.velocity;
        if (corrected) {
            const double length = std::hypot(normal.x, normal.y);
            normal = {normal.x / length, normal.y / length};
            const double outward = velocity.x * normal.x + velocity.y * normal.y;
            if (outward > 0) {
                velocity = {velocity.x - normal.x * outward * 1.65, velocity.y - normal.y * outward * 1.65};
            }
            world->teleport(p.body, {position.x / kUnits, position.y / kUnits});
        }
        const double speed = std::hypot(velocity.x, velocity.y);
        if (speed > 4) {
            velocity = {velocity.x * 4 / speed, velocity.y * 4 / speed};
        }
        if (corrected || speed > 4) {
            world->velocity(p.body, velocity);
        }
    }
}

void PhysicsModel::kick(double strength, double phase) {
    if (background_) {
        background_->kick(strength, phase);
        return;
    }
    for (size_t i = 0; i < particles.size(); ++i) {
        const auto& p = particles[i];
        const double angle = i * 2.399963 + phase;
        const double mass = std::numbers::pi * std::pow(p.radius / kUnits, 2);
        world->impulse(p.body, {std::cos(angle) * mass * strength, std::sin(angle) * mass * strength});
    }
}

void PhysicsModel::advancePhysics(double dt, const beatmachine::Signal& signal, bool audioActive, double phase) {
    if (background_) {
        background_->advance(dt, signal, audioActive, phase);
        return;
    }
    dt = std::clamp(dt, 0., .1);
    for (const auto& p : particles) {
        const auto state = world->state(p.body);
        if (!lanes.empty()) {
            const auto& lane = lanes[p.chamber];
            if (state.position.x * kUnits > lane.back().x - 2) {
                world->teleport(p.body, {(lane.front().x + 2) / kUnits, lane.front().y / kUnits});
            }
            const double mass = std::numbers::pi * std::pow(p.radius / kUnits, 2);
            const double drive = 1.3 + signal.bass * 2.5;
            const double blend = 1 - std::exp(-dt / .18);
            world->impulse(p.body, {mass * (drive - state.velocity.x) * blend, mass * (.1 - state.velocity.y) * blend});
        } else if (audioActive) {
            const auto r = chambers[p.chamber];
            const double x = state.position.x - (r.x + r.width / 2) / kUnits;
            const double y = state.position.y - (r.y + r.height / 2) / kUnits;
            const double mass = std::numbers::pi * std::pow(p.radius / kUnits, 2);
            const double radius = std::max(.01, std::hypot(x, y));
            const double drive = .5 + signal.bass * 1.5 + signal.middle * .6;
            const double motionPhase = phase * (1.2 + signal.high * 3) + p.chamber * std::numbers::pi;
            // Follow a bounded velocity field. A positional tangential force keeps
            // injecting energy at the rim and eventually overwhelms tiny colliders.
            const Point target{-y / radius * drive - x * .8 + std::cos(motionPhase) * signal.bass * .6,
                               x / radius * drive - y * .8 + std::sin(motionPhase) * signal.bass * .6};
            const double blend = 1 - std::exp(-dt / .18);
            world->impulse(
                p.body, {mass * (target.x - state.velocity.x) * blend, mass * (target.y - state.velocity.y) * blend});
        }
    }
    world->advance(dt);
    containParticles();
}

bool PhysicsModel::beginDrag(size_t index, Point target) {
    if (background_) {
        return background_->beginDrag(index, target);
    }
    cancelDrag();
    if (index >= particles.size()) {
        return false;
    }
    dragged = index;
    mouse = world->mouse(ground, particles[index].body, dragTarget(target, index), 10);
    return mouse.id != 0;
}

void PhysicsModel::drag(Point target) {
    if (background_) {
        background_->drag(target);
        return;
    }
    if (mouse.id) {
        world->mouseTarget(mouse, dragTarget(target, dragged));
    }
}

void PhysicsModel::cancelDrag() {
    if (background_) {
        background_->cancelDrag();
        return;
    }
    if (world && mouse.id) {
        world->destroy(mouse);
    }
    mouse = {};
}

void PhysicsModel::consumeBackground() {
    if (background_) {
        background_->consume(*this);
    }
}

void PhysicsModel::pauseBackground(bool paused) {
    if (background_) {
        background_->pause(paused);
    }
}

bool PhysicsModel::active() const {
    return background_ ? background_->active() : world && (world->active() || mouse.id);
}

ph::BodyState PhysicsModel::state(ph::Body body) const {
    return background_ ? background_->state(body) : world->state(body);
}

ph::PhysicsStats PhysicsModel::stats() const {
    return background_ ? background_->stats() : world->stats();
}
}  // namespace streetlink
