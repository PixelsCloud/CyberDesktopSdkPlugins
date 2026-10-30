#include "plugins/physics-toys/Model.h"

#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace toys {
namespace {
Point transformed(const physics::BodyState& state, Point local) {
    const double c = std::cos(state.angle), s = std::sin(state.angle);
    return {state.position.x + local.x * c - local.y * s, state.position.y + local.x * s + local.y * c};
}
}  // namespace

Model::Model(Kind kind, bool compact, bool mechanical, ModelOptions options)
    : kind_(kind), options_(options), mechanical_(mechanical) {
    reset(compact);
}

Model::Model(Kind kind, bool compact, bool mechanical, const ClockLayout& layout)
    : kind_(kind), clockLayout_(layout), mechanical_(mechanical) {
    if (kind != Kind::Clock || !std::isfinite(layout.size.width) || !std::isfinite(layout.size.height) ||
        layout.size.width <= 0 || layout.size.height <= 0 || layout.size.width > 2000 || layout.size.height > 2000 ||
        !std::isfinite(layout.top) || !std::isfinite(layout.length) || !std::isfinite(layout.halfWidth) ||
        !std::isfinite(layout.halfHeight) || layout.top < 0 || layout.length <= 0 || layout.halfWidth <= 0 ||
        layout.halfHeight <= 0 || layout.top + layout.length + 2 * layout.halfHeight > layout.size.height / 100) {
        throw std::invalid_argument("Invalid spring clock layout");
    }
    for (double x : layout.centers) {
        if (!std::isfinite(x) || x - layout.halfWidth < 0 || x + layout.halfWidth > layout.size.width / 100) {
            throw std::invalid_argument("Spring clock body outside chamber");
        }
    }
    reset(compact);
}

Model::Model(const ChimeLayout& layout) : kind_(Kind::Chime), chimeLayout_(layout), mechanical_(true) {
    if (!std::isfinite(layout.size.width) || !std::isfinite(layout.size.height) || layout.size.width <= 0 ||
        layout.size.height <= 0 || layout.size.width > 2000 || layout.size.height > 2000) {
        throw std::invalid_argument("Invalid chime layout");
    }
    for (size_t i = 0; i < layout.pieces.size(); ++i) {
        const auto& p = layout.pieces[i];
        if (!std::isfinite(p.center.x) || !std::isfinite(p.center.y) || !std::isfinite(p.anchor.x) ||
            !std::isfinite(p.anchor.y) || !std::isfinite(p.attachment.x) || !std::isfinite(p.attachment.y) ||
            !std::isfinite(p.half.width) || !std::isfinite(p.half.height) || p.half.width <= 0 || p.half.height <= 0 ||
            p.parent < -1 || p.parent >= int(i) || p.center.x - p.half.width < 0 || p.center.y - p.half.height < 0 ||
            p.center.x + p.half.width > layout.size.width / 100 ||
            p.center.y + p.half.height > layout.size.height / 100) {
            throw std::invalid_argument("Invalid chime piece");
        }
    }
    reset(false);
}

void Model::configure(const std::filesystem::path& assets) {
    std::ifstream file(assets / "skin.json");
    const auto document = nlohmann::json::parse(file);
    if (!document.contains("toyGeometry")) {
        return;
    }
    const auto& config = document.at("toyGeometry");
    registeredAnchors_.clear();
    registeredBodies_.clear();
    registeredTails_.clear();
    registeredSize_ = {config.at("size").at(0), config.at("size").at(1)};
    for (const auto& p : config.at("anchors")) {
        registeredAnchors_.push_back({p.at(0), p.at(1)});
    }
    const auto bodies = [&](const auto& values, auto& output) {
        for (const auto& r : values) {
            output.push_back({r.at(0), r.at(1), r.at(2), r.at(3)});
        }
    };
    bodies(config.at("bodies"), registeredBodies_);
    if (config.contains("tails")) {
        bodies(config.at("tails"), registeredTails_);
    }
    reset(compact_);
}

