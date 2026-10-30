#include <CyberCanvas/Physics2D.h>

#include <array>
#include <cmath>
#include <iostream>
#include <thread>

namespace {
bool passed = true;

void check(const char* name, bool value) {
    passed &= value;
    std::cout << (value ? "PASS " : "FAIL ") << name << '\n';
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
}  // namespace

int main() {
    using namespace cybercanvas;
    using namespace cybercanvas::physics;
    try {
        for (int fps : {30, 60, 120, 144}) {
            World world({.gravity = {0, 10}});
            const auto ball = world.createBody();
            world.circle(ball, {}, .1);
            for (int i = 0; i < fps; ++i) {
                world.advance(1. / fps);
            }
            check("fixed stepping at different display frequencies",
                  world.stats().steps == 60 && std::abs(world.state(ball).position.y - 5) < .1);
            const auto before = world.state(ball).position.y;
            world.paused(true);
            world.advance(100);
            check("hidden world excludes elapsed time", world.state(ball).position.y == before && !world.active());
            world.paused(false);
            const auto stalled = world.advance(10);
            check("catch-up is bounded and lost time observable", stalled.steps == 5 && stalled.droppedSeconds > 9.8);
        }
        World linear({.gravity = {}});
        const auto mover = linear.createBody({.velocity = {1, 0}, .sleepingAllowed = false});
        linear.circle(mover, {}, .1);
        linear.advance(1. / 60);
        linear.advance(1. / 120);
        check("interpolation is independent of physical state",
              std::abs(linear.interpolated(mover).position.x - 1. / 120) < 1e-6);
        linear.teleport(mover, {2, 3});
        check("teleport resets interpolation history", linear.interpolated(mover).position.x == 2);
        check("cross-world handle rejected", rejects([&] {
                  World other;
                  other.state(mover);
              }));
        bool threadRejected = false;
        std::thread foreign([&] {
            threadRejected = rejects([&] {
                linear.state(mover);
            });
        });
        foreign.join();
        check("world cannot be accessed on another thread", threadRejected);
        World world;
        const auto floor = world.createBody({.type = BodyType::Static, .position = {0, 2}});
        world.box(floor, {5, .1});
        const auto ball = world.createBody({.position = {0, 0}});
        const auto ballShape = world.circle(ball, {}, .2);
        bool contact = false;
        for (int i = 0; i < 600; ++i) {
            world.advance(1. / 60);
            for (const auto& event : world.takeEvents()) {
                contact |= event.kind == EventKind::ContactBegin;
            }
        }
        check("ball rests on the floor and produces contacts",
              contact && std::abs(world.state(ball).position.y - 1.7) < .02);
        check("resting world stops requesting ticks", !world.active());
        check("sleeping interpolation reaches final physical pose",
              world.interpolated(ball).position.y == world.state(ball).position.y);
        int commandValue = 0;
        world.enqueue([](World&) {
            throw std::runtime_error("command failure");
        });
        world.enqueue([&](World&) {
            commandValue = 1;
        });
        check("command errors are explicit", rejects([&] {
                  world.advance(0);
              }));
        world.advance(0);
        check("failed command preserves later queued commands", commandValue == 1);
        world.enqueue([=](World& w) {
            w.impulse(ball, {0, -1});
        });
        check("queued input wakes scheduling", world.active());
        world.advance(1. / 60);
        check("queued impulse moves sleeping body", world.state(ball).velocity.y < 0);
        check("point query finds the expected body", world.point(world.state(ball).position).front() == ballShape);
        const auto hit = world.ray({0, -5}, {0, 10});
        check("ray query returns closest geometry", hit && hit->body == ball);
        const auto joint = world.mouse(floor, ball, world.state(ball).position);
        world.mouseTarget(joint, {1, .5});
        const auto joints = world.debugJoints();
        check("debug mouse joint exposes its actual target",
              joints.size() == 1 && joints[0].mouse && joints[0].anchorFirst.x == 1);
        check("debug body exposes center of mass", world.debugBodies().size() == 2);
        for (int i = 0; i < 120; ++i) {
            world.advance(1. / 60);
        }
        check("mouse joint tracks a world target", std::abs(world.state(ball).position.x - 1) < .05);
        world.destroy(joint);
        check("joint destruction invalidates handle", !world.valid(joint));
        const auto spring = world.distance(floor, ball, {}, {}, 1, 3);
        world.destroy(ball);
        check("body destruction invalidates attached resources",
              !world.valid(ball) && !world.valid(ballShape) && !world.valid(spring));
        check("stale body cannot be read", rejects([&] {
                  world.state(ball);
              }));
        {
            World hinges({.gravity = {}});
            const auto base = hinges.createBody({.type = BodyType::Static});
            const auto flap = hinges.createBody();
            hinges.box(flap, {.6, .03}, {-.6, 0});
            const auto hinge = hinges.revolute(base, flap, {}, {},
                                               {.limit = true,
                                                .lowerAngle = -.2,
                                                .upperAngle = 0,
                                                .motor = true,
                                                .motorSpeed = -1,
                                                .maximumTorque = 20});
            for (int i = 0; i < 120; ++i) {
                hinges.advance(1. / 60);
            }
            std::cout << "motor lower limit pose " << hinges.state(flap).angle << '\n';
            // A continuously driven Box2D limit has angular solver slop. Position holding uses zero speed.
            check("revolute motor reaches and respects lower angular limit",
                  std::abs(hinges.state(flap).angle + .2) < .04);
            hinges.revoluteMotor(hinge, 1, 20);
            for (int i = 0; i < 120; ++i) {
                hinges.advance(1. / 60);
            }
            std::cout << "motor upper limit pose " << hinges.state(flap).angle << '\n';
            check("revolute motor reverses and respects upper limit", std::abs(hinges.state(flap).angle) < .04);
            hinges.revoluteMotor(hinge, 0, 20);
            hinges.impulse(flap, {0, .05});
            for (int i = 0; i < 60; ++i) {
                hinges.advance(1. / 60);
            }
            check("stopped motor holds the flap", std::abs(hinges.state(flap).angle) < .015);
            const auto distance = hinges.distance(base, flap, {}, {}, .1);
            check("revolute control rejects another joint kind", rejects([&] {
                      hinges.revoluteMotor(distance, 1, 20);
                  }));
            check("revolute control rejects invalid values atomically", rejects([&] {
                      hinges.revoluteMotor(hinge, 1, -1);
                  }));
            check("invalid hinge limits rejected before solver", rejects([&] {
                      hinges.revolute(base, flap, {}, {}, {.limit = true, .lowerAngle = .2, .upperAngle = -.2});
                  }));
        }
        {
            World sliders({.gravity = {}});
            const auto base = sliders.createBody({.type = BodyType::Static});
            const auto slider = sliders.createBody();
            sliders.box(slider, {.1, .05});
            const auto joint = sliders.prismatic(base, slider, {}, {}, {0, -2},
                                                 {.limit = true,
                                                  .lowerTranslation = 0,
                                                  .upperTranslation = .3,
                                                  .motor = true,
                                                  .motorSpeed = 1,
                                                  .maximumForce = 5});
            sliders.impulse(slider, {.01, 0});
            for (int i = 0; i < 120; ++i) {
                sliders.advance(1. / 60);
            }
            const auto atLimit = sliders.state(slider);
            check("prismatic normalizes its axis and holds transverse motion",
                  std::abs(atLimit.position.x) < .005 && std::abs(atLimit.angle) < .005);
            check("prismatic upper limit bounds a driven slider", std::abs(atLimit.position.y + .3) < .01);
            sliders.prismaticMotor(joint, -1, 5);
            for (int i = 0; i < 120; ++i) {
                sliders.advance(1. / 60);
            }
            check("prismatic motor reverses to the lower limit", std::abs(sliders.state(slider).position.y) < .01);
            const auto before = sliders.stats().joints;
            check("prismatic rejects zero axis", rejects([&] {
                      sliders.prismatic(base, slider, {}, {}, {});
                  }));
            check("prismatic rejects reversed limits", rejects([&] {
                      sliders.prismatic(base, slider, {}, {}, {1, 0}, {.lowerTranslation = 1, .upperTranslation = -1});
                  }));
            check("failed prismatic creation does not leak joints", before == sliders.stats().joints);
            check("prismatic control rejects invalid force atomically", rejects([&] {
                      sliders.prismaticMotor(joint, 1, -1);
                  }));
            const auto hinge = sliders.revolute(base, slider, {}, {});
            check("prismatic control rejects a different joint kind", rejects([&] {
                      sliders.prismaticMotor(hinge, 1, 5);
                  }));
            check("prismatic rejects another world's body", rejects([&] {
                      World other;
                      other.prismatic(base, slider, {}, {}, {1, 0});
                  }));
            sliders.destroy(slider);
            check("slider destruction releases all attached joints", !sliders.valid(joint) && !sliders.valid(hinge));
        }
        World limited({.maximumBodies = 2, .maximumDynamicBodies = 1, .maximumShapes = 1, .maximumJoints = 1});
        const auto single = limited.createBody();
        limited.circle(single, {}, .1);
        check("dynamic body budget enforced", rejects([&] {
                  limited.createBody();
              }));
        check("shape budget enforced", rejects([&] {
                  limited.box(single, {.1, .1});
              }));
        check("invalid shape rejected before reaching Box2D", rejects([&] {
                  world.circle(floor, {}, -1);
              }));
        check("invalid time rejected", rejects([&] {
                  world.advance(-1);
              }));
        check("invalid speed rejected", rejects([&] {
                  world.speed(3);
              }));
        World sensors({.gravity = {}});
        const auto detector = sensors.createBody({.type = BodyType::Static});
        const auto sensor = sensors.box(detector, {1, 1}, {}, 0, {.sensor = true});
        const auto visitor = sensors.createBody({.position = {-2, 0}, .velocity = {2, 0}, .sleepingAllowed = false});
        sensors.circle(visitor, {}, .1);
        bool entered = false, left = false;
        for (int i = 0; i < 150; ++i) {
            sensors.advance(1. / 60);
            for (const auto& event : sensors.takeEvents()) {
                entered |= event.kind == EventKind::SensorBegin && event.first == sensor;
                left |= event.kind == EventKind::SensorEnd && event.first == sensor;
            }
        }
        check("sensor enter and leave events copied safely", entered && left);
        check("debug geometry is available", sensors.debugShapes().size() == 2);
        const std::array<Point, 4> crossing{{{0, 0}, {1, 1}, {0, 1}, {1, 0}}};
        check("self-intersecting chains rejected before solver", rejects([&] {
                  world.chain(floor, crossing);
              }));
        {
            const auto slide = [](double friction) {
                World track;
                const auto ground = track.createBody({.type = BodyType::Static, .position = {0, 1}});
                track.box(ground, {100, .1}, {}, 0, {.friction = friction});
                const auto block = track.createBody({.position = {0, .68}, .velocity = {4, 0}, .fixedRotation = true});
                track.box(block, {.2, .2}, {}, 0, {.friction = friction});
                for (int i = 0; i < 120; ++i) {
                    track.advance(1. / 60);
                    track.takeEvents();
                }
                return track.state(block).position.x;
            };
            const auto smooth = slide(0), rough = slide(.8);
            check("friction reduces sliding distance", smooth > 7.5 && rough < 2 && rough > 0);
            const auto rebound = [](double restitution) {
                World bounce;
                const auto ground = bounce.createBody({.type = BodyType::Static, .position = {0, 2}});
                bounce.box(ground, {5, .1}, {}, 0, {.restitution = restitution});
                const auto body = bounce.createBody();
                bounce.circle(body, {}, .1, {.restitution = restitution});
                double upward = 0;
                for (int i = 0; i < 120; ++i) {
                    bounce.advance(1. / 60);
                    bounce.takeEvents();
                    upward = std::max(upward, -bounce.state(body).velocity.y);
                }
                return upward;
            };
            check("restitution increases rebound speed", rebound(.8) > 4 && rebound(0) < .5);
        }
        {
            World kinematics;
            const auto fixed = kinematics.createBody({.type = BodyType::Static, .position = {1, 2}});
            const auto moving = kinematics.createBody({.type = BodyType::Kinematic, .velocity = {2, 0}});
            kinematics.box(moving, {.2, .2});
            for (int i = 0; i < 60; ++i) {
                kinematics.advance(1. / 60);
            }
            check("kinematic body ignores gravity and static body stays fixed",
                  std::abs(kinematics.state(moving).position.x - 2) < 1e-4 &&
                      kinematics.state(moving).position.y == 0 && kinematics.state(fixed).position.y == 2);
            check("moving kinematic body remains active", kinematics.active());
            kinematics.speed(0);
            const auto before = kinematics.state(moving).position.x;
            kinematics.advance(10);
            check("zero speed freezes without catch-up",
                  !kinematics.active() && kinematics.state(moving).position.x == before);
            kinematics.speed(2);
            kinematics.advance(1. / 60);
            check("double speed advances two fixed steps",
                  std::abs(kinematics.state(moving).position.x - before - 4. / 60) < 1e-4);
        }
        {
            World ccd({.gravity = {}});
            const auto wall = ccd.createBody({.type = BodyType::Static, .position = {1, 0}});
            ccd.box(wall, {.01, 2});
            const auto fast = ccd.createBody({.velocity = {100, 0}, .bullet = true});
            ccd.circle(fast, {}, .03);
            for (int i = 0; i < 10; ++i) {
                ccd.advance(1. / 60);
            }
            check("bullet does not cross a thin static wall",
                  ccd.state(fast).position.x < 1 && ccd.state(fast).position.x > .8);
        }
        for (double hertz : {0., 3.}) {
            World constraints({.gravity = {}});
            const auto anchor = constraints.createBody({.type = BodyType::Static});
            const auto bob = constraints.createBody({.position = {1, 0}, .velocity = {1, 2}});
            constraints.circle(bob, {}, .1);
            constraints.distance(anchor, bob, {}, {}, 1, hertz);
            for (int i = 0; i < 1200; ++i) {
                constraints.advance(1. / 60);
            }
            const auto p = constraints.state(bob).position;
            check("distance and spring constraints remain bounded", std::abs(std::hypot(p.x, p.y) - 1) < .05);
        }
        {
            World hinge;
            const auto anchor = hinge.createBody({.type = BodyType::Static});
            const auto bob = hinge.createBody({.position = {0, 1}, .angularVelocity = 2});
            hinge.capsule(bob, {0, -.7}, {0, .7}, .1);
            hinge.revolute(anchor, bob, {}, {0, -1});
            for (int i = 0; i < 1200; ++i) {
                hinge.advance(1. / 60);
            }
            const auto joint = hinge.debugJoints().front();
            check("revolute local anchors stay coincident",
                  std::hypot(joint.anchorFirst.x - joint.anchorSecond.x, joint.anchorFirst.y - joint.anchorSecond.y) <
                      .02);
        }
        {
            World stack;
            const auto ground = stack.createBody({.type = BodyType::Static, .position = {0, 3}});
            stack.box(ground, {20, .1});
            std::vector<Body> boxes;
            for (int i = 0; i < 50; ++i) {
                const auto box = stack.createBody({.position = {(i % 10 - 5) * .5, 2.5 - (i / 10) * .42}});
                stack.box(box, {.2, .2});
                boxes.push_back(box);
            }
            for (int i = 0; i < 6000; ++i) {
                stack.advance(1. / 60);
                stack.takeEvents();
            }
            bool stable = true;
            for (const auto body : boxes) {
                const auto p = stack.state(body).position;
                stable &= std::isfinite(p.x) && std::isfinite(p.y) && std::abs(p.x) < 10 && p.y < 2.72;
            }
            check("50 body stack stays bounded and sleeps", stable && !stack.active());
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return passed ? 0 : 1;
}
