#include "plugins/hourglass-suite/PhysicalSand.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace hourglassplugin {
namespace {
namespace ph = cybercanvas::physics;
using cybercanvas::Point;
constexpr Point center{.9415, 1.4415};

Point rotated(Point p, double angle) {
    return {p.x * std::cos(angle) - p.y * std::sin(angle), p.x * std::sin(angle) + p.y * std::cos(angle)};
}

Point local(Point p, double angle) {
    return rotated({p.x - center.x, p.y - center.y}, -angle);
}
}  // namespace

PhysicalSand::PhysicalSand() {
    reset();
}

void PhysicalSand::reset() {
    world_ = std::make_unique<ph::World>(ph::WorldOptions{.collectEvents = false});
    jar_ = world_->createBody({.type = ph::BodyType::Kinematic, .position = center});
    grains_.clear();
    angle_ = 0;
    cycle_ = 0;
    // Continuous capsules follow the pixel silhouette without stair-step gaps.
    for (int side : {-1, 1}) {
        for (int row = 2; row < 26; ++row) {
            const Point first{side * (chamberHalfWidth(row) + 1) * .1, (row - 14) * .1};
            const Point second{side * (chamberHalfWidth(row + 1) + 1) * .1, (row - 13) * .1};
            world_->capsule(jar_, first, second, .0415, {.friction = .28});
        }
    }
    world_->box(jar_, {.84, .05}, {0, -1.30});
    world_->box(jar_, {.84, .05}, {0, 1.30});
    world_->box(jar_, {.085, .035}, {0, -.02});
    Model initial;
    for (const auto cell : initial.sample().upper) {
        const auto body = world_->createBody({.position = {(cell.x + .415) * .1, (cell.y + .415) * .1},
                                              .linearDamping = .08,
                                              .angularDamping = .4,
                                              .bullet = true});
        world_->circle(body, {}, .040, {.density = 1, .friction = .42, .restitution = .04});
        grains_.push_back({body});
    }
}

void PhysicalSand::advance(double seconds, const Model& timer, bool projectedFlip) {
    if (!std::isfinite(seconds) || seconds <= 0) {
        return;
    }
    seconds = std::min(seconds, .1);
    if (timer.cycles() != cycle_) {
        // A rotating jar can carry the pile with it, so rotating its poses again
        // does not guarantee an upper-chamber refill. Reuse the same grains in the
        // initial upper layout when the timer begins its next metered cycle.
        const auto initial = Model{}.sample().upper;
        size_t index = 0;
        for (auto& grain : grains_) {
            const auto cell = initial[index++];
            const auto position = world_->state(grain.body).position;
            // An out-of-plane flip ends with the actual lower pile mirrored into
            // the upper chamber. Preserve that pile instead of snapping to a grid.
            const Point refill = projectedFlip ? Point{position.x, 2 * center.y - position.y}
                                               : Point{(cell.x + .415) * .1, (cell.y + .415) * .1};
            world_->teleport(grain.body, refill);
            world_->velocity(grain.body, {});
            grain.released = false;
        }
        cycle_ = timer.cycles();
        angle_ = 0;
        world_->teleport(jar_, center);
        world_->velocity(jar_, {});
    }
    const double nextAngle = projectedFlip ? 0 : timer.angle() * std::numbers::pi / 180;
    const size_t target = grains_.size() - timer.sample().upper.size();
    size_t burst = 0;
    while (released() < target) {
        Grain* selected = nullptr;
        double lowest = -100;
        for (auto& grain : grains_) {
            if (grain.released) {
                continue;
            }
            const auto p = local(world_->state(grain.body).position, angle_);
            if (p.y > lowest) {
                selected = &grain;
                lowest = p.y;
            }
        }
        if (!selected) {
            break;
        }
        // Metered valve preserves timer accuracy even when grains jam at the neck.
        const auto position = rotated({0, .15 + .085 * double(burst++)}, angle_);
        world_->teleport(selected->body, {position.x + center.x, position.y + center.y});
        const auto velocity = rotated({.025 * (released() % 2 ? 1 : -1), .35}, angle_);
        world_->velocity(selected->body, velocity);
        selected->released = true;
    }
    const int steps = std::max(1, int(std::ceil(seconds * 60)));
    const double dt = seconds / steps;
    const double start = angle_;
    for (int i = 0; i < steps; ++i) {
        const double previous = start + (nextAngle - start) * i / steps;
        world_->teleport(jar_, center, previous);
        world_->velocity(jar_, {}, (nextAngle - start) / seconds);
        world_->advance(dt);
    }
    world_->teleport(jar_, center, nextAngle);
    world_->velocity(jar_, {});
    angle_ = nextAngle;
    // Box2D's discrete rotating kinematic boundary can sweep through a sleeping
    // grain during a fast flip. Keep an explicit containment guard for that rare
    // case; ordinary grain contacts and piles remain entirely Box2D-driven.
    for (auto& grain : grains_) {
        const auto state = world_->state(grain.body);
        auto p = local(state.position, angle_);
        const auto original = p;
        p.y = std::clamp(p.y, -1.20, 1.20);
        const double row = std::clamp(14 + p.y * 10, 2., 26.);
        const int lower = int(std::floor(row)), upper = std::min(26, lower + 1);
        const double half =
            chamberHalfWidth(lower) + (chamberHalfWidth(upper) - chamberHalfWidth(lower)) * (row - lower);
        const double limit = (half + 1) * .1 - .082;
        p.x = std::clamp(p.x, -limit, limit);
        if (std::hypot(p.x - original.x, p.y - original.y) > .00001) {
            const auto corrected = rotated(p, angle_);
            world_->teleport(grain.body, {corrected.x + center.x, corrected.y + center.y}, state.angle);
            auto velocity = rotated(state.velocity, -angle_);
            if (p.x != original.x) {
                velocity.x = 0;
            }
            if (p.y != original.y) {
                velocity.y = 0;
            }
            world_->velocity(grain.body, rotated(velocity, angle_), state.angularVelocity);
        }
    }
}

std::vector<cybercanvas::Point> PhysicalSand::sample() const {
    std::vector<cybercanvas::Point> result;
    result.reserve(grains_.size());
    for (const auto& grain : grains_) {
        const auto p = local(world_->state(grain.body).position, angle_);
        result.push_back({(p.x + center.x) * 10, (p.y + center.y) * 10});
    }
    return result;
}

size_t PhysicalSand::count() const {
    return grains_.size();
}

size_t PhysicalSand::released() const {
    return std::count_if(grains_.begin(), grains_.end(), [](const auto& grain) {
        return grain.released;
    });
}

cybercanvas::physics::PhysicsStats PhysicalSand::stats() const {
    return world_->stats();
}
}  // namespace hourglassplugin
