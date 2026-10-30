#include <CyberCanvas/Physics2D.h>
#include <box2d/box2d.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <deque>
#include <map>
#include <mutex>
#include <numbers>
#include <stdexcept>
#include <thread>

namespace cybercanvas::physics {
namespace {
std::atomic<uint64_t> nextHandle{1};
std::mutex worldsMutex;
unsigned worldCount = 0;

uint64_t handle() {
    const auto value = nextHandle.fetch_add(1);
    if (!value) {
        throw std::overflow_error("Physics handles exhausted");
    }
    return value;
}

float number(double value, double minimum = -1e6, double maximum = 1e6) {
    if (!std::isfinite(value) || value < minimum || value > maximum) {
        throw std::invalid_argument("Physics value outside supported range");
    }
    return float(value);
}

b2Vec2 vector(Point value) {
    return {number(value.x), number(value.y)};
}

Point point(b2Vec2 value) {
    return {value.x, value.y};
}

b2ShapeDef shapeDefinition(Material value) {
    auto result = b2DefaultShapeDef();
    result.density = number(value.density, 0, 1e4);
    result.material.friction = number(value.friction, 0, 10);
    result.material.restitution = number(value.restitution, 0, 1);
    if (value.group < -32768 || value.group > 32767) {
        throw std::invalid_argument("Physics collision group out of range");
    }
    result.filter.categoryBits = value.category;
    result.filter.maskBits = value.mask;
    result.filter.groupIndex = value.group;
    result.isSensor = value.sensor;
    result.enableSensorEvents = result.enableContactEvents = result.enableHitEvents = true;
    return result;
}
}  // namespace

struct World::Impl {
    struct BodyRecord {
        b2BodyId native;
        BodyType type;
        BodyState previous;
    };

    struct ShapeRecord {
        b2ShapeId native;
        Body body;
        DebugShape geometry;
        uint64_t category;
    };

    struct JointRecord {
        b2JointId native;
        Body first, second;
    };

    WorldOptions options;
    b2WorldId world{};
    std::thread::id thread = std::this_thread::get_id();
    std::map<uint64_t, BodyRecord> bodies;
    std::map<uint64_t, ShapeRecord> shapes;
    std::map<uint64_t, uint64_t> nativeShapes;
    std::map<uint64_t, JointRecord> joints;
    std::deque<std::function<void(World&)>> commands;
    std::vector<Event> events;
    double accumulator = 0, rate = 1, dropped = 0;
    uint64_t steps = 0;
    bool paused = false, advancing = false;

    explicit Impl(WorldOptions value) : options(value) {
        auto definition = b2DefaultWorldDef();
        definition.gravity = vector(value.gravity);
        number(value.fixedStep, 1. / 240, 1. / 30);
        if (!value.substeps || value.substeps > 16 || !value.maximumSteps || value.maximumSteps > 16 ||
            !value.maximumBodies || value.maximumBodies > 4096 || value.maximumDynamicBodies > value.maximumBodies ||
            !value.maximumShapes || value.maximumShapes > 8192 || !value.maximumJoints || value.maximumJoints > 2048) {
            throw std::invalid_argument("Invalid physics limits");
        }
        std::lock_guard lock(worldsMutex);
        if (worldCount >= 128) {
            throw std::length_error("Box2D world limit reached");
        }
        world = b2CreateWorld(&definition);
        if (!b2World_IsValid(world)) {
            throw std::runtime_error("Box2D world creation failed");
        }
        ++worldCount;
    }

    ~Impl() {
        std::lock_guard lock(worldsMutex);
        b2DestroyWorld(world);
        --worldCount;
    }

    void check() const {
        if (std::this_thread::get_id() != thread) {
            throw std::logic_error("Physics world used from another thread");
        }
    }

    const BodyRecord& body(Body value) const {
        check();
        const auto found = bodies.find(value.id);
        if (found == bodies.end()) {
            throw std::invalid_argument("Unknown physics body");
        }
        return found->second;
    }

