#include <CyberCanvas/Physics2D.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace cybercanvas;
using namespace cybercanvas::physics;

void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
    std::cout << "PASS " << message << '\n';
}

template <class Action>
bool rejects(Action action) {
    try {
        action();
        return false;
    } catch (const std::exception&) {
        return true;
    }
}

bool collide(int group, uint64_t mask) {
    World world({.gravity = {}});
    for (double direction : {-1., 1.}) {
        const auto body = world.createBody({.position = {direction * .5, 0}, .velocity = {-direction, 0}});
        world.circle(body, {}, .2, {.mask = mask, .group = group});
    }
    bool contact = false;
    for (int frame = 0; frame < 60; ++frame) {
        world.advance(1. / 60);
        for (const auto& event : world.takeEvents()) {
            contact |= event.kind == EventKind::ContactBegin;
        }
    }
    return contact;
}
}  // namespace

int main() {
    try {
        require(collide(0, ~uint64_t(0)), "ordinary shapes collide");
        require(!collide(0, 0), "mask excludes collision");
        require(collide(7, 0), "positive group overrides mask");
        require(!collide(-7, ~uint64_t(0)), "negative group excludes collision");

        World geometry({.gravity = {}});
        const auto capsuleBody = geometry.createBody({.type = BodyType::Static});
        const auto capsule = geometry.capsule(capsuleBody, {-.5, 0}, {.5, 0}, .2, {.category = 2});
        const auto polygonBody = geometry.createBody({.type = BodyType::Static, .position = {3, 0}});
        const std::array<Point, 3> triangle{{{-.5, -.5}, {.5, -.5}, {0, .5}}};
        const auto polygon = geometry.polygon(polygonBody, triangle, {.category = 4});
        require(geometry.point({0, 0}) == std::vector<Shape>{capsule}, "capsule point query is exact");
        require(geometry.point({3, 0}) == std::vector<Shape>{polygon}, "convex polygon point query is exact");
        require(geometry.point({3.4, .4}).empty(), "point query rejects polygon AABB corner");
        require(geometry.query({-1, -1, 5, 2}, 2) == std::vector<Shape>{capsule}, "AABB query honors category mask");
        const auto ray = geometry.ray({-2, 0}, {6, 0}, 4);
        require(ray && ray->shape == polygon && ray->fraction > .7 && ray->fraction < .9,
                "ray query skips masked capsule and finds polygon");
        const auto boundary = geometry.createBody({.type = BodyType::Static, .position = {0, 5}});
        const std::array<Point, 4> square{{{-2, -2}, {2, -2}, {2, 2}, {-2, 2}}};
        const auto edges = geometry.chain(boundary, square);
        require(edges.size() == 4 && std::all_of(edges.begin(), edges.end(),
                                                 [&](Shape shape) {
                                                     return geometry.valid(shape) && geometry.owner(shape) == boundary;
                                                 }),
                "closed chain exposes four owned valid edges");
        geometry.destroy(boundary);
        require(std::none_of(edges.begin(), edges.end(),
                             [&](Shape shape) {
                                 return geometry.valid(shape);
                             }),
                "destroying boundary invalidates all chain edges");

        World limited({.gravity = {}, .maximumBodies = 2, .maximumDynamicBodies = 1, .maximumJoints = 1});
        const auto anchor = limited.createBody({.type = BodyType::Static});
        const auto body = limited.createBody({.position = {1, 0}});
        const auto shape = limited.circle(body, {}, .1);
        limited.distance(anchor, body, {}, {}, 1);
        require(rejects([&] {
                    limited.mouse(anchor, body, {1, 0});
                }),
                "joint budget rejects extra joint");
        require(rejects([&] {
                    limited.createBody({.type = BodyType::Static});
                }),
                "total body budget rejects extra static body");
        limited.enqueue([=](World& world) {
            world.destroy(body);
        });
        limited.advance(0);
        require(!limited.valid(body) && !limited.valid(shape) && limited.stats().joints == 0,
                "queued deletion releases attached resources before next step");
        for (int i = 0; i < 1024; ++i) {
            limited.enqueue([](World&) {
            });
        }
        require(rejects([&] {
                    limited.enqueue([](World&) {
                    });
                }),
                "command queue has a hard capacity");
        limited.advance(0);
        limited.enqueue([](World&) {
        });
        limited.advance(0);
        require(!limited.active(), "drained queue recovers and does not busy loop");

        World steady({.gravity = {}}), jitter({.gravity = {}});
        const auto a = steady.createBody({.velocity = {1, 2}, .sleepingAllowed = false});
        const auto b = jitter.createBody({.velocity = {1, 2}, .sleepingAllowed = false});
        steady.circle(a, {}, .1);
        jitter.circle(b, {}, .1);
        for (int i = 0; i < 120; ++i) {
            steady.advance(1. / 60);
            jitter.advance(i % 2 ? .025 : 1. / 120);
        }
        require(steady.stats().steps == jitter.stats().steps &&
                    std::abs(steady.state(a).position.x - jitter.state(b).position.x) < 1e-6,
                "jittered scheduling preserves fixed-step trajectory");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