bool Model::fit(Size size, cybercanvas::Rect limits, std::function<Point(Point)> map) {
    if (std::abs(size.width - fittedSize_.width) + std::abs(size.height - fittedSize_.height) +
            std::abs(limits.x - limits_.x) + std::abs(limits.y - limits_.y) + std::abs(limits.width - limits_.width) +
            std::abs(limits.height - limits_.height) <
        .001) {
        return false;
    }
    fittedSize_ = size;
    limits_ = limits;
    layoutMap_ = std::move(map);
    reset(compact_);
    return true;
}

void Model::reset(bool compact) {
    compact_ = compact;
    drag_ = {};
    time_ = 0;
    accumulator_ = 0;
    pieces_.clear();
    world_ = std::make_unique<physics::World>(physics::WorldOptions{.gravity = {0, 5}});
    ground_ = world_->createBody({.type = physics::BodyType::Static});
    const auto enclosure =
        options_.separateEnclosure ? world_->createBody({.type = physics::BodyType::Static}) : ground_;
    const auto area = size();
    if (layoutMap_) {
        const double x = limits_.x / 100, y = limits_.y / 100;
        const double w = limits_.width / 100, h = limits_.height / 100;
        world_->box(enclosure, {.05, h / 2}, {x - .05, y + h / 2});
        world_->box(enclosure, {.05, h / 2}, {x + w + .05, y + h / 2});
        world_->box(enclosure, {w / 2, .05}, {x + w / 2, y - .05});
        world_->box(enclosure, {w / 2, .05}, {x + w / 2, y + h + .05});
    } else {
        world_->box(enclosure, {.05, area.height / 200}, {options_.wallInset, area.height / 200});
        world_->box(enclosure, {.05, area.height / 200}, {area.width / 100 - options_.wallInset, area.height / 200});
        world_->box(enclosure, {area.width / 200, .05}, {area.width / 200, area.height / 100 - options_.wallInset});
        world_->box(enclosure, {area.width / 200, .05}, {area.width / 200, options_.wallInset});
    }
    if (!registeredBodies_.empty()) {
        for (size_t i = 0; i < registeredBodies_.size(); ++i) {
            auto r = registeredBodies_[i];
            auto a = registeredAnchors_[i];
            if (layoutMap_) {
                const auto center = layoutMap_({r.x, r.y});
                r.x = center.x;
                r.y = center.y;
                a = layoutMap_(a);
            }
            const Size half{r.width / 200, r.height / 200};
            const Point center{r.x / 100, r.y / 100}, anchor{a.x / 100, a.y / 100};
            const auto body = world_->createBody({.position = center, .linearDamping = .5, .angularDamping = 2.5});
            world_->box(body, half, {}, 0, {.friction = .3, .restitution = .15});
            const Point attachment{0, -half.height};
            const double length = std::hypot(center.x - anchor.x, center.y + attachment.y - anchor.y);
            world_->distance(ground_, body, anchor, attachment, std::max(.01, length), 3.0, .28);
            pieces_.push_back({body, ground_, anchor, attachment, half, unsigned(i)});
            if (kind_ == Kind::Chime && i < registeredTails_.size()) {
                const auto tail = registeredTails_.at(i);
                const Size tailHalf{tail.width / 200, tail.height / 200};
                const Point tailCenter{tail.x / 100, tail.y / 100};
                const auto pendant =
                    world_->createBody({.position = tailCenter, .linearDamping = .35, .angularDamping = .5});
                world_->box(pendant, tailHalf, {}, 0, {.density = .45, .restitution = .2});
                const double gap = tailCenter.y - tailHalf.height - center.y - half.height;
                world_->distance(body, pendant, {0, half.height}, {0, -tailHalf.height}, std::max(.01, gap));
                pieces_.push_back({pendant, body, {0, half.height}, {0, -tailHalf.height}, tailHalf, unsigned(3 + i)});
            }
        }
        return;
    }
    if (chimeLayout_) {
        // The suspension and enclosure must be different bodies: joints suppress
        // parent collisions, but every piece must still collide with the walls.
        const auto suspension = world_->createBody({.type = physics::BodyType::Static});
        for (const auto& p : chimeLayout_->pieces) {
            const auto body = world_->createBody({.position = p.center, .linearDamping = .35, .angularDamping = .65});
            world_->box(body, p.half, {}, 0, {.density = p.parent < 0 ? .8 : .45, .restitution = .2});
            const auto parent = p.parent < 0 ? suspension : pieces_.at(size_t(p.parent)).body;
            world_->distance(parent, body, p.anchor, p.attachment, .005, 8, .8);
            pieces_.push_back({body, parent, p.anchor, p.attachment, p.half, p.art});
        }
        return;
    }
    if (kind_ == Kind::Clock) {
        const double top = clockLayout_  ? clockLayout_->top
                           : mechanical_ ? (options_.illustrated ? (compact ? .04 : .09) : .27)
                                         : (compact ? .12 : .35);
        const double length = clockLayout_  ? clockLayout_->length
                              : mechanical_ ? (options_.illustrated ? (compact ? .30 : .50) : (compact ? .18 : .65))
                                            : (compact ? .16 : .40);
        const double halfHeight = clockLayout_  ? clockLayout_->halfHeight
                                  : mechanical_ ? (options_.illustrated ? (compact ? .28 : .65) : (compact ? .31 : .55))
                                                : (compact ? .25 : .32);
        const double halfWidth = clockLayout_                          ? clockLayout_->halfWidth
                                 : mechanical_ && options_.illustrated ? (compact ? .215 : .27)
                                                                       : .20;
        for (int i = 0; i < 6; ++i) {
            const double x = clockLayout_ ? clockLayout_->centers[i]
                             : mechanical_
                                 ? (options_.illustrated ? (compact ? .28 + i * .59 + (i / 2) * .075 : .25 + i * .625)
                                                         : .40 + i * .49 + (i / 2) * .14)
                                 : .48 + i * .48 + (i / 2) * .12;
            const auto body = world_->createBody(
                {.position = {x, top + length + halfHeight}, .linearDamping = .5, .angularDamping = 2.5});
            world_->box(body, {halfWidth, halfHeight}, {}, 0, {.friction = .3, .restitution = .15});
            const Point attachment{0, -halfHeight};
            world_->distance(ground_, body, {x, top}, attachment, length, 3.0, .28);
            pieces_.push_back({body, ground_, {x, top}, attachment, {halfWidth, halfHeight}, unsigned(i)});
        }
    } else {
        const bool pixels = options_.pixels;
        const std::array<double, 3> lengths =
            mechanical_ ? (pixels                 ? (compact_ ? std::array{.90, 1.65, 1.05} : std::array{.85, .65, .95})
                           : options_.illustrated ? std::array{.55, .45, .68}
                                                  : std::array{.58, .42, .66})
                        : std::array{.79, .54, .94};
        const double top = mechanical_ ? (pixels && compact_ ? 1.20 : options_.illustrated ? .62 : .70) : .46;
        for (int i = 0; i < 3; ++i) {
            const double x = mechanical_ ? (pixels ? (compact_ ? std::array{.63, 1.64, 2.58}[i] : .35 + i * .95)
                                            : options_.illustrated ? .47 + i * .83
                                                                   : .45 + i * .85)
                                         : .70 + i * .60;
            const double suspension = pixels && compact_ && mechanical_ ? std::array{1.34, 1.20, 1.30}[i] : top;
            const Size half{mechanical_ ? (pixels                 ? (i == 1   ? .42
                                                                     : i == 2 ? .46
                                                                              : .36)
                                           : options_.illustrated ? (i == 1   ? .44
                                                                     : i == 2 ? .45
                                                                              : .43)
                                                                  : (i == 1   ? .28
                                                                     : i == 2 ? .31
                                                                              : .24))
                                        : (i == 1 ? .28 : .23),
                            mechanical_ ? (pixels                 ? (compact_ ? .52 : .48)
                                           : options_.illustrated ? .55
                                                                  : .32)
                            : i == 1    ? .25
                                        : .24};
            const auto body = world_->createBody(
                {.position = {x, suspension + lengths[i] + half.height}, .linearDamping = .20, .angularDamping = .55});
            if (i == 1) {
                const std::array<Point, 4> bell =
                    pixels                 ? std::array<Point, 4>{{{-.21, -.36}, {.21, -.36}, {.45, .30}, {-.45, .30}}}
                    : options_.illustrated ? std::array<Point, 4>{{{-.23, -.46}, {.23, -.46}, {.44, .40}, {-.44, .40}}}
                                           : std::array<Point, 4>{{{-.14, -.24}, {.14, -.24}, {.30, .20}, {-.30, .20}}};
                world_->polygon(body, bell, {.restitution = .3});
            } else {
                world_->circle(body, {}, half.width, {.restitution = .35});
            }
            world_->distance(ground_, body, {x, suspension}, {0, -half.height}, lengths[i]);
            pieces_.push_back({body, ground_, {x, suspension}, {0, -half.height}, half, unsigned(i)});
            const double tailLength =
                mechanical_ ? (pixels ? (compact_ ? std::array{.85, .85, .78}[i] : .65) : .18) : .32;
            const Size tailHalf =
                mechanical_ ? (pixels ? Size{compact_ && i == 1 ? .22 : .16, compact_ ? (i == 1 ? .45 : .40) : .35}
                               : options_.illustrated ? Size{.18, i == 0   ? .42
                                                                  : i == 1 ? .50
                                                                           : .54}
                                                      : Size{.12, .25})
                            : Size{.10, .18};
            const auto tail = world_->createBody(
                {.position = {x, suspension + lengths[i] + half.height * 2 + tailLength + tailHalf.height},
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
                const double mass = chimeLayout_     ? 4 * piece.half.width * piece.half.height * (tail ? .45 : .8)
                                    : tail           ? 4 * piece.half.width * piece.half.height * .45
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
        draggedPiece_ = index;
        const double force =
            chimeLayout_ ? 4 * pieces_[index].half.width * pieces_[index].half.height * (index % 2 ? .45 : .8) * 12
                         : 18;
        drag_ = world_->mouse(ground_, pieces_[index].body, target, force, 4, .8);
    }
}

void Model::drag(Point target) {
    if (drag_.id) {
        const auto area = size();
        if (chimeLayout_ && draggedPiece_) {
            const auto& piece = pieces_.at(*draggedPiece_);
            const auto state = world_->state(piece.body);
            const double halfX = std::abs(std::cos(state.angle)) * piece.half.width +
                                 std::abs(std::sin(state.angle)) * piece.half.height;
            const double halfY = std::abs(std::sin(state.angle)) * piece.half.width +
                                 std::abs(std::cos(state.angle)) * piece.half.height;
            target = {std::clamp(target.x, halfX + .025, area.width / 100 - halfX - .025),
                      std::clamp(target.y, halfY + .025, area.height / 100 - halfY - .025)};
        }
        world_->mouseTarget(drag_,
                            {std::clamp(target.x, options_.dragMargin.x, area.width / 100 - options_.dragMargin.x),
                             std::clamp(target.y, options_.dragMargin.y, area.height / 100 - options_.dragMargin.y)});
    }
}

void Model::cancelDrag() {
    if (world_ && drag_.id) {
        world_->destroy(drag_);
    }
    drag_ = {};
    draggedPiece_.reset();
}

bool Model::active() const {
    return world_->active();
}

Size Model::size() const {
    if (fittedSize_.width > 0) {
        return fittedSize_;
    }
    if (registeredSize_.width > 0) {
        return registeredSize_;
    }
    if (clockLayout_) {
        return clockLayout_->size;
    }
    if (chimeLayout_) {
        return chimeLayout_->size;
    }
    if (kind_ == Kind::Chime && options_.pixels) {
        return {compact_ ? 313. : 270., compact_ ? 600. : 410.};
    }
    return kind_ == Kind::Chime ? Size{260, options_.illustrated ? 390. : 290.}
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
}  // namespace toys