    const JointRecord& joint(Joint value) const {
        check();
        const auto found = joints.find(value.id);
        if (found == joints.end()) {
            throw std::invalid_argument("Unknown physics joint");
        }
        return found->second;
    }

    BodyState state(Body value) const {
        const auto id = body(value).native;
        return {value,
                physics::point(b2Body_GetPosition(id)),
                physics::point(b2Body_GetLinearVelocity(id)),
                b2Rot_GetAngle(b2Body_GetRotation(id)),
                b2Body_GetAngularVelocity(id),
                b2Body_IsAwake(id)};
    }

    void shapeCapacity(size_t count = 1) const {
        check();
        if (count > options.maximumShapes - shapes.size()) {
            throw std::length_error("Physics shape limit reached");
        }
    }

    Shape registerShape(b2ShapeId native, Body body, std::vector<Point> vertices, double radius, Material material,
                        bool destroyOnFailure = true) {
        Shape value;
        try {
            value.id = handle();
            shapes.emplace(
                value.id,
                ShapeRecord{
                    native, body, {value, body, std::move(vertices), radius, material.sensor}, material.category});
            nativeShapes.emplace(b2StoreShapeId(native), value.id);
            return value;
        } catch (...) {
            shapes.erase(value.id);
            if (destroyOnFailure) {
                b2DestroyShape(native, true);
            }
            throw;
        }
    }

    Shape shape(b2ShapeId native) const {
        const auto found = nativeShapes.find(b2StoreShapeId(native));
        return found == nativeShapes.end() ? Shape{} : Shape{found->second};
    }

    void jointCapacity(Body first, Body second) const {
        const auto& a = body(first);
        const auto& b = body(second);
        if (first == second || (a.type == BodyType::Static && b.type == BodyType::Static)) {
            throw std::invalid_argument("Joint needs distinct bodies and a movable endpoint");
        }
        if (joints.size() >= options.maximumJoints) {
            throw std::length_error("Physics joint limit reached");
        }
    }

    Joint registerJoint(b2JointId native, Body first, Body second) {
        try {
            const Joint value{handle()};
            joints.emplace(value.id, JointRecord{native, first, second});
            return value;
        } catch (...) {
            b2DestroyJoint(native);
            throw;
        }
    }

