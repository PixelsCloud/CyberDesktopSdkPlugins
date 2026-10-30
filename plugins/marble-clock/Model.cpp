#include "plugins/marble-clock/Model.h"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <fstream>
#include <nlohmann/json.hpp>
#include <numbers>

namespace marbleclock {
namespace {
Point meters(Point p) {
    return {p.x / kPixelsPerMeter, p.y / kPixelsPerMeter};
}
}  // namespace

namespace {
Geometry classicGeometry(bool compact, bool pixels = false) {
    Geometry result;
    result.width = pixels ? (compact ? 1000 : 1680) : (compact ? 720 : 1080);
    result.left = compact ? 66 : 118;
    result.pivot = pixels ? (compact ? 680 : 1000) : (compact ? 474 : 588);
    result.closedAngle = pixels ? -std::atan(5.0 / (result.pivot - result.left)) : kClosedAngle;
    const double end = pixels ? result.width - 97 : (compact ? 623 : 947);
    const double turn = pixels ? result.width - 48 : (compact ? 672 : 996);
    const double lift = pixels ? 28 : 0;
    result.track = {{{92, 122 - lift}, {end, 153 - lift}},
                    {{turn, 127 - lift}, {turn, 180 - lift}},
                    {{turn, 180 - lift}, {turn - 2, 191 - lift}},
                    {{turn - 2, 191 - lift}, {turn - 10, 198 - lift}},
                    {{turn - 10, 198 - lift}, {end + 3, 202 - lift}},
                    {{end + 3, 202 - lift}, {result.pivot + 47, 212 - lift}},
                    {{result.pivot + 47, 212 - lift}, {result.pivot - (pixels ? 70 : 140), 220 - lift}}};
    result.enclosure = {{{result.left, 213}, {result.left, floorHeight(result, result.left) - 6}, 3},
                        {{result.pivot, 233}, {result.pivot, 265}, 3},
                        {{26, 92}, {26, 348}, 3},
                        {{result.width - 26, 92}, {result.width - 26, 348}, 3},
                        {{26, pixels ? 50. : 92.}, {result.width - 26, pixels ? 50. : 92.}, 3}};
    result.returning = {{{37, 347}, {result.width - 60, 351}, 3.5}};
    return result;
}

}  // namespace

Geometry geometry(bool compact, GeometryStyle style) {
    if (style == GeometryStyle::Pixels) {
        return classicGeometry(compact, true);
    }
    if (style == GeometryStyle::Classic) {
        return classicGeometry(compact);
    }
    Geometry result;
    result.width = compact ? 720 : 1080;
    result.left = compact ? 28 : 78;
    result.pivot = compact ? 312 : 460;
    // Source registration is shared with the painted channels; collision edges
    // follow their interior floors, not the bounding rectangle of the artwork.
    const double scale = compact ? 1.5 : 1.15;
    const Point origin = compact ? Point{70, 368} : Point{670, 342};
    const auto point = [&](Point p) {
        return Point{(p.x - origin.x) / scale, (p.y - origin.y) / scale};
    };
    const std::vector<Point> floor =
        compact ? std::vector<Point>{{230, 433},  {950, 543},  {1030, 543}, {1080, 556}, {1107, 584}, {1104, 618},
                                     {1081, 650}, {1040, 672}, {973, 681},  {620, 681},  {530, 694}}
                : std::vector<Point>{{760, 462},  {1775, 571}, {1870, 571}, {1905, 596}, {1900, 627},
                                     {1870, 651}, {1790, 670}, {1285, 670}, {1190, 615}};
    for (size_t i = 1; i < floor.size(); ++i) {
        result.track.push_back({point(floor[i - 1]), point(floor[i]), 2.5});
    }
    const double bottom = compact ? (style == GeometryStyle::ExtendedCompact ? 297 : 283) : 345;
    result.closedAngle = 0;
    result.enclosure = {{{result.left, 213}, {result.left, kFloorY - 8}, 3},
                        {{result.pivot, kFloorY - 8}, {result.pivot, kFloorY + 8}, 3},
                        {{compact && style == GeometryStyle::ExtendedCompact ? -12. : 8., 0},
                         {compact && style == GeometryStyle::ExtendedCompact ? -12. : 8., bottom},
                         3},
                        {{result.width - 8, 0}, {result.width - 8, bottom}, 3}};
    result.returning = {{{22, bottom}, {result.width - 30, bottom + 1}, 3.5}};
    return result;
}

double floorHeight(const Geometry& shape, double x) {
    return shape.floorY + std::tan(shape.closedAngle) * (x - shape.pivot);
}

Model::Model(bool compact, GeometryStyle style)
    : geometry_(geometry(compact, style)),
      style_(style),
      radius_(style == GeometryStyle::Pixels ? kPixelRadius : (style == GeometryStyle::Classic ? kRadius : 10.5)),
      closedAngle_(geometry_.closedAngle),
      openAngle_(style == GeometryStyle::Pixels ? -std::atan(43.0 / (geometry_.pivot - geometry_.left)) : kOpenAngle) {
    marbles_.reserve(kMaximumMarbles);
    reset();
}

void Model::configure(const std::filesystem::path& assets) {
    std::ifstream file(assets / "skin.json");
    const auto document = nlohmann::json::parse(file);
    if (!document.contains("marbleGeometry")) {
        return;
    }
    const auto& config = document.at("marbleGeometry");
    const double scale = config.at("scale");
    const double ox = config.at("origin").at(0), oy = config.at("origin").at(1);
    const auto point = [&](const auto& p) {
        return Point{(p.at(0).template get<double>() - ox) / scale, (p.at(1).template get<double>() - oy) / scale};
    };
    geometry_.left = (config.at("left").get<double>() - ox) / scale;
    geometry_.pivot = (config.at("pivot").get<double>() - ox) / scale;
    geometry_.floorY = (config.at("floorY").get<double>() - oy) / scale;
    const auto rails = [&](std::string_view key, auto& target) {
        target.clear();
        const auto& values = config.at(std::string(key));
        for (size_t i = 1; i < values.size(); ++i) {
            target.push_back({point(values.at(i - 1)), point(values.at(i)), 2.5});
        }
    };
    rails("track", geometry_.track);
    rails("returning", geometry_.returning);
    geometry_.enclosure = {{{geometry_.left, geometry_.floorY - 60}, {geometry_.left, geometry_.floorY - 6}, 3},
                           {{geometry_.pivot, geometry_.floorY - 8}, {geometry_.pivot, geometry_.floorY + 8}, 3},
                           {{8, 0}, {8, kHeight - 8}, 3},
                           {{geometry_.width - 8, 0}, {geometry_.width - 8, kHeight - 8}, 3}};
    reset();
}

void Model::reset() {
    cancelDrag();
    marbles_.clear();
    world_ = std::make_unique<physics::World>(physics::WorldOptions{
        .gravity = {0, 64}, .maximumBodies = 80, .maximumDynamicBodies = 76, .maximumShapes = 160, .maximumJoints = 2});
    ground_ = world_->createBody({.type = physics::BodyType::Static});
    const physics::Material rail{.friction = .28, .restitution = .08, .category = 1, .mask = 2};
    for (const auto* segments : {&geometry_.track, &geometry_.enclosure, &geometry_.returning}) {
        for (const auto& segment : *segments) {
            world_->capsule(ground_, meters(segment.first), meters(segment.second), segment.radius / kPixelsPerMeter,
                            rail);
        }
    }
    flap_ = world_->createBody(
        {.position = meters({geometry_.pivot, geometry_.floorY}), .angle = closedAngle_, .gravityScale = 0});
    world_->capsule(flap_, meters({geometry_.left - geometry_.pivot, 0}), {}, 4.5 / kPixelsPerMeter,
                    {.density = 8, .friction = .24, .restitution = .02, .category = 4, .mask = 2});
    hinge_ = world_->revolute(
        ground_, flap_, meters({geometry_.pivot, geometry_.floorY}), {},
        {.limit = true, .lowerAngle = openAngle_ - closedAngle_, .upperAngle = 0, .motor = true, .maximumTorque = 12});
    phase_ = Phase::Collecting;
    accumulator_ = phaseTime_ = drainTime_ = releaseDelay_ = 0;
    pending_ = 0;
    lastRelease_ = time_ - 1;
    clockChanged_ = time_ - 1;
    ++generation_;
    // Reconstruct a stable current-minute pile without simulating hidden or skipped seconds.
    const unsigned count = second();
    const bool pixels = style_ == GeometryStyle::Pixels;
    const unsigned columns = unsigned((geometry_.pivot - geometry_.left - 30) / (pixels ? 26 : radius_ * 2 + 1));
    for (unsigned i = 0; i < count; ++i) {
        const unsigned row = i / columns, column = i % columns;
        const double x = pixels ? geometry_.left + 20 + column * 26 + (row % 2) * 13
                         : style_ == GeometryStyle::Classic
                             ? geometry_.left + 17 + column * 18 + (row % 2) * 8.5
                             : geometry_.left + radius_ + 5 + column * (radius_ * 2 + 1) + (row % 2) * radius_;
        const double height = pixels                             ? 17 + row * 22.5
                              : style_ == GeometryStyle::Classic ? 13 + row * 15.8
                                                                 : radius_ + 4 + row * radius_ * 1.8;
        add({x, floorHeight(geometry_, x) - height}, false);
    }
}

void Model::add(Point logical, bool launched) {
    if (marbles_.size() >= kMaximumMarbles) {
        return;
    }
    const auto body = world_->createBody({.position = meters(logical),
                                          .velocity = launched ? Point{.85, 0} : Point{},
                                          .linearDamping = .02,
                                          .angularDamping = .02,
                                          .bullet = true});
    try {
        // Low rolling resistance lets incoming balls spread across the shallow collection chute.
        world_->circle(body, {}, radius_ / kPixelsPerMeter,
                       {.density = 1, .friction = .02, .restitution = .35, .category = 2, .mask = 7});
        marbles_.push_back({body, ++nextToken_, generation_, time_});
    } catch (...) {
        world_->destroy(body);
        throw;
    }
}

bool Model::spawn() {
    if (marbles_.size() >= kMaximumMarbles) {
        return false;
    }
    const auto entry = geometry_.track.front().first;
    add(style_ == GeometryStyle::Pixels    ? Point{108, 72}
        : style_ == GeometryStyle::Classic ? Point{85, 106}
                                           : Point{entry.x, entry.y - radius_ - 5},
        true);
    lastRelease_ = time_;
    ++releases_;
    return true;
}

bool Model::release() {
    if (phase_ != Phase::Collecting || pending_) {
        if (pending_ >= (style_ == GeometryStyle::Pixels ? 16u : 8u)) {
            return false;
        }
        ++pending_;
        return true;
    }
    return spawn();
}

void Model::drain() {
    if (phase_ != Phase::Collecting) {
        return;
    }
    cancelDrag();
    ++generation_;
    phase_ = Phase::Opening;
    phaseTime_ = drainTime_ = 0;
}

bool Model::setTime(int hour, int minute, int secondValue, bool animate) {
    hour = std::clamp(hour, 0, 23);
    minute = std::clamp(minute, 0, 59);
    secondValue = std::clamp(secondValue, 0, 59);
    const int next = hour * 3600 + minute * 60 + secondValue;
    if (next == clock_) {
        return false;
    }
    const bool continuous = clock_ >= 0 && (next - clock_ + 86400) % 86400 == 1;
    clockChanged_ = animate && continuous ? time_ : time_ - 1;
    clock_ = next;
    if (!animate || !continuous) {
        reset();
        return true;
    }
    if (secondValue == 0) {
        // The sixtieth ball belongs to the outgoing minute. The next minute starts at 01.
        spawn();
        if (phase_ != Phase::Collecting) {
            phase_ = Phase::Collecting;
            pending_ = 0;
        }
        drain();
    } else {
        release();
    }
    return true;
}

void Model::phaseStep(double delta) {
    phaseTime_ += delta;
    if (phase_ != Phase::Collecting) {
        drainTime_ += delta;
    }
    const auto angle = world_->state(flap_).angle;
    const double target = phase_ == Phase::Opening || phase_ == Phase::Draining ? openAngle_ : closedAngle_;
    world_->revoluteMotor(hinge_, std::clamp((target - angle) * 18, -1., 1.), 12);
    if (phase_ == Phase::Opening && std::abs(angle - openAngle_) < .009) {
        phase_ = Phase::Draining;
        phaseTime_ = 0;
    } else if (phase_ == Phase::Draining &&
               ((oldMarbles() == 0 && drainTime_ > 1) || drainTime_ > (style_ == GeometryStyle::Pixels ? 12 : 8))) {
        // A dragged or obstructed old ball cannot keep the clock's minute transition open forever.
        for (size_t i = marbles_.size(); i-- > 0;) {
            if (marbles_[i].generation != generation_) {
                remove(i, true);
            }
        }
        phase_ = Phase::Closing;
        phaseTime_ = 0;
    } else if (phase_ == Phase::Closing && std::abs(angle - closedAngle_) < .007) {
        phase_ = Phase::Collecting;
        phaseTime_ = 0;
        releaseDelay_ = 0;
    }
    if (phase_ == Phase::Collecting && pending_) {
        releaseDelay_ -= delta;
        if (releaseDelay_ <= 0 && spawn()) {
            --pending_;
            releaseDelay_ = .23;
        }
    }
}

bool Model::advance(double secondsValue) {
    if (!std::isfinite(secondsValue) || secondsValue <= 0 || !active()) {
        return false;
    }
    // Use the same fixed cadence for motor control and Box2D, independent of paint frequency.
    accumulator_ += std::min(secondsValue, .08);
    const bool classic = style_ == GeometryStyle::Classic || style_ == GeometryStyle::Pixels;
    bool changed = false;
    constexpr double delta = 1. / 60;
    while (accumulator_ + 1e-8 >= delta) {
        changed = true;
        time_ += delta;
        phaseStep(delta);
        for (const auto& marble : marbles_) {
            const auto p = world_->state(marble.body).position;
            if (!classic && geometry_.width >= 1000 && p.y * kPixelsPerMeter > 245 && p.y * kPixelsPerMeter < 296 &&
                p.x * kPixelsPerMeter > geometry_.pivot - 20 && marble.body != dragged_) {
                const double mass = std::numbers::pi * std::pow(radius_ / kPixelsPerMeter, 2);
                const double drive = std::clamp((-2. - world_->state(marble.body).velocity.x) * mass * 35, -.12, .12);
                world_->force(marble.body, {drive, 0});
            }
            if (p.y * kPixelsPerMeter > (!classic && geometry_.width < 1000 ? 272 : 330) && marble.body != dragged_) {
                // The visible return belt provides horizontal traction; motion remains fully physical.
                const double mass = std::numbers::pi * std::pow(radius_ / kPixelsPerMeter, 2);
                const double traction = std::clamp((2. - world_->state(marble.body).velocity.x) * mass * 35, -.06, .06);
                world_->force(marble.body, {traction, 0});
            }
        }
        world_->advance(delta);
        for (const auto& event : world_->takeEvents()) {
            contacts_ += event.kind == physics::EventKind::ContactBegin;
        }
        for (size_t i = marbles_.size(); i-- > 0;) {
            const auto p = world_->state(marbles_[i].body).position;
            if (marbles_[i].body != dragged_ && p.x * kPixelsPerMeter > geometry_.width - 110 &&
                p.y * kPixelsPerMeter > (!classic && geometry_.width < 1000 ? 272 : 315)) {
                remove(i);
            } else if (!std::isfinite(p.x) || !std::isfinite(p.y) ||
                       (classic ? p.y > 2 || p.x < -1 || p.x > 4
                                : p.y * kPixelsPerMeter > (geometry_.width < 1000 ? 315 : 378) || p.x < -.1 ||
                                      p.x * kPixelsPerMeter > geometry_.width + 10)) {
                remove(i, true);
            }
        }
        accumulator_ = std::max(0., accumulator_ - delta);
    }
    return changed;
}

void Model::remove(size_t index, bool recovered) {
    if (marbles_[index].body == dragged_) {
        cancelDrag();
    }
    world_->destroy(marbles_[index].body);
    marbles_.erase(marbles_.begin() + index);
    recovered ? ++recoveries_ : ++recycled_;
}

void Model::quiet() {
    reset();
}

bool Model::beginDrag(uint32_t token, Point logical) {
    cancelDrag();
    const auto found = std::find_if(marbles_.begin(), marbles_.end(), [=](const Marble& marble) {
        return marble.token == token;
    });
    if (found == marbles_.end() || !std::isfinite(logical.x) || !std::isfinite(logical.y)) {
        return false;
    }
    dragged_ = found->body;
    mouse_ = world_->mouse(ground_, dragged_, meters(logical), 8, 6, .8);
    return true;
}

void Model::drag(Point logical) {
    if (mouse_.id && std::isfinite(logical.x) && std::isfinite(logical.y)) {
        const bool classic = style_ == GeometryStyle::Classic || style_ == GeometryStyle::Pixels;
        logical.x = std::clamp(logical.x, classic ? 42. : 22., geometry_.width - (classic ? 42 : 22));
        logical.y = std::clamp(logical.y, classic ? 106. : 8., !classic && geometry_.width < 1000 ? 292. : 327.);
        world_->mouseTarget(mouse_, meters(logical));
    }
}

void Model::cancelDrag() {
    if (world_ && mouse_.id && world_->valid(mouse_)) {
        world_->destroy(mouse_);
    }
    mouse_ = {};
    dragged_ = {};
}

bool Model::dragging() const {
    return mouse_.id != 0;
}

bool Model::active() const {
    return phase_ != Phase::Collecting || pending_ || mouse_.id ||
           std::any_of(marbles_.begin(), marbles_.end(), [&](const Marble& marble) {
               return world_->state(marble.body).awake;
           });
}

const Geometry& Model::shape() const {
    return geometry_;
}

const std::vector<Marble>& Model::marbles() const {
    return marbles_;
}

physics::BodyState Model::state(const Marble& marble) const {
    return world_->interpolated(marble.body);
}

double Model::flapAngle() const {
    return world_->interpolated(flap_).angle;
}

double Model::motionTime() const {
    return time_;
}

double Model::feederAngle() const {
    const double remaining = releases_ ? std::pow(std::clamp(1 - (time_ - lastRelease_) * 5, 0., 1.), 3) : 0;
    return (double(releases_ % 16) - remaining) * std::numbers::pi / 8;
}

double Model::digitProgress() const {
    return std::clamp((time_ - clockChanged_) * 5, 0., 1.);
}

std::wstring Model::previousTimeText() const {
    const int value = (std::max(clock_, 0) + 86399) % 86400;
    wchar_t text[16]{};
    swprintf_s(text, L"%02d:%02d:%02d", value / 3600, value / 60 % 60, value % 60);
    return text;
}

Phase Model::phase() const {
    return phase_;
}

unsigned Model::pending() const {
    return pending_;
}

unsigned Model::oldMarbles() const {
    return unsigned(std::count_if(marbles_.begin(), marbles_.end(), [&](const Marble& marble) {
        return marble.generation != generation_;
    }));
}

unsigned Model::second() const {
    return clock_ < 0 ? 0 : unsigned(clock_ % 60);
}

std::wstring Model::timeText() const {
    const auto value = std::max(clock_, 0);
    wchar_t text[16]{};
    swprintf_s(text, L"%02d:%02d:%02d", value / 3600, value / 60 % 60, value % 60);
    return text;
}

physics::PhysicsStats Model::stats() const {
    return world_->stats();
}

uint64_t Model::contacts() const {
    return contacts_;
}

uint64_t Model::recycled() const {
    return recycled_;
}

uint64_t Model::recoveries() const {
    return recoveries_;
}

uint64_t Model::releases() const {
    return releases_;
}
}  // namespace marbleclock
