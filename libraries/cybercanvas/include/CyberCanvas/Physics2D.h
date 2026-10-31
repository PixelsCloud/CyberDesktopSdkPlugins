#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "CyberCanvas/Types.h"

namespace cybercanvas::physics {
struct Body {
    uint64_t id = 0;
    bool operator==(const Body&) const = default;
};

struct Shape {
    uint64_t id = 0;
    bool operator==(const Shape&) const = default;
};

struct Joint {
    uint64_t id = 0;
    bool operator==(const Joint&) const = default;
};
enum class BodyType { Static, Kinematic, Dynamic };

struct WorldOptions {
    Point gravity{0, 9.8};
    double fixedStep = 1. / 60;
    unsigned substeps = 4, maximumSteps = 5;
    size_t maximumBodies = 160, maximumDynamicBodies = 128, maximumShapes = 256, maximumJoints = 64;
    // Disable copied contact/sensor/hit notifications for visual-only worlds.
    // This does not disable collision detection or physical response.
    bool collectEvents = true;
    bool measureTimings = false;
};

struct BodyOptions {
    BodyType type = BodyType::Dynamic;
    Point position{}, velocity{};
    double angle = 0, angularVelocity = 0, linearDamping = 0, angularDamping = 0, gravityScale = 1;
    bool fixedRotation = false, bullet = false, sleepingAllowed = true;
};

struct Material {
    double density = 1, friction = .3, restitution = 0;
    uint64_t category = 1, mask = ~uint64_t(0);
    int group = 0;
    bool sensor = false;
};

struct RevoluteOptions {
    bool limit = false;
    double lowerAngle = 0, upperAngle = 0;
    bool motor = false;
    double motorSpeed = 0, maximumTorque = 0;
    double springHertz = 0, springDamping = .7, targetAngle = 0;
};

struct PrismaticOptions {
    bool limit = false;
    double lowerTranslation = 0, upperTranslation = 0;
    double hertz = 0, damping = .7, targetTranslation = 0;
    bool motor = false;
    double motorSpeed = 0, maximumForce = 0;
};

struct BodyState {
    Body body;
    Point position{}, velocity{};
    double angle = 0, angularVelocity = 0;
    bool awake = false;
};
enum class EventKind { ContactBegin, ContactEnd, SensorBegin, SensorEnd, Hit };

struct Event {
    EventKind kind;
    Shape first, second;
    Point point{}, normal{};
    double speed = 0;
};

struct RayHit {
    Shape shape;
    Body body;
    Point point{}, normal{};
    double fraction = 0;
};

struct StepResult {
    unsigned steps = 0;
    double alpha = 0, droppedSeconds = 0;
    bool active = false;
};

struct PhysicsStats {
    size_t bodies = 0, shapes = 0, joints = 0, awakeBodies = 0;
    uint64_t steps = 0;
    double droppedSeconds = 0;
    uint64_t advanceNanoseconds = 0;
    uint64_t maximumAdvanceNanoseconds = 0;
    bool timingsMeasured = false;
};

struct DebugShape {
    Shape shape;
    Body body;
    std::vector<Point> vertices;
    double radius = 0;
    bool sensor = false, awake = false;
};

struct DebugBody {
    BodyState state;
    Point centerOfMass;
};

struct DebugJoint {
    Joint joint;
    Body first, second;
    Point anchorFirst, anchorSecond;
    bool mouse = false;
};

// Meters, seconds, radians; x right, y down. One owning thread, no worker or
// timer. Handles are local to this linked module and validated by each World.
class World {
public:
    explicit World(WorldOptions options = {});
    ~World();
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    Body createBody(BodyOptions options = {});
    Shape circle(Body body, Point center, double radius, Material material = {});
    Shape box(Body body, Size halfExtent, Point center = {}, double angle = 0, Material material = {});
    Shape capsule(Body body, Point first, Point second, double radius, Material material = {});
    Shape polygon(Body body, std::span<const Point> vertices, Material material = {});
    // A chain belongs to its static body; destroy the body to remove the chain.
    // Open chains require ghost endpoints and do not collide on the end edges.
    std::vector<Shape> chain(Body body, std::span<const Point> vertices, bool loop = true, Material material = {});
    Joint distance(Body first, Body second, Point anchorFirst, Point anchorSecond, double length, double hertz = 0,
                   double damping = .7);
    Joint revolute(Body first, Body second, Point anchorFirst, Point anchorSecond);
    Joint revolute(Body first, Body second, Point anchorFirst, Point anchorSecond, RevoluteOptions options);
    // Radians/second and N*m. A zero speed motor holds the hinge against external load.
    void revoluteMotor(Joint joint, double speed, double maximumTorque);
    // Axis is in the first body's local coordinates; translations are meters.
    Joint prismatic(Body first, Body second, Point anchorFirst, Point anchorSecond, Point axis,
                    PrismaticOptions options = {});
    void prismaticMotor(Joint joint, double speed, double maximumForce);
    Joint mouse(Body ground, Body body, Point target, double maximumForce = 1000, double hertz = 5,
                double damping = .7);
    void mouseTarget(Joint joint, Point target);
    void destroy(Body body);
    void destroy(Joint joint);
    bool valid(Body body) const;
    bool valid(Shape shape) const;
    bool valid(Joint joint) const;
    Body owner(Shape shape) const;
    void impulse(Body body, Point value);
    void angularImpulse(Body body, double value);
    void force(Body body, Point value);
    void velocity(Body body, Point value, double angular = 0);
    void teleport(Body body, Point position, double angle = 0);
    void wake(Body body);

    BodyState state(Body body) const;
    Point centerOfMass(Body body) const;
    BodyState interpolated(Body body) const;
    StepResult advance(double elapsed);
    void paused(bool value);
    void speed(double value);
    bool active() const;
    // Commands queued by input/events run before the next step, including a
    // sleeping world's next advance. Paused worlds retain their command queue.
    void enqueue(std::function<void(World&)> command);
    std::vector<Event> takeEvents();
    // Visual-only scenes must drain events too. Retain the storage for reuse
    // instead of moving and reallocating the event vector every update.
    void discardEvents();
    std::vector<Shape> point(Point position, uint64_t mask = ~uint64_t(0)) const;
    std::vector<Shape> query(Rect area, uint64_t mask = ~uint64_t(0)) const;
    std::optional<RayHit> ray(Point origin, Point translation, uint64_t mask = ~uint64_t(0)) const;
    std::vector<DebugShape> debugShapes() const;
    std::vector<DebugBody> debugBodies() const;
    std::vector<DebugJoint> debugJoints() const;
    PhysicsStats stats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace cybercanvas::physics
