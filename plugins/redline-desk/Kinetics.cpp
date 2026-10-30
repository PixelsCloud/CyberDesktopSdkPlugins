#include "plugins/redline-desk/Kinetics.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace redline {
namespace {
constexpr double kUnits = 100;
constexpr std::array<double, 2> kFanSpeed{2 * std::numbers::pi / 18, -2 * std::numbers::pi / 22};
constexpr std::array<Point, 2> kFanCenters{{{285. / 1866, 283. / 615}, {858. / 1866, 254. / 615}}};

void fan(Canvas& canvas, Point center, double radius, double angle) {
    canvas.oval(center.x, center.y, radius, radius, kInk);
    for (double factor : {.42, .65, .88}) {
        canvas.oval(center.x, center.y, radius * factor, radius * factor, 0x53606c88, .65);
    }
    canvas.oval(center.x, center.y, radius, radius, kSilver, 1.3);
    canvas.saved([&] {
        canvas.translate(center.x, center.y);
        canvas.rotate(angle * 180 / std::numbers::pi);
        for (unsigned i = 0; i < 3; ++i) {
            canvas.saved([&] {
                canvas.rotate(i * 120.);
                auto blade = canvas.path();
                blade.moveTo({radius * .08, -radius * .04})
                    .cubicTo({radius * .18, -radius * .43}, {radius * .51, -radius * .75},
                             {radius * .78, -radius * .53})
                    .cubicTo({radius * .99, -radius * .34}, {radius * .75, radius * .11}, {radius * .42, radius * .15})
                    .cubicTo({radius * .23, radius * .18}, {radius * .08, radius * .12}, {radius * .08, -radius * .04})
                    .close();
                canvas.masked(blade, [&] {
                    canvas.gradientBox({-radius, -radius, radius * 2, radius * 2}, 0, 0xc5ced4ff, 0x46515bff);
                });
                canvas.stroke(blade, kSilver, {.width = .65});
                if (i == 0) {
                    canvas.line(radius * .63, -radius * .47, radius * .74, -radius * .40, kRuby, radius * .045);
                }
            });
        }
    });
    canvas.oval(center.x, center.y, radius * .17, radius * .17, 0x252d34ff);
    canvas.oval(center.x, center.y, radius * .17, radius * .17, kSilver, 1.1);
    canvas.oval(center.x, center.y, radius * .07, radius * .07, kRuby);
}

Point meters(Point point) {
    return {point.x / kUnits, point.y / kUnits};
}

void metal(Canvas& canvas, Rect bounds, bool lit = false) {
    canvas.gradientBox(bounds, 3, 0xaab6bdff, 0x30373dff);
    canvas.strokeBox(bounds, 3, kSilver, {.width = 1});
    canvas.gradientBox({bounds.x + 2, bounds.y + 2, bounds.width - 4, bounds.height - 4}, 2, 0x343b41ff, 0x171b1fff);
    canvas.line(bounds.x + 3, bounds.y + bounds.height * .55, bounds.x + bounds.width - 3,
                bounds.y + bounds.height * .55, lit ? kRuby : 0x622521ff, 2);
}

void hairspring(Canvas& canvas, Point center, double radius, double angle) {
    auto curve = canvas.path();
    for (unsigned i = 0; i <= 180; ++i) {
        const double t = double(i) / 180;
        const double a = t * 10 * std::numbers::pi + angle * (1 - t);
        const double r = radius * (.10 + .77 * t);
        const Point point{center.x + std::cos(a) * r, center.y + std::sin(a) * r};
        if (!i) {
            curve.moveTo(point);
        } else {
            curve.lineTo(point);
        }
    }
    canvas.stroke(curve, 0xaab6bdcc, {.width = .65});
}
}  // namespace

