#include <algorithm>
#include <cmath>

#include "plugins/brick-clock/Clock.h"

namespace brickclock {

const Contours& glyph(int digit) {
    static const std::array<Contours, 10> shapes{{
        {{{0, 0}, {60, 0}, {60, 100}, {0, 100}}, {{20, 20}, {20, 80}, {40, 80}, {40, 20}}},
        {{{8, 18}, {30, 0}, {46, 0}, {46, 82}, {60, 82}, {60, 100}, {8, 100}, {8, 82}, {26, 82}, {26, 26}, {8, 38}}},
        {{{0, 0},
          {60, 0},
          {60, 60},
          {20, 60},
          {20, 80},
          {60, 80},
          {60, 100},
          {0, 100},
          {0, 40},
          {40, 40},
          {40, 20},
          {0, 20}}},
        {{{0, 0},
          {60, 0},
          {60, 100},
          {0, 100},
          {0, 80},
          {40, 80},
          {40, 60},
          {10, 60},
          {10, 40},
          {40, 40},
          {40, 20},
          {0, 20}}},
        {{{0, 0}, {20, 0}, {20, 40}, {40, 40}, {40, 0}, {60, 0}, {60, 100}, {40, 100}, {40, 60}, {0, 60}}},
        {{{0, 0},
          {60, 0},
          {60, 20},
          {20, 20},
          {20, 40},
          {60, 40},
          {60, 100},
          {0, 100},
          {0, 80},
          {40, 80},
          {40, 60},
          {0, 60}}},
        {{{0, 0}, {60, 0}, {60, 20}, {20, 20}, {20, 40}, {60, 40}, {60, 100}, {0, 100}},
         {{20, 60}, {20, 80}, {40, 80}, {40, 60}}},
        {{{0, 0}, {60, 0}, {60, 24}, {30, 100}, {8, 100}, {38, 20}, {0, 20}}},
        {{{0, 0}, {60, 0}, {60, 100}, {0, 100}},
         {{20, 20}, {20, 40}, {40, 40}, {40, 20}},
         {{20, 60}, {20, 80}, {40, 80}, {40, 60}}},
        {{{0, 0}, {60, 0}, {60, 100}, {0, 100}, {0, 80}, {40, 80}, {40, 60}, {0, 60}},
         {{20, 20}, {20, 40}, {40, 40}, {40, 20}}},
    }};
    return shapes.at(size_t(std::clamp(digit, 0, 9)));
}

bool inGlyph(int digit, Point p) {
    bool inside = false;
    for (const auto& contour : glyph(digit)) {
        for (size_t i = 0, j = contour.size() - 1; i < contour.size(); j = i++) {
            const Point a = contour[i], b = contour[j];
            if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) {
                inside = !inside;
            }
        }
    }
    return inside;
}

Point digitOrigin(size_t index) {
    return {154 + (double(index) * 74 + double(index / 2) * 32) * kGlyphScale, 46};
}

Model::Model(uint32_t seed) : random_(seed) {
    const auto ground = world_.createBody({.type = physics::BodyType::Static});
    const physics::Material stone{.friction = .7, .restitution = .18};
    world_.box(ground, {5.1, .05}, {5, kFloor / 100 + .05}, 0, stone);
    world_.box(ground, {.05, 1.3}, {-.05, 1.25}, 0, stone);
    world_.box(ground, {.05, 1.3}, {10.05, 1.25}, 0, stone);
    world_.box(ground, {5.1, .05}, {5, -.05}, 0, stone);
    chips_.reserve(kMaximumChips);
}

double Model::random(double low, double high) {
    return std::uniform_real_distribution<double>(low, high)(random_);
}

bool Model::setTime(int hour, int minute, int second, bool animate) {
    hour = std::clamp(hour, 0, 23);
    minute = std::clamp(minute, 0, 59);
    second = std::clamp(second, 0, 59);
    const int nextSecond = hour * 3600 + minute * 60 + second;
    if (nextSecond == previousSecond_) {
        return false;
    }
    const bool continuous = previousSecond_ >= 0 && (nextSecond - previousSecond_ + 86400) % 86400 == 1;
    if (!animate || !continuous) {
        settle();
    }
    const std::array<int, 6> next{hour / 10, hour % 10, minute / 10, minute % 10, second / 10, second % 10};
    for (size_t i = 0; i < next.size(); ++i) {
        if (animate && continuous && next[i] != digits_[i]) {
            const auto previous = digits_[i];
            digits_[i] = next[i];
            beginCarving(i, previous);
        } else {
            digits_[i] = next[i];
        }
    }
    previousSecond_ = nextSecond;
    return true;
}