    void collect() {
        const auto append = [&](EventKind kind, b2ShapeId first, b2ShapeId second, Point position = {},
                                Point normal = {}, double speed = 0) {
            const auto a = shape(first), b = shape(second);
            if (!a.id || !b.id) {
                return;
            }
            if (events.size() >= 65536) {
                throw std::length_error("Consume physics events before advancing again");
            }
            events.push_back({kind, a, b, position, normal, speed});
        };
        const auto contacts = b2World_GetContactEvents(world);
        for (int i = 0; i < contacts.beginCount; ++i) {
            const auto& e = contacts.beginEvents[i];
            append(EventKind::ContactBegin, e.shapeIdA, e.shapeIdB);
        }
        for (int i = 0; i < contacts.endCount; ++i) {
            const auto& e = contacts.endEvents[i];
            append(EventKind::ContactEnd, e.shapeIdA, e.shapeIdB);
        }
        for (int i = 0; i < contacts.hitCount; ++i) {
            const auto& e = contacts.hitEvents[i];
            append(EventKind::Hit, e.shapeIdA, e.shapeIdB, physics::point(e.point), physics::point(e.normal),
                   e.approachSpeed);
        }
        const auto sensors = b2World_GetSensorEvents(world);
        for (int i = 0; i < sensors.beginCount; ++i) {
            const auto& e = sensors.beginEvents[i];
            append(EventKind::SensorBegin, e.sensorShapeId, e.visitorShapeId);
        }
        for (int i = 0; i < sensors.endCount; ++i) {
            const auto& e = sensors.endEvents[i];
            append(EventKind::SensorEnd, e.sensorShapeId, e.visitorShapeId);
        }
    }
};

World::World(WorldOptions options) : impl_(std::make_unique<Impl>(options)) {}

World::~World() = default;

Body World::createBody(BodyOptions options) {
    impl_->check();
    if (static_cast<unsigned>(options.type) > 2) {
        throw std::invalid_argument("Invalid physics body type");
    }
    const auto dynamic = std::count_if(impl_->bodies.begin(), impl_->bodies.end(), [](const auto& value) {
        return value.second.type == BodyType::Dynamic;
    });
    if (impl_->bodies.size() >= impl_->options.maximumBodies ||
        (options.type == BodyType::Dynamic && size_t(dynamic) >= impl_->options.maximumDynamicBodies)) {
        throw std::length_error("Physics body limit reached");
    }
    auto definition = b2DefaultBodyDef();
    definition.type = static_cast<b2BodyType>(options.type);
    definition.position = vector(options.position);
    definition.rotation = b2MakeRot(number(options.angle));
    definition.linearVelocity = vector(options.velocity);
    definition.angularVelocity = number(options.angularVelocity);
    definition.linearDamping = number(options.linearDamping, 0, 1000);
    definition.angularDamping = number(options.angularDamping, 0, 1000);
    definition.gravityScale = number(options.gravityScale, -100, 100);
    definition.fixedRotation = options.fixedRotation;
    definition.isBullet = options.bullet;
    definition.enableSleep = options.sleepingAllowed;
    const Body value{handle()};
    const auto native = b2CreateBody(impl_->world, &definition);
    try {
        impl_->bodies.emplace(value.id, Impl::BodyRecord{native, options.type, {}});
    } catch (...) {
        b2DestroyBody(native);
        throw;
    }
    impl_->bodies.at(value.id).previous = impl_->state(value);
    return value;
}

Shape World::circle(Body body, Point center, double radius, Material material) {
    const auto native = impl_->body(body).native;
    impl_->shapeCapacity();
    const auto definition = shapeDefinition(material);
    const b2Circle shape{vector(center), number(radius, .005, 1000)};
    std::vector<Point> vertices{center};
    return impl_->registerShape(b2CreateCircleShape(native, &definition, &shape), body, std::move(vertices), radius,
                                material);
}

Shape World::box(Body body, Size halfExtent, Point center, double angle, Material material) {
    const auto native = impl_->body(body).native;
    impl_->shapeCapacity();
    const auto definition = shapeDefinition(material);
    const auto shape = b2MakeOffsetBox(number(halfExtent.width, .005, 1000), number(halfExtent.height, .005, 1000),
                                       vector(center), b2MakeRot(number(angle)));
    std::vector<Point> vertices;
    for (int i = 0; i < shape.count; ++i) {
        vertices.push_back(physics::point(shape.vertices[i]));
    }
    return impl_->registerShape(b2CreatePolygonShape(native, &definition, &shape), body, std::move(vertices), 0,
                                material);
}

Shape World::capsule(Body body, Point first, Point second, double radius, Material material) {
    const auto native = impl_->body(body).native;
    impl_->shapeCapacity();
    const auto definition = shapeDefinition(material);
    const b2Capsule shape{vector(first), vector(second), number(radius, .005, 1000)};
    if (std::hypot(first.x - second.x, first.y - second.y) < .005) {
        throw std::invalid_argument("Capsule endpoints must be distinct");
    }
    std::vector<Point> vertices{first, second};
    return impl_->registerShape(b2CreateCapsuleShape(native, &definition, &shape), body, std::move(vertices), radius,
                                material);
}

Shape World::polygon(Body body, std::span<const Point> vertices, Material material) {
    const auto native = impl_->body(body).native;
    impl_->shapeCapacity();
    const auto definition = shapeDefinition(material);
    if (vertices.size() < 3 || vertices.size() > B2_MAX_POLYGON_VERTICES) {
        throw std::invalid_argument("Convex polygon needs 3..8 vertices");
    }
    std::vector<b2Vec2> points;
    for (auto vertex : vertices) {
        points.push_back(vector(vertex));
    }
    const auto hull = b2ComputeHull(points.data(), int(points.size()));
    if (hull.count != int(points.size()) || !b2ValidateHull(&hull)) {
        throw std::invalid_argument("Polygon must be strictly convex and nondegenerate");
    }
    const auto shape = b2MakePolygon(&hull, 0);
    std::vector<Point> ordered;
    for (int i = 0; i < shape.count; ++i) {
        ordered.push_back(physics::point(shape.vertices[i]));
    }
    return impl_->registerShape(b2CreatePolygonShape(native, &definition, &shape), body, std::move(ordered), 0,
                                material);
}

std::vector<Shape> World::chain(Body body, std::span<const Point> vertices, bool loop, Material material) {
    const auto& record = impl_->body(body);
    if (record.type != BodyType::Static || material.sensor || vertices.size() < 4 || vertices.size() > 256) {
        throw std::invalid_argument("Chain needs a static body, 4..256 points and a solid material");
    }
    impl_->shapeCapacity(loop ? vertices.size() : vertices.size() - 3);
    const auto shape = shapeDefinition(material);
    std::vector<b2Vec2> points;
    for (const auto vertex : vertices) {
        points.push_back(vector(vertex));
    }
    for (size_t i = 0; i < points.size(); ++i) {
        for (size_t j = i + 1; j < points.size(); ++j) {
            if (b2Distance(points[i], points[j]) <= .005f) {
                throw std::invalid_argument("Chain vertices must be distinct and separated");
            }
        }
    }
    const auto edges = loop ? points.size() : points.size() - 1;
    const auto orientation = [](b2Vec2 a, b2Vec2 b, b2Vec2 c) {
        return double(b.x - a.x) * (c.y - a.y) - double(b.y - a.y) * (c.x - a.x);
    };
    for (size_t i = 0; i < edges; ++i) {
        for (size_t j = i + 1; j < edges; ++j) {
            if (j == i + 1 || (loop && i == 0 && j == edges - 1)) {
                continue;
            }
            const auto a = points[i], b = points[(i + 1) % points.size()], c = points[j],
                       d = points[(j + 1) % points.size()];
            const bool overlap =
                std::max(std::min(a.x, b.x), std::min(c.x, d.x)) <= std::min(std::max(a.x, b.x), std::max(c.x, d.x)) &&
                std::max(std::min(a.y, b.y), std::min(c.y, d.y)) <= std::min(std::max(a.y, b.y), std::max(c.y, d.y));
            if (overlap && orientation(a, b, c) * orientation(a, b, d) <= 0 &&
                orientation(c, d, a) * orientation(c, d, b) <= 0) {
                throw std::invalid_argument("Chain edges must not self-intersect");
            }
        }
    }
    auto definition = b2DefaultChainDef();
    definition.points = points.data();
    definition.count = int(points.size());
    definition.isLoop = loop;
    definition.filter = shape.filter;
    definition.materials = &shape.material;
    definition.materialCount = 1;
    definition.enableSensorEvents = true;
    const auto native = b2CreateChain(record.native, &definition);
    std::vector<Shape> result;
    try {
        std::vector<b2ShapeId> segments(size_t(b2Chain_GetSegmentCount(native)));
        b2Chain_GetSegments(native, segments.data(), int(segments.size()));
        result.reserve(segments.size());
        for (auto segment : segments) {
            const auto geometry = b2Shape_GetChainSegment(segment).segment;
            result.push_back(impl_->registerShape(
                segment, body, {physics::point(geometry.point1), physics::point(geometry.point2)}, 0, material, false));
        }
    } catch (...) {
        for (auto shape : result) {
            impl_->nativeShapes.erase(b2StoreShapeId(impl_->shapes.at(shape.id).native));
            impl_->shapes.erase(shape.id);
        }
        b2DestroyChain(native);
        throw;
    }
    return result;
}

Joint World::distance(Body first, Body second, Point anchorFirst, Point anchorSecond, double length, double hertz,
                      double damping) {
    impl_->jointCapacity(first, second);
    auto definition = b2DefaultDistanceJointDef();
    definition.bodyIdA = impl_->body(first).native;
    definition.bodyIdB = impl_->body(second).native;
    definition.localAnchorA = vector(anchorFirst);
    definition.localAnchorB = vector(anchorSecond);
    definition.length = number(length, .005, 10000);
    definition.hertz = number(hertz, 0, 120);
    definition.enableSpring = hertz > 0;
    definition.dampingRatio = number(damping, 0, 10);
    return impl_->registerJoint(b2CreateDistanceJoint(impl_->world, &definition), first, second);
}

Joint World::revolute(Body first, Body second, Point anchorFirst, Point anchorSecond) {
    return revolute(first, second, anchorFirst, anchorSecond, {});
}

Joint World::revolute(Body first, Body second, Point anchorFirst, Point anchorSecond, RevoluteOptions options) {
    impl_->jointCapacity(first, second);
    auto definition = b2DefaultRevoluteJointDef();
    definition.bodyIdA = impl_->body(first).native;
    definition.bodyIdB = impl_->body(second).native;
    definition.localAnchorA = vector(anchorFirst);
    definition.localAnchorB = vector(anchorSecond);
    definition.referenceAngle = number(state(second).angle - state(first).angle);
    definition.enableLimit = options.limit;
    definition.lowerAngle = number(options.lowerAngle, -std::numbers::pi, std::numbers::pi);
    definition.upperAngle = number(options.upperAngle, options.lowerAngle, std::numbers::pi);
    definition.enableMotor = options.motor;
    definition.motorSpeed = number(options.motorSpeed, -1000, 1000);
    definition.maxMotorTorque = number(options.maximumTorque, 0, 1e6);
    definition.enableSpring = options.springHertz > 0;
    definition.hertz = number(options.springHertz, 0, 120);
    definition.dampingRatio = number(options.springDamping, 0, 10);
    definition.targetAngle = number(options.targetAngle);
    return impl_->registerJoint(b2CreateRevoluteJoint(impl_->world, &definition), first, second);
}

void World::revoluteMotor(Joint joint, double speed, double maximumTorque) {
    const auto& record = impl_->joint(joint);
    if (b2Joint_GetType(record.native) != b2_revoluteJoint) {
        throw std::invalid_argument("Expected a revolute joint");
    }
    const auto nativeSpeed = number(speed, -1000, 1000), torque = number(maximumTorque, 0, 1e6);
    b2RevoluteJoint_SetMotorSpeed(record.native, nativeSpeed);
    b2RevoluteJoint_SetMaxMotorTorque(record.native, torque);
    b2RevoluteJoint_EnableMotor(record.native, true);
    wake(record.second);
}

Joint World::prismatic(Body first, Body second, Point anchorFirst, Point anchorSecond, Point axis,
                       PrismaticOptions options) {
    impl_->jointCapacity(first, second);
    const auto direction = vector(axis);
    const double length = std::hypot(direction.x, direction.y);
    if (length < 1e-6) {
        throw std::invalid_argument("Prismatic axis must be nonzero");
    }
    auto definition = b2DefaultPrismaticJointDef();
    definition.bodyIdA = impl_->body(first).native;
    definition.bodyIdB = impl_->body(second).native;
    definition.localAnchorA = vector(anchorFirst);
    definition.localAnchorB = vector(anchorSecond);
    definition.localAxisA = {float(direction.x / length), float(direction.y / length)};
    definition.referenceAngle = number(state(second).angle - state(first).angle);
    definition.enableLimit = options.limit;
    definition.lowerTranslation = number(options.lowerTranslation, -10000, 10000);
    definition.upperTranslation = number(options.upperTranslation, options.lowerTranslation, 10000);
    definition.enableSpring = options.hertz > 0;
    definition.hertz = number(options.hertz, 0, 120);
    definition.dampingRatio = number(options.damping, 0, 10);
    definition.targetTranslation = number(options.targetTranslation, -10000, 10000);
    definition.enableMotor = options.motor;
    definition.motorSpeed = number(options.motorSpeed, -1000, 1000);
    definition.maxMotorForce = number(options.maximumForce, 0, 1e6);
    return impl_->registerJoint(b2CreatePrismaticJoint(impl_->world, &definition), first, second);
}

void World::prismaticMotor(Joint joint, double speed, double maximumForce) {
    const auto& record = impl_->joint(joint);
    if (b2Joint_GetType(record.native) != b2_prismaticJoint) {
        throw std::invalid_argument("Expected a prismatic joint");
    }
    const auto nativeSpeed = number(speed, -1000, 1000), force = number(maximumForce, 0, 1e6);
    b2PrismaticJoint_SetMotorSpeed(record.native, nativeSpeed);
    b2PrismaticJoint_SetMaxMotorForce(record.native, force);
    b2PrismaticJoint_EnableMotor(record.native, true);
    wake(record.second);
}

Joint World::mouse(Body ground, Body body, Point target, double maximumForce, double hertz, double damping) {
    impl_->jointCapacity(ground, body);
    if (impl_->body(ground).type != BodyType::Static || impl_->body(body).type != BodyType::Dynamic) {
        throw std::invalid_argument("Mouse joint requires static ground and dynamic body");
    }
    auto definition = b2DefaultMouseJointDef();
    definition.bodyIdA = impl_->body(ground).native;
    definition.bodyIdB = impl_->body(body).native;
    definition.target = vector(target);
    definition.maxForce = number(maximumForce, .001);
    definition.hertz = number(hertz, .01, 120);
    definition.dampingRatio = number(damping, 0, 10);
    wake(body);
    return impl_->registerJoint(b2CreateMouseJoint(impl_->world, &definition), ground, body);
}

void World::mouseTarget(Joint joint, Point target) {
    const auto& record = impl_->joint(joint);
    if (b2Joint_GetType(record.native) != b2_mouseJoint) {
        throw std::invalid_argument("Expected a mouse joint");
    }
    b2MouseJoint_SetTarget(record.native, vector(target));
    wake(record.second);
}

void World::destroy(Body body) {
    const auto native = impl_->body(body).native;
    for (auto it = impl_->shapes.begin(); it != impl_->shapes.end();) {
        if (it->second.body == body) {
            impl_->nativeShapes.erase(b2StoreShapeId(it->second.native));
            it = impl_->shapes.erase(it);
        } else {
            ++it;
        }
    }
    std::erase_if(impl_->joints, [&](const auto& value) {
        return value.second.first == body || value.second.second == body;
    });
    b2DestroyBody(native);
    impl_->bodies.erase(body.id);
}

void World::destroy(Joint joint) {
    b2DestroyJoint(impl_->joint(joint).native);
    impl_->joints.erase(joint.id);
}

bool World::valid(Body body) const {
    impl_->check();
    return impl_->bodies.contains(body.id);
}

bool World::valid(Shape shape) const {
    impl_->check();
    return impl_->shapes.contains(shape.id);
}

bool World::valid(Joint joint) const {
    impl_->check();
    return impl_->joints.contains(joint.id);
}

Body World::owner(Shape shape) const {
    if (!valid(shape)) {
        throw std::invalid_argument("Unknown physics shape");
    }
    return impl_->shapes.at(shape.id).body;
}

void World::impulse(Body body, Point value) {
    b2Body_ApplyLinearImpulseToCenter(impl_->body(body).native, vector(value), true);
}

void World::angularImpulse(Body body, double value) {
    b2Body_ApplyAngularImpulse(impl_->body(body).native, number(value), true);
}

void World::force(Body body, Point value) {
    b2Body_ApplyForceToCenter(impl_->body(body).native, vector(value), true);
}

void World::velocity(Body body, Point value, double angular) {
    const auto native = impl_->body(body).native;
    const auto v = vector(value);
    const auto spin = number(angular);
    b2Body_SetLinearVelocity(native, v);
    b2Body_SetAngularVelocity(native, spin);
}

void World::teleport(Body body, Point position, double angle) {
    b2Body_SetTransform(impl_->body(body).native, vector(position), b2MakeRot(number(angle)));
    wake(body);
    impl_->bodies.at(body.id).previous = state(body);
}

void World::wake(Body body) {
    b2Body_SetAwake(impl_->body(body).native, true);
}

BodyState World::state(Body body) const {
    return impl_->state(body);
}

Point World::centerOfMass(Body body) const {
    return physics::point(b2Body_GetWorldCenterOfMass(impl_->body(body).native));
}

BodyState World::interpolated(Body body) const {
    auto current = state(body);
    const auto& previous = impl_->body(body).previous;
    const double alpha = impl_->accumulator / impl_->options.fixedStep;
    current.position = {std::lerp(previous.position.x, current.position.x, alpha),
                        std::lerp(previous.position.y, current.position.y, alpha)};
    current.angle = previous.angle + std::remainder(current.angle - previous.angle, 2 * std::numbers::pi) * alpha;
    return current;
}

StepResult World::advance(double elapsed) {
    impl_->check();
    number(elapsed, 0, 1e6);
    if (impl_->advancing) {
        throw std::logic_error("Physics advance cannot reenter");
    }
    if (impl_->paused || impl_->rate == 0) {
        return {0, impl_->accumulator / impl_->options.fixedStep, 0, false};
    }
    impl_->advancing = true;
    try {
        const auto count = impl_->commands.size();
        for (size_t i = 0; i < count; ++i) {
            auto command = std::move(impl_->commands.front());
            impl_->commands.pop_front();
            command(*this);
        }
        StepResult result;
        if (active()) {
            impl_->accumulator += elapsed * impl_->rate;
            const double fixed = impl_->options.fixedStep;
            while (impl_->accumulator + 1e-12 >= fixed && result.steps < impl_->options.maximumSteps) {
                for (auto& [id, record] : impl_->bodies) {
                    record.previous = impl_->state({id});
                }
                b2World_Step(impl_->world, float(fixed), int(impl_->options.substeps));
                ++impl_->steps;
                ++result.steps;
                impl_->accumulator = std::max(0., impl_->accumulator - fixed);
                impl_->collect();
            }
            if (impl_->accumulator >= fixed) {
                const double remaining = std::fmod(impl_->accumulator, fixed);
                result.droppedSeconds = impl_->accumulator - remaining;
                impl_->dropped += result.droppedSeconds;
                impl_->accumulator = remaining;
            }
        } else {
            impl_->accumulator = 0;
        }
        result.alpha = impl_->accumulator / impl_->options.fixedStep;
        result.active = active();
        if (!result.active) {
            impl_->accumulator = 0;
            result.alpha = 0;
            for (auto& [id, record] : impl_->bodies) {
                record.previous = impl_->state({id});
            }
        }
        impl_->advancing = false;
        return result;
    } catch (...) {
        impl_->advancing = false;
        throw;
    }
}

void World::paused(bool value) {
    impl_->check();
    impl_->paused = value;
    impl_->accumulator = 0;
    for (auto& [id, record] : impl_->bodies) {
        record.previous = impl_->state({id});
    }
}

void World::speed(double value) {
    impl_->check();
    number(value, 0, 2);
    impl_->rate = value;
}

bool World::active() const {
    impl_->check();
    if (impl_->paused || impl_->rate == 0) {
        return false;
    }
    if (!impl_->commands.empty() || b2World_GetAwakeBodyCount(impl_->world) > 0) {
        return true;
    }
    for (const auto& [id, record] : impl_->bodies) {
        if (record.type == BodyType::Kinematic) {
            const auto state = impl_->state({id});
            if (state.velocity.x != 0 || state.velocity.y != 0 || state.angularVelocity != 0) {
                return true;
            }
        }
    }
    return false;
}

void World::enqueue(std::function<void(World&)> command) {
    impl_->check();
    if (!command || impl_->commands.size() >= 1024) {
        throw std::invalid_argument("Invalid physics command or queue full");
    }
    impl_->commands.push_back(std::move(command));
}

std::vector<Event> World::takeEvents() {
    impl_->check();
    auto result = std::move(impl_->events);
    impl_->events.clear();
    return result;
}

std::vector<Shape> World::query(Rect area, uint64_t mask) const {
    impl_->check();
    const auto lower = vector({area.x, area.y});
    const auto upper = vector({area.x + number(area.width, 0), area.y + number(area.height, 0)});

    struct Context {
        const Impl* impl;
        std::vector<Shape> result;
    } context{impl_.get()};

    auto filter = b2DefaultQueryFilter();
    filter.maskBits = mask;
    b2World_OverlapAABB(
        impl_->world, {lower, upper}, filter,
        [](b2ShapeId id, void* ptr) {
            auto& context = *static_cast<Context*>(ptr);
            const auto shape = context.impl->shape(id);
            if (shape.id) {
                context.result.push_back(shape);
            }
            return true;
        },
        &context);
    std::sort(context.result.begin(), context.result.end(), [](Shape a, Shape b) {
        return a.id < b.id;
    });
    return context.result;
}

std::vector<Shape> World::point(Point position, uint64_t mask) const {
    const auto p = vector(position);
    auto result = query({position.x - .001, position.y - .001, .002, .002}, mask);
    std::erase_if(result, [&](Shape shape) {
        return !b2Shape_TestPoint(impl_->shapes.at(shape.id).native, p);
    });
    return result;
}

std::optional<RayHit> World::ray(Point origin, Point translation, uint64_t mask) const {
    impl_->check();
    auto filter = b2DefaultQueryFilter();
    filter.maskBits = mask;
    const auto hit = b2World_CastRayClosest(impl_->world, vector(origin), vector(translation), filter);
    if (!hit.hit) {
        return {};
    }
    const auto shape = impl_->shape(hit.shapeId);
    if (!shape.id) {
        return {};
    }
    return RayHit{shape, owner(shape), physics::point(hit.point), physics::point(hit.normal), hit.fraction};
}

std::vector<DebugShape> World::debugShapes() const {
    impl_->check();
    std::vector<DebugShape> result;
    for (const auto& [id, record] : impl_->shapes) {
        auto shape = record.geometry;
        const auto body = impl_->body(record.body).native;
        shape.awake = b2Body_IsAwake(body);
        for (auto& vertex : shape.vertices) {
            vertex = physics::point(b2Body_GetWorldPoint(body, vector(vertex)));
        }
        result.push_back(std::move(shape));
    }
    return result;
}

PhysicsStats World::stats() const {
    impl_->check();
    return {impl_->bodies.size(), impl_->shapes.size(),
            impl_->joints.size(), size_t(b2World_GetAwakeBodyCount(impl_->world)),
            impl_->steps,         impl_->dropped};
}

std::vector<DebugBody> World::debugBodies() const {
    impl_->check();
    std::vector<DebugBody> result;
    for (const auto& [id, record] : impl_->bodies) {
        result.push_back({impl_->state({id}), physics::point(b2Body_GetWorldCenterOfMass(record.native))});
    }
    return result;
}

std::vector<DebugJoint> World::debugJoints() const {
    impl_->check();
    std::vector<DebugJoint> result;
    for (const auto& [id, record] : impl_->joints) {
        const bool mouse = b2Joint_GetType(record.native) == b2_mouseJoint;
        const auto first =
            mouse ? b2MouseJoint_GetTarget(record.native)
                  : b2Body_GetWorldPoint(impl_->body(record.first).native, b2Joint_GetLocalAnchorA(record.native));
        const auto second =
            b2Body_GetWorldPoint(impl_->body(record.second).native, b2Joint_GetLocalAnchorB(record.native));
        result.push_back({{id}, record.first, record.second, physics::point(first), physics::point(second), mouse});
    }
    return result;
}
}  // namespace cybercanvas::physics