KineticModel::KineticModel(int kind, Size size)
    : kind_(kind), size_(size), world_(physics::WorldOptions{.gravity = {0, kind == 0 ? 9.8 : 0}}) {
    ground_ = world_.createBody({.type = physics::BodyType::Static});
    if (kind < 2) {
        const unsigned count = kind == 0 ? 1 : 2;
        for (unsigned i = 0; i < count; ++i) {
            const Point pivot{size.width * (kind == 0 ? .34 : kFanCenters[i].x),
                              size.height * (kind == 0 ? .20 : kFanCenters[i].y)};
            const auto body = world_.createBody({.position = meters(pivot), .angularDamping = .08});
            if (kind == 0) {
                const double length = size.height * .43 / kUnits;
                world_.capsule(body, {}, {0, length}, .025);
                world_.box(body, {.075, .12}, {0, length}, 0, {.density = 8});
            } else {
                world_.circle(body, {}, size.height * (i ? 180. : 174.) / 615 / kUnits, {.density = 2});
            }
            const auto joint = world_.revolute(ground_, body, meters(pivot), {},
                                               {.limit = kind == 0, .lowerAngle = -.38, .upperAngle = .38});
            if (kind == 1) {
                world_.revoluteMotor(joint, kFanSpeed[i], .08);
            } else {
                world_.velocity(body, {}, .8);
            }
            rotors_.push_back(body);
            rotorJoints_.push_back(joint);
            pivots_.push_back(pivot);
        }
        return;
    }
    const unsigned count = kind == 2 ? 2 : 4;
    for (unsigned i = 0; i < count; ++i) {
        const bool hat = kind == 3 && i == 3;
        const double x = size.width * (kind == 2 ? (i ? .76 : .29) : i == 0 ? .22 : i == 1 ? .52 : i == 2 ? .61 : .86);
        const double top = size.height * (hat ? .44 : kind == 2 ? (i ? .480 : .378) : i == 0 ? .341 : .494);
        const double travel = hat ? .08 : .28;
        Striker part;
        part.hat = hat;
        part.rest = {x / kUnits, top / kUnits - travel - .045};
        part.contact = {x / kUnits, top / kUnits + .025};
        part.radius = hat ? size.height * .14 : size.height * (i == 0 ? .30 : .24);
        part.target = world_.createBody(
            {.type = hat ? physics::BodyType::Static : physics::BodyType::Dynamic, .position = part.contact});
        world_.box(part.target, {hat ? .30 : .12, .025}, {}, 0, {.density = 8, .restitution = .08});
        if (!hat) {
            world_.prismatic(
                ground_, part.target, part.contact, {}, {0, 1},
                {.limit = true, .lowerTranslation = 0, .upperTranslation = .045, .hertz = 12, .damping = .8});
        }
        part.body = world_.createBody({.position = part.rest, .bullet = true});
        world_.box(part.body, {hat ? .30 : .075, .045}, {}, 0, {.density = 5, .restitution = .08});
        part.joint = world_.prismatic(ground_, part.body, part.rest, {}, {0, 1},
                                      {.limit = true, .lowerTranslation = 0, .upperTranslation = travel + .01});
        strikers_.push_back(part);
    }
}

bool KineticModel::advance(double seconds) {
    if (!std::isfinite(seconds) || seconds <= 0) {
        return false;
    }
    accumulator_ += std::min(seconds, .1);
    bool changed = false;
    while (accumulator_ >= 1. / 60) {
        accumulator_ -= 1. / 60;
        changed |= active();
        step();
    }
    return changed;
}

