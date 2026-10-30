#include "CyberCanvas/Particles.h"

namespace cybercanvas {
auto ParticleDynamics::active() const -> bool {
    return false;
}

ParticleEmitter::~ParticleEmitter() {
    if (dynamics_) {
        dynamics_->reset();
    }
}

auto ParticleEmitter::origin(Point value) -> void {
    guard();
    point(value);
    origin_ = value;
}

auto ParticleEmitter::origin() const -> Point {
    return origin_;
}

auto ParticleEmitter::wind(std::function<Point(Point, double)> value) -> void {
    guard();
    wind_ = std::move(value);
}

auto ParticleEmitter::emitting(bool value) -> void {
    guard();
    emitting_ = value;
}

auto ParticleEmitter::paused(bool value) -> void {
    guard();
    if (paused_ == value) {
        return;
    }
    paused_ = value;
    resetInterpolation();
}

auto ParticleEmitter::visible(bool value) -> void {
    guard();
    if (visible_ == value) {
        return;
    }
    visible_ = value;
    resetInterpolation();
}

auto ParticleEmitter::reducedMotion(bool value) -> void {
    guard();
    if (reduced_ == value) {
        return;
    }
    reduced_ = value;
    resetInterpolation();
}

auto ParticleEmitter::active() const -> bool {
    return !paused_ && visible_ && !reduced_ &&
           (!particles_.empty() || (emitting_ && settings_.rate > 0) || (dynamics_ && dynamics_->active()));
}

auto ParticleEmitter::reset(uint32_t seed) -> void {
    guard();
    random_.seed(seed);
    if (dynamics_) {
        dynamics_->reset();
    }
    particles_.clear();
    accumulator_ = emission_ = elapsed_ = 0;
    counters_ = {};
}

auto ParticleEmitter::burst(uint32_t count) -> void {
    guard();
    if (count > 4096) {
        throw std::length_error("Particle burst exceeds limit");
    }
    spawn(count);
}

auto ParticleEmitter::advance(double seconds) -> void {
    guard();
    validate(seconds, 0, 86400);
    if (!active()) {
        return;
    }
    stepping_ = true;

    struct Guard {
        bool& flag;

        ~Guard() {
            flag = false;
        }
    } guard{stepping_};

    accumulator_ += seconds;
    uint32_t count = 0;
    while (accumulator_ + 1e-12 >= step_ && count < 30) {
        simulate();
        accumulator_ = std::max(0., accumulator_ - step_);
        ++count;
        ++counters_.steps;
    }
    if (accumulator_ >= step_) {
        counters_.droppedSteps += uint64_t(accumulator_ / step_);
        accumulator_ = std::fmod(accumulator_, step_);
    }
}

auto ParticleEmitter::stats() const -> ParticleStats {
    auto value = counters_;
    value.alive = particles_.size();
    return value;
}

auto ParticleEmitter::particles() const -> std::vector<ParticleView> {
    std::vector<ParticleView> result;
    result.reserve(particles_.size());
    const auto alpha = accumulator_ / step_;
    for (const auto& particle : particles_) {
        result.push_back(sample(particle, alpha));
    }
    return result;
}

auto ParticleEmitter::draw(Canvas& canvas) const -> void {
    const auto alpha = accumulator_ / step_;
    for (const auto& state : particles_) {
        const auto particle = sample(state, alpha);
        if (particle.size <= 0 || !(particle.color & 255)) {
            continue;
        }
        if (settings_.atlas) {
            const auto cell =
                std::min(settings_.atlasCells - 1, uint32_t(particle.age / particle.lifetime * settings_.atlasCells));
            canvas.saved([&] {
                canvas.translate(particle.position.x, particle.position.y);
                canvas.rotate(particle.rotation);
                canvas.image(*settings_.atlas, {-particle.size / 2, -particle.size / 2, particle.size, particle.size},
                             {.cell = cell,
                              .cells = settings_.atlasCells,
                              .opacity = (particle.color & 255) / 255.,
                              .clip = false});
            });
        } else {
            canvas.oval(particle.position.x, particle.position.y, particle.size / 2, particle.size / 2, particle.color);
        }
    }
}

auto ParticleEmitter::sample(const Particle& particle, double alpha) const -> ParticleView {
    Point position = mix(particle.previous, particle.position, alpha);
    if (settings_.space == ParticleSpace::Local) {
        position.x += origin_.x;
        position.y += origin_.y;
    }
    const double age = std::max(0., particle.age - step_ + accumulator_);
    const double fraction = std::clamp(age / particle.life, 0., 1.);
    auto ink = colorAt(fraction);
    const double opacity = std::clamp(settings_.opacity.sample(fraction), 0., 1.);
    ink = (ink & 0xffffff00) | uint32_t(std::lround((ink & 255) * opacity));
    return {position,
            particle.velocity,
            age,
            particle.life,
            std::clamp(settings_.size.sample(fraction), 0., 1024.),
            ink,
            particle.id,
            std::lerp(particle.previousRotation, particle.rotation, alpha)};
}

auto ParticleEmitter::validate(double value, double minimum, double maximum) -> void {
    if (!std::isfinite(value) || value < minimum || value > maximum) {
        throw std::invalid_argument("Invalid particle value");
    }
}

auto ParticleEmitter::point(Point value) -> void {
    validate(value.x, -1e6, 1e6);
    validate(value.y, -1e6, 1e6);
}

auto ParticleEmitter::guard() const -> void {
    if (stepping_) {
        throw std::logic_error("Cannot mutate particles during a wind callback");
    }
}

auto ParticleEmitter::unit() -> double {
    return double(random_()) / 4294967296.;
}

auto ParticleEmitter::resetInterpolation() -> void {
    accumulator_ = 0;
    for (auto& particle : particles_) {
        particle.previous = particle.position;
        particle.previousRotation = particle.rotation;
    }
}

auto ParticleEmitter::colorAt(double fraction) const -> uint32_t {
    const auto& keys = settings_.colors;
    auto next = std::upper_bound(keys.begin(), keys.end(), fraction, [](double value, const auto& key) {
        return value < key.fraction;
    });
    if (next == keys.end()) {
        return keys.back().color;
    }
    if (next == keys.begin()) {
        return next->color;
    }
    const auto& previous = *(next - 1);
    const auto t = (fraction - previous.fraction) / (next->fraction - previous.fraction);
    uint32_t result = 0;
    for (unsigned shift = 0; shift < 32; shift += 8) {
        result |= uint32_t(std::lround(
                      std::lerp(double((previous.color >> shift) & 255), double((next->color >> shift) & 255), t)))
                  << shift;
    }
    return result;
}

auto ParticleEmitter::spawn(uint32_t count) -> void {
    for (uint32_t i = 0; i < count; ++i) {
        if (particles_.size() == settings_.maximum) {
            counters_.dropped += count - i;
            break;
        }
        Point p;
        if (settings_.shape == EmissionShape::Line) {
            const double t = unit() - .5;
            p = {settings_.extent.width * t, settings_.extent.height * t};
        } else if (settings_.shape == EmissionShape::Rectangle) {
            p = {settings_.extent.width * (unit() - .5), settings_.extent.height * (unit() - .5)};
        } else if (settings_.shape == EmissionShape::Circle) {
            const double angle = unit() * 2 * std::numbers::pi, radius = std::sqrt(unit()) * .5;
            p = {std::cos(angle) * radius * settings_.extent.width, std::sin(angle) * radius * settings_.extent.height};
        }
        if (settings_.space == ParticleSpace::World) {
            p.x += origin_.x;
            p.y += origin_.y;
        }
        const double angle = (settings_.direction + (unit() - .5) * settings_.spread) * std::numbers::pi / 180;
        const double speed = std::lerp(settings_.minimumSpeed, settings_.maximumSpeed, unit());
        const double life = std::lerp(settings_.minimumLife, settings_.maximumLife, unit());
        Particle particle{p, p, {std::cos(angle) * speed, std::sin(angle) * speed}, 0, life, nextId_++};
        if (dynamics_) {
            dynamics_->create({particle.id, p, particle.velocity});
        }
        particles_.push_back(particle);
        ++counters_.emitted;
    }
}

auto ParticleEmitter::simulate() -> void {
    // Validate all external acceleration before modifying the simulation state.
    winds_.clear();
    for (const auto& particle : particles_) {
        auto value = wind_ ? wind_(particle.position, elapsed_) : Point{};
        point(value);
        winds_.push_back({settings_.gravity.x + value.x, settings_.gravity.y + value.y});
    }
    motions_.clear();
    if (dynamics_) {
        for (const auto& particle : particles_) {
            motions_.push_back({particle.id, particle.position, particle.velocity, particle.rotation});
        }
        dynamics_->advance(motions_, winds_, step_);
    }
    size_t index = 0;
    for (auto& particle : particles_) {
        const auto acceleration = winds_[index];
        particle.previous = particle.position;
        particle.previousRotation = particle.rotation;
        if (dynamics_) {
            const auto& motion = motions_[index];
            particle.position = motion.position;
            particle.velocity = motion.velocity;
            particle.rotation = motion.rotation;
        } else {
            particle.position.x += particle.velocity.x * step_ + acceleration.x * step_ * step_ * .5;
            particle.position.y += particle.velocity.y * step_ + acceleration.y * step_ * step_ * .5;
            particle.velocity.x += acceleration.x * step_;
            particle.velocity.y += acceleration.y * step_;
        }
        particle.age += step_;
        ++index;
    }
    std::erase_if(particles_, [this](const auto& particle) {
        const bool expired = particle.age >= particle.life;
        if (expired && dynamics_) {
            dynamics_->erase(particle.id);
        }
        return expired;
    });
    if (emitting_) {
        emission_ += settings_.rate * step_;
        const auto count = uint32_t(emission_ + 1e-12);
        emission_ = std::max(0., emission_ - count);
        spawn(count);
    }
    elapsed_ += step_;
}

ParticleEmitter::ParticleEmitter(ParticleSettings settings, std::unique_ptr<ParticleDynamics> dynamics)
    : settings_(std::move(settings)), random_(settings_.seed), dynamics_(std::move(dynamics)) {
    if (static_cast<unsigned>(settings_.shape) > 3 || static_cast<unsigned>(settings_.space) > 1 ||
        !settings_.maximum || settings_.maximum > 1024 || !settings_.atlasCells || settings_.atlasCells > 256 ||
        (settings_.atlas && settings_.atlas->width() % settings_.atlasCells)) {
        throw std::invalid_argument("Invalid particle capacity or atlas");
    }
    validate(settings_.rate, 0, 4096);
    validate(settings_.minimumLife, .01, 3600);
    validate(settings_.maximumLife, settings_.minimumLife, 3600);
    validate(settings_.minimumSpeed, 0, 10000);
    validate(settings_.maximumSpeed, settings_.minimumSpeed, 10000);
    validate(settings_.direction, -36000, 36000);
    validate(settings_.spread, 0, 360);
    point(settings_.gravity);
    validate(settings_.extent.width, 0, 100000);
    validate(settings_.extent.height, 0, 100000);
    if (settings_.size.duration() > 1 || settings_.opacity.duration() > 1 || settings_.colors.empty() ||
        settings_.colors.size() > 256 || settings_.colors.front().fraction != 0) {
        throw std::invalid_argument("Particle curves use normalized lifetime 0..1");
    }
    double previous = -1;
    for (const auto& key : settings_.colors) {
        validate(key.fraction, 0, 1);
        if (key.fraction <= previous) {
            throw std::invalid_argument("Invalid particle color curve");
        }
        previous = key.fraction;
    }
    particles_.reserve(settings_.maximum);
    winds_.reserve(settings_.maximum);
    motions_.reserve(settings_.maximum);
}
}  // namespace cybercanvas
