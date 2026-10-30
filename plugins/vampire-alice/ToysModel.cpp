#include <cmath>
#include <numbers>

#include "controllers/toys/Toys.h"

namespace alice_toys {
namespace {
Point transformed(const physics::BodyState& state, Point local) {
    const double c = std::cos(state.angle), s = std::sin(state.angle);
    return {state.position.x + local.x * c - local.y * s, state.position.y + local.x * s + local.y * c};
}
}  // namespace

Model::Model(Kind kind, bool compact, bool mechanical) : kind_(kind), mechanical_(mechanical) {
    reset(compact);
}

void Model::reset(bool compact) {
    compact_ = compact;
    drag_ = {};
    time_ = 0;
    accumulator_ = 0;
    pieces_.clear();
    world_ = std::make_unique<physics::World>(physics::WorldOptions{.gravity = {0, 5}});
    ground_ = world_->createBody({.type = physics::BodyType::Static});
    const auto area = size();
    world_->box(ground_, {.05, area.height / 200}, {-.05, area.height / 200});
    world_->box(ground_, {.05, area.height / 200}, {area.width / 100 + .05, area.height / 200});
    world_->box(ground_, {area.width / 200, .05}, {area.width / 200, area.height / 100 + .05});
    world_->box(ground_, {area.width / 200, .05}, {area.width / 200, -.05});
    if (kind_ == Kind::Clock) {
        const double top = mechanical_ ? (compact ? .04 : .09) : (compact ? .12 : .35);
        const double length = mechanical_ ? (compact ? .30 : .50) : (compact ? .16 : .40);
        const double halfHeight = mechanical_ ? (compact ? .28 : .65) : (compact ? .25 : .32);
        const double halfWidth = mechanical_ ? (compact ? .215 : .27) : .20;
        for (int i = 0; i < 6; ++i) {
            const double x = mechanical_ ? (compact ? .28 + i * .59 + (i / 2) * .075 : .25 + i * .625)
                                         : .48 + i * .48 + (i / 2) * .12;
            const auto body = world_->createBody(
                {.position = {x, top + length + halfHeight}, .linearDamping = .5, .angularDamping = 2.5});
            world_->box(body, {halfWidth, halfHeight}, {}, 0, {.friction = .3, .restitution = .15});
            const Point attachment{0, -halfHeight};
            world_->distance(ground_, body, {x, top}, attachment, length, 3.0, .28);
            pieces_.push_back({body, ground_, {x, top}, attachment, {halfWidth, halfHeight}, unsigned(i)});
        }
    } else {
        const std::array<double, 3> lengths = mechanical_ ? std::array{.55, .45, .68} : std::array{.79, .54, .94};
        const double top = mechanical_ ? .62 : .46;
        for (int i = 0; i < 3; ++i) {
            const double x = mechanical_ ? .47 + i * .83 : .70 + i * .60;
            const Size half{mechanical_ ? (i == 1   ? .44
                                           : i == 2 ? .45
                                                    : .43)
                                        : (i == 1 ? .28 : .23),
                            mechanical_ ? .55
                            : i == 1    ? .25
                                        : .24};
            const auto body = world_->createBody(
                {.position = {x, top + lengths[i] + half.height}, .linearDamping = .20, .angularDamping = .55});
            if (i == 1) {
                const std::array<Point, 4> bell{{{-.23, -.46}, {.23, -.46}, {.44, .40}, {-.44, .40}}};
                world_->polygon(body, bell, {.restitution = .3});
            } else {
                world_->circle(body, {}, half.width, {.restitution = .35});
            }
            world_->distance(ground_, body, {x, top}, {0, -half.height}, lengths[i]);
            pieces_.push_back({body, ground_, {x, top}, {0, -half.height}, half, unsigned(i)});
            const double tailLength = mechanical_ ? .18 : .32;
            const Size tailHalf = mechanical_ ? Size{.18, i == 0 ? .42 : i == 1 ? .50 : .54} : Size{.10, .18};
            const auto tail =
                world_->createBody({.position = {x, top + lengths[i] + half.height * 2 + tailLength + tailHalf.height},
                                    .linearDamping = .35,
                                    .angularDamping = .5});
            world_->box(tail, tailHalf, {}, 0, {.density = .45, .restitution = .2});
            world_->distance(body, tail, {0, half.height}, {0, -tailHalf.height}, tailLength);
            pieces_.push_back({tail, body, {0, half.height}, {0, -tailHalf.height}, tailHalf, unsigned(3 + i)});
        }
        pluck();
    }
}

bool Model::advance(double seconds, double wind, double speed) {
    if (!std::isfinite(seconds) || seconds <= 0 || !std::isfinite(wind) || !std::isfinite(speed)) {
        return false;
    }
    if (speed <= 0) {
        return false;
    }
    const bool moving = world_->active();
    bool changed = false;
    accumulator_ += std::min(seconds, .1) * std::clamp(speed, 0., 2.);
    // Apply wind once per fixed solver step, as in the other mechanical toys.
    // Its strength and trajectory therefore do not depend on host frame rate.
    while (accumulator_ + 1e-9 >= 1. / 60) {
        accumulator_ = std::max(0., accumulator_ - 1. / 60);
        time_ += 1. / 60;
        if (kind_ == Kind::Chime && wind > 0) {
            const double direction = breeze_.sample(time_ * .23, 11) * 2.6 + breeze_.sample(time_ * .61, 53) * .9;
            const double gust = .70 + .25 * breeze_.sample(time_ * .09, 91);
            for (size_t i = 0; i < pieces_.size(); ++i) {
                const auto& piece = pieces_[i];
                const bool tail = i % 2;
                const double mass = tail             ? 4 * piece.half.width * piece.half.height * .45
                                    : piece.art == 1 ? .45 * piece.half.width
                                                     : std::numbers::pi * piece.half.width * piece.half.width;
                const double flutter = breeze_.sample(time_ * (tail ? 1.4 : .8), 100 + double(i) * 13);
                double acceleration = std::clamp(
                    std::clamp(wind, 0., 1.) * gust * (6 * direction + flutter * (tail ? 2.4 : 1.4)), -2., 2.);
                const double x = world_->state(piece.body).position.x;
                const double left = x - piece.half.width;
                const double right = size().width / 100 - x - piece.half.width;
                // Fade outward gusts near the edge; the return remains a physical force.
                const double clearance = acceleration < 0 ? left : right;
                acceleration *= std::clamp((clearance - .08) / .35, 0., 1.);
                acceleration += std::clamp((.12 - left) * 8, 0., 2.) - std::clamp((.12 - right) * 8, 0., 2.);
                // force() wakes sleeping Box2D bodies, including after a calm interval.
                world_->force(piece.body, {mass * acceleration, 0});
            }
        }
        const auto result = world_->advance(1. / 60);
        changed |= result.steps && (moving || result.active);
        world_->takeEvents();
    }
    return changed;
}

bool Model::setTime(int hour, int minute, int second, bool twelve, bool animate) {
    hour = std::clamp(hour, 0, 23);
    minute = std::clamp(minute, 0, 59);
    second = std::clamp(second, 0, 59);
    const bool pm = hour >= 12;
    if (twelve) {
        hour = hour % 12 ? hour % 12 : 12;
    }
    const std::wstring next{wchar_t(L'0' + hour / 10),   wchar_t(L'0' + hour % 10),   wchar_t(L'0' + minute / 10),
                            wchar_t(L'0' + minute % 10), wchar_t(L'0' + second / 10), wchar_t(L'0' + second % 10)};
    const bool changed = digits_ != next || afternoon_ != pm;
    if (kind_ == Kind::Clock && !digits_.empty() && animate) {
        for (size_t i = 0; i < pieces_.size(); ++i) {
            if (digits_[i] != next[i]) {
                const auto half = pieces_[i].half;
                world_->impulse(pieces_[i].body, {0, -.22 * half.width * half.height / (.31 * .38)});
            }
        }
    }
    digits_ = next;
    afternoon_ = pm;
    return changed;
}

void Model::pluck() {
    for (size_t i = 0; i < pieces_.size(); ++i) {
        const double strength = mechanical_ ? (i % 2 ? .006 : .018) : .035;
        world_->impulse(pieces_[i].body,
                        {kind_ == Kind::Chime ? strength * (i % 2 ? -1 : 1) : 0, kind_ == Kind::Clock ? -.18 : 0});
    }
}

void Model::beginDrag(size_t index, Point target) {
    cancelDrag();
    if (index < pieces_.size()) {
        drag_ = world_->mouse(ground_, pieces_[index].body, target, 18, 4, .8);
    }
}

void Model::drag(Point target) {
    if (drag_.id) {
        const auto area = size();
        world_->mouseTarget(
            drag_, {std::clamp(target.x, .1, area.width / 100 - .1), std::clamp(target.y, .1, area.height / 100 - .1)});
    }
}

void Model::cancelDrag() {
    if (world_ && drag_.id) {
        world_->destroy(drag_);
    }
    drag_ = {};
}

bool Model::active() const {
    return world_->active();
}

Size Model::size() const {
    return kind_ == Kind::Chime ? Size{260, 390}
           : mechanical_        ? Size{360, compact_ ? 112. : 216.}
                                : Size{360, compact_ ? 105. : 185.};
}

Point Model::anchor(const Piece& piece) const {
    return transformed(world_->interpolated(piece.parent), piece.anchor);
}

Point Model::attachment(const Piece& piece) const {
    return transformed(world_->interpolated(piece.body), piece.attachment);
}

const std::vector<Piece>& Model::pieces() const {
    return pieces_;
}

physics::BodyState Model::state(size_t index) const {
    return world_->interpolated(pieces_.at(index).body);
}

physics::PhysicsStats Model::stats() const {
    return world_->stats();
}

const std::wstring& Model::digits() const {
    return digits_;
}

bool Model::afternoon() const {
    return afternoon_;
}

Kind Model::kind() const {
    return kind_;
}

bool Model::mechanical() const {
    return mechanical_;
}

void Model::setWindSeed(uint32_t seed) {
    breeze_ = cybercanvas::NoiseField({.seed = int32_t(seed & 0x7fffffff), .frequency = .6});
}
}  // namespace alice_toys