void KineticModel::step() {
    if (kind_ == 0) {
        const auto state = world_.state(rotors_.front());
        if (previousAngle_ * state.angle < 0 && std::abs(state.angularVelocity) > .05) {
            ++escapementSteps_;
        }
        previousAngle_ = state.angle;
    }
    for (size_t i = 0; i < rotors_.size(); ++i) {
        const auto body = rotors_[i];
        if (kind_ == 1) {
            world_.revoluteMotor(rotorJoints_[i], kFanSpeed[i], drag_.id ? 0 : .08);
            continue;
        }
        const auto state = world_.state(body);
        if (!drag_.id && std::abs(state.angle) < .08 && std::abs(state.angularVelocity) < (kind_ == 0 ? .8 : 2.4)) {
            const double impulse = kind_ == 0 ? .0015 : .03;
            world_.angularImpulse(body, state.angularVelocity >= 0 ? impulse : -impulse);
        }
    }
    for (auto& part : strikers_) {
        part.flash = std::max(0., part.flash - 1. / 60);
        part.phaseTime += 1. / 60;
        if (part.phase == 1 && part.phaseTime > .13) {
            part.phase = 2;
        }
        if (part.phase == 2) {
            const double offset = world_.state(part.body).position.y - part.rest.y;
            world_.prismaticMotor(part.joint, std::clamp(-offset * (part.hat ? 300 : 45), part.hat ? -15. : -5., 0.),
                                  part.hat ? 160 : 20);
            // Box2D permits about 5 mm of linear constraint slop. A tighter
            // threshold can leave the return controller permanently armed.
            if (offset < (part.hat ? .012 : .008)) {
                world_.prismaticMotor(part.joint, 0, 10);
                part.phase = 0;
            }
        }
    }
    world_.advance(1. / 60);
    for (const auto& event : world_.takeEvents()) {
        if (event.kind != physics::EventKind::ContactBegin) {
            continue;
        }
        const auto first = world_.owner(event.first), second = world_.owner(event.second);
        for (auto& part : strikers_) {
            if (part.phase == 1 &&
                ((first == part.body && second == part.target) || (second == part.body && first == part.target))) {
                ++part.hits;
                part.flash = .10;
                part.phase = 2;
                world_.prismaticMotor(part.joint, part.hat ? -15 : -4, part.hat ? 160 : 20);
            }
        }
    }
}

void KineticModel::strike(unsigned channel, double strength) {
    if (kind_ < 2 || !std::isfinite(strength)) {
        return;
    }
    unsigned index = kind_ == 2 ? std::min(channel, 1u) : channel;
    if (kind_ == 3 && channel == 1) {
        index = 1 + alternate_++ % 2;
    } else if (kind_ == 3 && channel == 2) {
        index = 3;
    }
    if (index >= strikers_.size() || strikers_[index].phase != 0) {
        return;
    }
    auto& part = strikers_[index];
    part.phase = 1;
    part.phaseTime = 0;
    world_.prismaticMotor(part.joint, 6 + std::clamp(strength, 0., 1.) * (part.hat ? 8 : 3), part.hat ? 160 : 25);
}

void KineticModel::beginDrag(Point position) {
    cancelDrag();
    const auto point = meters(position);
    for (const auto shape : world_.point(point)) {
        const auto body = world_.owner(shape);
        if (body != ground_ && std::find(rotors_.begin(), rotors_.end(), body) != rotors_.end()) {
            drag_ = world_.mouse(ground_, body, point, 15, 4, .8);
            return;
        }
    }
}

void KineticModel::drag(Point position) {
    if (drag_.id) {
        world_.mouseTarget(
            drag_, meters({std::clamp(position.x, 2., size_.width - 2), std::clamp(position.y, 2., size_.height - 2)}));
    }
}

void KineticModel::cancelDrag() {
    if (drag_.id) {
        world_.destroy(drag_);
        drag_ = {};
    }
}

void KineticModel::quiet() {
    cancelDrag();
    for (auto& part : strikers_) {
        if (part.phase) {
            part.phase = 2;
        }
    }
}

bool KineticModel::active() const {
    return !rotors_.empty() || world_.active() || std::any_of(strikers_.begin(), strikers_.end(), [](const auto& p) {
        return p.phase || p.flash > 0;
    });
}

physics::PhysicsStats KineticModel::stats() const {
    return world_.stats();
}

unsigned KineticModel::hits() const {
    unsigned result = 0;
    for (const auto& part : strikers_) {
        result += part.hits;
    }
    return result;
}

std::vector<physics::DebugBody> KineticModel::debugBodies() const {
    return world_.debugBodies();
}

const std::vector<Striker>& KineticModel::strikers() const {
    return strikers_;
}

