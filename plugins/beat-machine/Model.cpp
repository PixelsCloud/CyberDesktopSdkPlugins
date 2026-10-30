#include "plugins/beat-machine/Model.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>

namespace beatmachine {
namespace {
Point meters(Point p) {
    return {p.x / kUnits, p.y / kUnits};
}

double limited(double value, double limit) {
    return std::clamp(value, -limit, limit);
}
}  // namespace

double illustratedLaneHeight(double x, bool compact) {
    const double end = compact ? 465 : 680, pool = compact ? 595 : 955;
    const double start = compact ? 185 : 200, middle = compact ? 260 : 235;
    return x <= end    ? start + (x - 30) * (middle - start) / (end - 30)
           : x <= pool ? middle + (x - end) * (340 - middle) / (pool - end)
                       : 340;
}

namespace {
double classicLaneHeight(double x, bool compact, bool pixels) {
    const double end = compact ? 465 : 680, pool = compact ? 595 : (pixels ? 1055 : 955);
    return x <= end ? 260 + (x - 30) * 25 / (end - 30) : x <= pool ? 285 + (x - end) * 55 / (pool - end) : 340;
}
}  // namespace

double laneHeight(double x, bool compact) {
    return classicLaneHeight(x, compact, false);
}

Model::Model(unsigned count, bool compact, GeometryStyle style, double radius)
    : compact_(compact),
      style_(style),
      radius_(radius),
      mass_(.25 * 3.141592653589793 * (radius_ / kUnits) * (radius_ / kUnits)) {
    reset(count);
}

void Model::configure(const std::filesystem::path& assets) {
    std::ifstream file(assets / "skin.json");
    const auto document = nlohmann::json::parse(file);
    if (!document.contains("physicsGeometry")) {
        return;
    }
    const auto& config = document.at("physicsGeometry");
    registeredFloor_.clear();
    registrationOrigin_ = {config.at("origin").at(0), config.at("origin").at(1)};
    registrationScale_ = config.at("scale");
    const auto point = [&](const auto& value) {
        return Point{(value.at(0).template get<double>() - registrationOrigin_.x) / registrationScale_,
                     (value.at(1).template get<double>() - registrationOrigin_.y) / registrationScale_};
    };
    for (size_t i = 0; i < registeredBumpers_.size(); ++i) {
        registeredBumpers_[i] = point(config.at("bumpers").at(i));
    }
    for (size_t i = 0; i < registeredHeads_.size(); ++i) {
        registeredHeads_[i] = point(config.at("heads").at(i));
    }
    for (size_t i = 0; i < registeredPivots_.size(); ++i) {
        registeredPivots_[i] = point(config.at("pivots").at(i));
    }
    for (const auto& p : config.at("floor")) {
        registeredFloor_.push_back(point(p));
    }
    reset(36);
}

double Model::floorHeight(double x) const {
    if (registeredFloor_.empty()) {
        return style_ == GeometryStyle::Illustrated ? illustratedLaneHeight(x, compact_)
                                                    : classicLaneHeight(x, compact_, style_ == GeometryStyle::Pixels);
    }
    for (size_t i = 1; i < registeredFloor_.size(); ++i) {
        const auto a = registeredFloor_[i - 1], b = registeredFloor_[i];
        if (x <= b.x) {
            return a.y + (b.y - a.y) * std::clamp((x - a.x) / std::max(.001, b.x - a.x), 0., 1.);
        }
    }
    return registeredFloor_.back().y;
}

void Model::reset(unsigned count) {
    cancelDrag();
    balls_.clear();
    world_ = std::make_unique<physics::World>(physics::WorldOptions{.gravity = {0, 12},
                                                                    .maximumBodies = 64,
                                                                    .maximumDynamicBodies = 56,
                                                                    .maximumShapes = 112,
                                                                    .maximumJoints = 12});
    ground_ = world_->createBody({.type = physics::BodyType::Static});
    const double end = compact_ ? 465 : 680, pool = width() - 125, wall = width() - 30;
    rails_ = {{{30, 65}, {wall, 65}},
              {{30, 65}, {30, 332}},
              {{wall, 65}, {wall, 340}},
              {{30, floorHeight(30)}, {end, floorHeight(end)}},
              {{end, floorHeight(end)}, {pool, 340}},
              {{pool, 340}, {wall, 340}},
              {{width() - 33, 102}, {width() - 75, 68}}};
    const Point origin = compact_ ? Point{565, 444} : Point{170, 320};
    const double registration = compact_ ? 1.28 : 1.1;
    const auto registered = [&](Point p) {
        return Point{(p.x - origin.x) / registration, (p.y - origin.y) / registration};
    };
    const std::array<Point, 3> targets = compact_ ? std::array<Point, 3>{{{1248, 536}, {1388, 571}, {1286, 654}}}
                                                  : std::array<Point, 3>{{{1062, 463}, {1173, 403}, {1288, 454}}};
    for (size_t i = 0; i < targets.size(); ++i) {
        bumpers_[i] = registered(targets[i]);
    }
    if (style_ != GeometryStyle::Illustrated) {
        bumpers_ = compact_ ? std::array<Point, 3>{{{505, 145}, {554, 195}, {625, 124}}}
                            : std::array<Point, 3>{{{766, 142}, {865, 195}, {932, 126}}};
    }
    if (style_ == GeometryStyle::Pixels) {
        bumpers_ = compact_ ? std::array<Point, 3>{{{499, 96}, {584, 160}, {645, 213}}}
                            : std::array<Point, 3>{{{869, 102}, {1060, 153}, {1133, 199}}};
    }
    if (!registeredFloor_.empty()) {
        bumpers_ = registeredBumpers_;
        rails_.resize(3);
        for (size_t i = 1; i < registeredFloor_.size(); ++i) {
            rails_.push_back({registeredFloor_[i - 1], registeredFloor_[i], 4.5});
        }
        rails_.push_back({{width() - 33, 102}, {width() - 75, 68}});
    }
    const bool pixels = style_ == GeometryStyle::Pixels;
    if (pixels) {
        for (size_t index = 3; index <= 5; ++index) {
            rails_[index].radius = 6;
        }
    }
    const physics::Material metal{.friction = .12, .restitution = .15, .category = 1, .mask = 2};
    for (const auto& rail : rails_) {
        world_->capsule(ground_, meters(rail.first), meters(rail.second), rail.radius / kUnits, metal);
    }
    for (size_t i = 0; i < bumpers_.size(); ++i) {
        bumperShapes_[i] = world_->circle(ground_, meters(bumpers_[i]),
                                          (pixels     ? (i == 2     ? 18
                                                         : compact_ ? 28
                                                                    : 30)
                                           : compact_ ? 20
                                                      : 24) /
                                              kUnits,
                                          {.friction = .08, .restitution = .85, .category = 1, .mask = 2});
    }
    for (size_t i = 0; i < pistons_.size(); ++i) {
        auto& piston = pistons_[i];
        piston = {};
        const double x =
            pixels ? (compact_ ? 65 + i * 115. : 117 + i * 174.) : (compact_ ? 92 + i * 105. : 132 + i * 155.);
        const std::array<Point, 5> heads =
            compact_ ? std::array<Point, 5>{{{650, 693}, {802, 720}, {946, 746}, {1076, 770}, {1450, 864}}}
                     : std::array<Point, 5>{{{235, 552}, {395, 573}, {540, 586}, {690, 596}, {1310, 680}}};
        piston.rest = style_ != GeometryStyle::Illustrated
                          ? (i == 4 ? Point{width() - 70, 328} : Point{x, floorHeight(x) + 5})
                          : registered(heads[i]);
        if (!registeredFloor_.empty()) {
            piston.rest = registeredHeads_[i];
        }
        piston.body = world_->createBody({.position = meters(piston.rest), .gravityScale = 0});
        // A closed plunger housing prevents marbles from entering beneath a raised head.
        world_->box(piston.body,
                    {(i == 4     ? 25.
                      : pixels   ? (compact_ ? 27. : 30.)
                      : compact_ ? 36.
                                 : 52.) /
                         kUnits,
                     (i == 4   ? 7.
                      : pixels ? 56.
                               : 51.) /
                         kUnits},
                    i == 4 ? Point{} : meters({0, 44}), i == 4 ? -.55 : 0,
                    {.density = 1, .friction = .04, .restitution = .35, .category = 4, .mask = 2});
        if (i == 4 && !registeredFloor_.empty()) {
            // The approved angled return barrel reaches from its elevated cap to the
            // bottom loading well. Its physical housing catches balls at that well.
            world_->capsule(piston.body, {}, meters({30, compact_ ? 70. : 52.}), 16. / kUnits,
                            {.density = .25, .friction = .04, .restitution = .35, .category = 4, .mask = 2});
        }
        piston.joint = world_->prismatic(ground_, piston.body, meters(piston.rest), {}, {0, -1},
                                         {.limit = true,
                                          .lowerTranslation = 0,
                                          .upperTranslation = i == 4 ? .22 : .15,
                                          .hertz = 3,
                                          .damping = .8,
                                          .motor = true,
                                          .maximumForce = 6});
    }
    for (size_t i = 0; i < paddles_.size(); ++i) {
        auto& paddle = paddles_[i];
        paddle = {};
        paddle.rest = registered(compact_ ? (i == 0 ? Point{742, 564} : Point{963, 567})
                                          : (i == 0 ? Point{509, 436} : Point{855, 438}));
        if (style_ != GeometryStyle::Illustrated) {
            paddle.rest = {
                pixels ? (compact_ ? 140 + i * 280. : 300 + i * 410.) : (compact_ ? 217 + i * 134. : 330 + i * 220.),
                120 + i * 13.};
        }
        if (!registeredFloor_.empty()) {
            paddle.rest = registeredPivots_[i];
        }
        paddle.body = world_->createBody({.position = meters(paddle.rest),
                                          .angle = pixels                             ? (i == 1 ? -.25 : .25)
                                                   : style_ == GeometryStyle::Classic ? .25
                                                                                      : -.30,
                                          .gravityScale = 0});
        world_->capsule(paddle.body, {}, meters({(pixels && i == 1 ? -1. : 1.) * (compact_ ? 70. : 105.), 0}),
                        (pixels ? (compact_ ? 8.5 : 12.5) : (compact_ ? 6.5 : 8)) / kUnits,
                        {.density = 4, .friction = .08, .restitution = .2, .category = 4, .mask = 2});
        world_->circle(paddle.body, {}, (compact_ ? 11 : 14) / kUnits,
                       {.density = 4, .friction = .08, .restitution = .2, .category = 4, .mask = 2});
        paddle.joint = world_->revolute(ground_, paddle.body, meters(paddle.rest), {},
                                        {.limit = true,
                                         .lowerAngle = pixels && i == 1 ? -.45 : -.55,
                                         .upperAngle = pixels && i == 1 ? .55 : .45,
                                         .motor = true,
                                         .maximumTorque = .8});
    }
    time_ = accumulator_ = 0;
    bass_ = middle_ = 0;
    lastLaunch_ = -2;
    beat_ = accent_ = launches_ = returns_ = hits_ = recoveries_ = 0;
    ejections_ = 0;
    nextHammer_ = 0;
    pistonBeats_.fill(0);
    loaderOccupied_ = false;
    flashes_.fill(0);
    count = std::clamp(count, 12u, 48u);
    const double span = width() - 150;
    const unsigned columns = std::min(count, unsigned(span / (radius_ * 2 + 3)));
    for (unsigned i = 0; i < count; ++i) {
        const double x = 50 + (i % columns + .5) * span / columns;
        const Point p{x, floorHeight(x) - radius_ - 4.5 - (i / columns) * (radius_ * 2 + 2)};
        const auto body =
            world_->createBody({.position = meters(p), .linearDamping = .025, .angularDamping = .02, .bullet = true});
        world_->circle(body, {}, radius_ / kUnits,
                       {.density = .25, .friction = .025, .restitution = .6, .category = 2, .mask = 7});
        balls_.push_back({body, i + 100});
    }
}

void Model::strike(unsigned index, double strength) {
    if (index >= pistons_.size() || !std::isfinite(strength)) {
        return;
    }
    auto& piston = pistons_[index];
    if (piston.until > time_ || (index == 4 && time_ - lastLaunch_ < .095)) {
        return;
    }
    // Keep the return launcher long enough to clear the bumper bank. The four
    // floor hammers need a short, gentle stroke rather than the same launch energy.
    piston.stroke = (index == 4 ? 100 : 14 + 16 * std::clamp(strength, 0., 1.)) / kUnits;
    piston.until = time_ + .065;
    ++piston.strikes;
    world_->wake(piston.body);
    if (index == 4) {
        lastLaunch_ = time_;
        ++launches_;
    }
}

void Model::nudge(unsigned index) {
    if (index < paddles_.size()) {
        paddles_[index].until = time_ + .2;
        world_->wake(paddles_[index].body);
    }
}

void Model::controls() {
    for (auto& piston : pistons_) {
        const auto state = world_->state(piston.body);
        const double translation = piston.rest.y / kUnits - state.position.y;
        const bool launcher = &piston == &pistons_[4];
        const double target = piston.until > time_ ? piston.stroke : 0;
        double speed = limited((target - translation) * 55, launcher ? 4.2 : 1.25);
        if (std::abs(target - translation) < .0015) {
            speed = 0;
        }
        if (std::abs(speed - piston.command) > .015 || (speed == 0 && piston.command != 0)) {
            world_->prismaticMotor(piston.joint, speed, 6);
            piston.command = speed;
        }
    }
    for (auto& paddle : paddles_) {
        const double drive = paddle.until > time_ ? -.22 : .25 - middle_ * .65;
        const double target = style_ == GeometryStyle::Pixels && &paddle == &paddles_[1] ? -drive : drive;
        const double difference = target - world_->state(paddle.body).angle;
        const double speed = std::abs(difference) < .006 ? 0 : limited(difference * 18, 4);
        if (std::abs(speed - paddle.command) > .02 || (speed == 0 && paddle.command != 0)) {
            world_->revoluteMotor(paddle.joint, speed, .8);
            paddle.command = speed;
        }
    }
}

void Model::step(const Signal& signal) {
    time_ += 1. / 60;
    bass_ = signal.bass;
    middle_ = signal.middle;
    const bool sounding = std::max({signal.bass, signal.middle, signal.high}) > .035;
    if (signal.bassMapped) {
        for (unsigned i = 0; i < pistons_.size(); ++i) {
            if (signal.pistonBeat[i] != pistonBeats_[i]) {
                pistonBeats_[i] = signal.pistonBeat[i];
                strike(i, signal.pistonStrength[i]);
            }
        }
    } else if (signal.beat != beat_) {
        beat_ = signal.beat;
        const unsigned pair = nextHammer_++ % 2;
        strike(pair, signal.strength);
        strike(pair + 2, signal.strength);
    }
    bool loaded = false;
    for (auto& ball : balls_) {
        const auto state = world_->state(ball.body);
        const Point p{state.position.x * kUnits, state.position.y * kUnits};
        loaded |= p.x > width() - 115 && p.y > 275;
        if (p.x > width() - 115 && p.y > 275) {
            ball.loaded = true;
        } else if (ball.loaded && p.x < width() - 180 && p.y < 260) {
            ball.loaded = false;
            ++returns_;
        }
        if (sounding && p.y > floorHeight(p.x) - radius_ - 12 && p.x < width() - 88 && ball.body != dragged_) {
            // The working belt moves slowly below the hammers, then accelerates balls
            // down the return ramp. Its bounded traction also brakes fast landings.
            const double speed = p.x < (compact_ ? 460 : 675) ? .22 : 1.1;
            const double acceleration = std::clamp((speed - state.velocity.x) * 8, -3., 5.);
            world_->force(ball.body, {mass_ * acceleration, 0});
        }
        if (std::hypot(state.velocity.x, state.velocity.y) > 6) {
            const double speed = std::hypot(state.velocity.x, state.velocity.y);
            world_->velocity(ball.body, {state.velocity.x * 6 / speed, state.velocity.y * 6 / speed},
                             limited(state.angularVelocity, 80));
        }
        if (p.x < 10 || p.x > width() - 10 || p.y < 40 || p.y > 370) {
            world_->teleport(ball.body, meters({width() - 130, 280}));
            world_->velocity(ball.body, {});
            ++recoveries_;
        }
    }
    loaderOccupied_ = loaded;
    if (!signal.bassMapped && loaded && sounding && time_ - lastLaunch_ > (signal.accent != accent_ ? .85 : 1.2)) {
        strike(4, 1);
        accent_ = signal.accent;
    }
    controls();
    world_->advance(1. / 60);
    for (auto& flash : flashes_) {
        flash = std::max(0., flash - 1. / 60 / .18);
    }
    for (const auto& event : world_->takeEvents()) {
        const auto first = world_->owner(event.first), second = world_->owner(event.second);
        if ((event.kind == physics::EventKind::ContactBegin || event.kind == physics::EventKind::ContactEnd) &&
            (first == pistons_[4].body || second == pistons_[4].body)) {
            const auto other = first == pistons_[4].body ? second : first;
            for (auto& ball : balls_) {
                if (ball.body == other) {
                    ball.launcherContact = event.kind == physics::EventKind::ContactBegin;
                }
            }
        }
        if (event.kind == physics::EventKind::Hit) {
            ++hits_;
            for (size_t i = 0; i < bumperShapes_.size(); ++i) {
                if (event.first == bumperShapes_[i] || event.second == bumperShapes_[i]) {
                    flashes_[i] = std::clamp(.35 + event.speed * .2 + signal.high * .4, 0., 1.);
                }
            }
        }
    }
    if (pistons_[4].until > time_) {
        for (auto& ball : balls_) {
            if (!ball.launcherContact || ball.body == dragged_ || ball.launch == launches_) {
                continue;
            }
            // Resting contacts have no Hit event. Release stored spring energy at a
            // real contact once per stroke, including a ball already seated on the head.
            const auto v = world_->state(ball.body).velocity;
            // A shallow, longer throw clears the bumper bank and lands over the left
            // hammers instead of rising into the bumpers and dropping into the loader.
            world_->impulse(ball.body, {mass_ * ((compact_ ? -3.6 : -5.4) - v.x), mass_ * (-2.6 - v.y)});
            ball.launch = launches_;
            ++ejections_;
        }
    }
}

bool Model::advance(double seconds, const Signal& signal) {
    if (!std::isfinite(seconds) || seconds < 0) {
        return false;
    }
    const bool wasActive = active();
    auto safe = signal;
    for (double* value : {&safe.bass, &safe.middle, &safe.high, &safe.strength}) {
        *value = std::isfinite(*value) ? std::clamp(*value, 0., 1.) : 0;
    }
    for (unsigned i = 0; i < safe.pistonBass.size(); ++i) {
        safe.pistonBass[i] = std::isfinite(safe.pistonBass[i]) ? std::clamp(safe.pistonBass[i], 0., 1.) : 0;
        safe.pistonStrength[i] = std::isfinite(safe.pistonStrength[i]) ? std::clamp(safe.pistonStrength[i], 0., 1.) : 0;
    }
    accumulator_ += std::min(seconds, .1);
    while (accumulator_ + 1e-9 >= 1. / 60) {
        accumulator_ = std::max(0., accumulator_ - 1. / 60);
        step(safe);
    }
    return wasActive || active();
}

void Model::quiet() {
    cancelDrag();
    accumulator_ = 0;
    bass_ = middle_ = 0;
    beat_ = accent_ = 0;
    pistonBeats_.fill(0);
    for (auto& piston : pistons_) {
        piston.until = 0;
    }
    for (auto& paddle : paddles_) {
        paddle.until = 0;
    }
}

bool Model::beginDrag(uint32_t token, Point point) {
    cancelDrag();
    for (const auto& ball : balls_) {
        if (ball.token == token) {
            dragged_ = ball.body;
            mouse_ = world_->mouse(ground_, ball.body, meters(point), .3, 5, .8);
            return true;
        }
    }
    return false;
}

void Model::drag(Point point) {
    if (dragging() && std::isfinite(point.x) && std::isfinite(point.y)) {
        world_->mouseTarget(mouse_, meters({std::clamp(point.x, 44., width() - 44), std::clamp(point.y, 80., 320.)}));
    }
}

void Model::cancelDrag() {
    if (world_ && world_->valid(mouse_)) {
        world_->destroy(mouse_);
    }
    mouse_ = {};
    dragged_ = {};
}

bool Model::active() const {
    return world_->active() ||
           std::any_of(flashes_.begin(), flashes_.end(),
                       [](double value) {
                           return value > 0;
                       }) ||
           std::any_of(pistons_.begin(), pistons_.end(),
                       [&](const auto& piston) {
                           return piston.until > time_;
                       }) ||
           std::any_of(paddles_.begin(), paddles_.end(), [&](const auto& paddle) {
               return paddle.until > time_;
           });
}

bool Model::dragging() const {
    return world_->valid(mouse_);
}

physics::BodyState Model::state(physics::Body body) const {
    return world_->state(body);
}

physics::PhysicsStats Model::stats() const {
    return world_->stats();
}

const std::vector<Ball>& Model::balls() const {
    return balls_;
}

const std::vector<Segment>& Model::rails() const {
    return rails_;
}

const std::array<Actuator, 5>& Model::pistons() const {
    return pistons_;
}

const std::array<Actuator, 2>& Model::paddles() const {
    return paddles_;
}

const std::array<Point, 3>& Model::bumpers() const {
    return bumpers_;
}

const std::array<double, 3>& Model::flashes() const {
    return flashes_;
}

double Model::time() const {
    return time_;
}

uint64_t Model::launches() const {
    return launches_;
}

uint64_t Model::ejections() const {
    return ejections_;
}

uint64_t Model::returns() const {
    return returns_;
}

uint64_t Model::hits() const {
    return hits_;
}

uint64_t Model::recoveries() const {
    return recoveries_;
}

bool Model::compact() const {
    return compact_;
}

double Model::width() const {
    return compact_ ? 720 : style_ == GeometryStyle::Pixels ? 1180 : kWidth;
}
}  // namespace beatmachine