void Model::beginCarving(size_t index, int previous) {
    auto& carving = carvings_[index];
    carving = {previous, 0};
    carving.samples.reserve(chipCount_);
    // Bounded rejection sampling, using the exact contours also used to draw the cut.
    for (unsigned attempt = 0; attempt < chipCount_ * 200 && carving.samples.size() < chipCount_; ++attempt) {
        Point point{random(2, 58), random(2, 98)};
        if (inGlyph(digits_[index], point)) {
            carving.samples.push_back(point);
        }
    }
    std::sort(carving.samples.begin(), carving.samples.end(), [](Point first, Point second) {
        return first.y < second.y;
    });
}

void Model::emitChip(size_t index, Point sample) {
    if (chips_.size() == kMaximumChips) {
        removeChip(0);
    }
    const auto origin = digitOrigin(index);
    const Point source{origin.x + sample.x * kGlyphScale, origin.y + sample.y * kGlyphScale};
    // At the default 4-column size these become 4-8 pixel chips, rather than subpixel dust.
    const double radius = random(6, 10.5);
    std::vector<Point> vertices{{-radius, 0},
                                {-.35 * radius, -radius},
                                {.8 * radius, -.65 * radius},
                                {radius, .45 * radius},
                                {-.2 * radius, .8 * radius}};
    const auto body = world_.createBody({.position = {source.x / 100, source.y / 100},
                                         .velocity = {random(-.85, .85), random(-.7, -.35)},
                                         .angle = random(-.8, .8),
                                         .angularVelocity = random(-5, 5),
                                         .linearDamping = .15,
                                         .angularDamping = .6,
                                         .gravityScale = .65});
    std::vector<Point> meters;
    for (Point vertex : vertices) {
        meters.push_back({vertex.x / 100, vertex.y / 100});
    }
    try {
        world_.polygon(body, meters, {.density = 1, .friction = .65, .restitution = .38});
        chips_.push_back({body, std::move(vertices), source, index, time_});
    } catch (...) {
        world_.destroy(body);
        throw;
    }
}

void Model::removeChip(size_t index) {
    world_.destroy(chips_[index].body);
    chips_.erase(chips_.begin() + index);
}

bool Model::advance(double seconds) {
    if (!std::isfinite(seconds) || seconds <= 0 || !active()) {
        return false;
    }
    // Long gaps are discarded. Hidden/resumed time is never replayed as a burst.
    const double delta = std::min(seconds, .08);
    time_ += delta;
    for (size_t i = 0; i < carvings_.size(); ++i) {
        auto& carving = carvings_[i];
        carving.elapsed = std::min(1., carving.elapsed + delta);
        const double reveal = std::clamp((carving.elapsed - .08) / .24, 0., 1.) * 100;
        while (carving.emitted < carving.samples.size() && carving.samples[carving.emitted].y <= reveal) {
            emitChip(i, carving.samples[carving.emitted++]);
        }
    }
    world_.advance(delta);
    for (const auto& event : world_.takeEvents()) {
        contacts_ += event.kind == physics::EventKind::ContactBegin;
    }
    for (size_t i = chips_.size(); i-- > 0;) {
        if (chipAge(chips_[i]) >= kChipLifetime) {
            removeChip(i);
        }
    }
    return true;
}

void Model::settle() {
    while (!chips_.empty()) {
        removeChip(chips_.size() - 1);
    }
    for (auto& carving : carvings_) {
        carving = {};
    }
    time_ = 0;
    world_.takeEvents();
}

void Model::replay() {
    if (previousSecond_ >= 0) {
        beginCarving(5, digits_[5]);
    }
}

void Model::setChipCount(unsigned count) {
    chipCount_ = std::clamp(count, 8u, 20u);
}

bool Model::active() const {
    return !chips_.empty() || std::any_of(carvings_.begin(), carvings_.end(), [](const auto& carving) {
        return carving.elapsed < .36;
    });
}

const std::array<int, 6>& Model::digits() const {
    return digits_;
}

const std::array<Carving, 6>& Model::carvings() const {
    return carvings_;
}

const std::vector<Chip>& Model::chips() const {
    return chips_;
}

physics::BodyState Model::state(const Chip& chip) const {
    return world_.interpolated(chip.body);
}

physics::PhysicsStats Model::stats() const {
    return world_.stats();
}

double Model::chipAge(const Chip& chip) const {
    return time_ - chip.born;
}

uint64_t Model::contacts() const {
    return contacts_;
}
}  // namespace brickclock