void KineticModel::draw(Canvas& canvas, const std::function<void(std::string_view, Rect)>& sprite) {
    for (unsigned i = 0; i < rotors_.size(); ++i) {
        const auto state = world_.interpolated(rotors_[i]);
        const auto pivot = pivots_[i];
        if (kind_ == 1) {
            fan(canvas, pivot, size_.height * (i ? 180. : 174.) / 615, state.angle);
        } else {
            canvas.saved([&] {
                canvas.translate(size_.width * .16, size_.height * .35);
                canvas.rotate((escapementSteps_ % 30) * 12.);
                const double r = size_.height * .21;
                if (sprite) {
                    sprite("gear", {-r, -r, r * 2, r * 2});
                }
            });
            canvas.saved([&] {
                canvas.translate(pivot.x, pivot.y);
                canvas.rotate(state.angle * 180 / std::numbers::pi);
                if (sprite) {
                    sprite("pendulum", {-11, -3, 22, size_.height * .55});
                } else {
                    canvas.line(0, 0, 0, size_.height * .43, kSilver, 3);
                    metal(canvas, {-8, size_.height * .43 - 12, 16, 24}, true);
                }
            });
            canvas.oval(pivot.x, pivot.y, 4, 4, kRuby);
        }
    }
    if (kind_ == 1) {
        const Point center{size_.width * 1534 / 1866, size_.height * 419 / 615};
        const double radius = size_.height * 126 / 615;
        canvas.oval(center.x, center.y, radius, radius, kInk);
        hairspring(canvas, center, radius, 0);
        canvas.oval(center.x, center.y, radius, radius, kSilver, 1);
        for (unsigned i = 0; i < 3; ++i) {
            const double angle = -.5 + i * 2 * std::numbers::pi / 3;
            canvas.line(center.x, center.y, center.x + std::cos(angle) * radius * .9,
                        center.y + std::sin(angle) * radius * .9, kSilver, radius * .09);
        }
        canvas.oval(center.x, center.y, radius * .11, radius * .11, kRuby);
    }
    if (kind_ == 3) {
        // Couple the snare guides to its fixed right bracket rather than leaving
        // their upper ends suspended in air.
        const double y = size_.height * .05;
        canvas.line(size_.width * .64, y, size_.width * .61, size_.height * .30, kSilver, 4);
        metal(canvas, {size_.width * .50, y - 3, size_.width * .14, 6});
    }
    for (unsigned i = 0; i < strikers_.size(); ++i) {
        const auto& part = strikers_[i];
        const bool hat = kind_ == 3 && i == 3;
        if (!(kind_ == 3 && i == 2)) {
            const double x = part.contact.x * kUnits, top = part.contact.y * kUnits;
            if (!hat && sprite) {
                const auto state = world_.interpolated(part.target);
                // The skin knows the exact elliptical membrane region. Pass only its
                // physical retreat; rescaling an approximate circle would duplicate rims.
                sprite(i == 0 ? "drumFace0" : "drumFace1",
                       {0, (state.position.y - part.contact.y) * kUnits, part.flash > 0 ? 1. : 0., 0});
            } else if (!hat && !sprite) {
                const auto position = world_.interpolated(part.target).position;
                const double y = top + part.radius;
                canvas.oval(x, y, part.radius, part.radius, 0x151a1eff);
                canvas.oval(x, y, part.radius, part.radius, kSilver, 2);
                canvas.oval(x, y + (position.y - part.contact.y) * kUnits, part.radius * .86, part.radius * .86,
                            0x252b30ff);
                canvas.oval(x, y, part.radius * .93, part.radius * .93, part.flash > 0 ? kRuby : 0x632623ff, 1.5);
            } else if (hat && !sprite) {
                canvas.oval(x, top, 29, 3, kSilver);
                canvas.line(x, top, x, size_.height * .91, kSilver, 2);
            }
        }
        const auto position = world_.interpolated(part.body).position;
        canvas.line(part.rest.x * kUnits, size_.height * (hat ? .58 : .05), position.x * kUnits, position.y * kUnits,
                    kSilver, 2);
        const Rect head{position.x * kUnits - (hat ? 54 : 10), position.y * kUnits - (hat ? 24 : 6), hat ? 108. : 20.,
                        hat ? 40. : 12.};
        if (sprite) {
            sprite(hat ? "hat" : "mallet", head);
        } else {
            metal(canvas, head, part.flash > 0);
        }
        if (part.flash > 0) {
            canvas.line(part.contact.x * kUnits - 8, part.contact.y * kUnits, part.contact.x * kUnits + 8,
                        part.contact.y * kUnits, kRuby, 2);
        }
    }
}
}  // namespace redline
