#include <CyberCanvas/Fluid.h>

#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

#include "src/FluidBackend.h"

namespace cybercanvas {
namespace {
constexpr double kStep = 1. / 120;

void range(double value, double low, double high) {
    if (!std::isfinite(value) || value < low || value > high) {
        throw std::invalid_argument("Fluid value outside supported range");
    }
}

FluidSettings validate(FluidSettings value) {
    if (!value.maximum || value.maximum > 4096 || !value.maximumSteps || value.maximumSteps > 30 || !value.substeps ||
        value.substeps > 16) {
        throw std::invalid_argument("Invalid fluid capacity or step budget");
    }
    range(value.radius, .5, 50);
    range(value.pixelsPerMeter, 1, 10000);
    range(value.density, 1, 20000);
    range(value.viscosity, 0, 1);
    range(value.surfaceTension, 0, 10);
    range(value.adhesion, 0, 10);
    range(value.gravity.x, -100000, 100000);
    range(value.gravity.y, -100000, 100000);
    return value;
}

using Backend = std::unique_ptr<void, decltype(&cc_fluid_destroy)>;

Backend create(const FluidSettings& value) {
    const CfSettings settings{static_cast<float>(value.radius / value.pixelsPerMeter),
                              static_cast<float>(value.density),
                              static_cast<float>(value.viscosity),
                              static_cast<float>(value.surfaceTension),
                              static_cast<float>(value.adhesion),
                              value.substeps};
    Backend result(cc_fluid_create(&settings), &cc_fluid_destroy);
    if (!result) {
        throw std::runtime_error("Salva fluid initialization failed");
    }
    return result;
}

std::vector<CfSample> convert(std::span<const FluidSample> samples, double scale) {
    std::vector<CfSample> result;
    result.reserve(samples.size());
    for (const auto& sample : samples) {
        range(sample.position.x, -1000000, 1000000);
        range(sample.position.y, -1000000, 1000000);
        range(sample.velocity.x, -10000, 10000);
        range(sample.velocity.y, -10000, 10000);
        result.push_back(
            {{static_cast<float>(sample.position.x / scale), static_cast<float>(sample.position.y / scale)},
             {static_cast<float>(sample.velocity.x / scale), static_cast<float>(sample.velocity.y / scale)}});
    }
    return result;
}
}  // namespace

struct FluidSystem::Impl {
    explicit Impl(FluidSettings options) : settings(validate(options)), backend(create(settings)) {}

    void check(int result) const {
        if (result) {
            failed = true;
            throw std::runtime_error("Salva fluid operation failed; reset required");
        }
    }

    void ready() const {
        if (failed) {
            throw std::runtime_error("Fluid solver faulted; reset required");
        }
    }

    struct Boundary {
        size_t count;
        FluidReaction reaction;
    };

    FluidSettings settings;
    Backend backend;
    std::map<uint64_t, Boundary> boundaries;
    FluidStats stats;
    uint64_t nextBoundary = 1;
    double accumulator = 0;
    bool running = true;
    mutable bool failed = false;
};

FluidSystem::FluidSystem(FluidSettings settings) : impl_(std::make_unique<Impl>(settings)) {}

FluidSystem::~FluidSystem() = default;

void FluidSystem::add(std::span<const FluidSample> particles) {
    auto& i = *impl_;
    i.ready();
    if (particles.size() > i.settings.maximum - i.stats.particles) {
        throw std::length_error("Fluid particle budget exceeded");
    }
    if (particles.empty()) {
        return;
    }
    const auto values = convert(particles, i.settings.pixelsPerMeter);
    i.check(cc_fluid_add(i.backend.get(), values.data(), static_cast<uint32_t>(values.size())));
    i.stats.particles += values.size();
    ++i.stats.revision;
}

uint64_t FluidSystem::boundary(std::span<const FluidSample> samples) {
    auto& i = *impl_;
    i.ready();
    if (samples.empty() || samples.size() > 8192 - i.stats.boundaryPoints || i.boundaries.size() == 64) {
        throw std::length_error("Fluid boundary budget exceeded");
    }
    const auto values = convert(samples, i.settings.pixelsPerMeter);
    const auto id = i.nextBoundary++;
    auto [entry, inserted] = i.boundaries.emplace(id, Impl::Boundary{values.size(), {}});
    try {
        i.check(cc_fluid_boundary(i.backend.get(), id, values.data(), static_cast<uint32_t>(values.size())));
    } catch (...) {
        i.boundaries.erase(entry);
        throw;
    }
    i.stats.boundaryPoints += values.size();
    return id;
}

void FluidSystem::updateBoundary(uint64_t id, std::span<const FluidSample> samples) {
    auto& i = *impl_;
    i.ready();
    if (samples.size() != i.boundaries.at(id).count) {
        throw std::invalid_argument("Fluid boundary sample count cannot change");
    }
    const auto values = convert(samples, i.settings.pixelsPerMeter);
    i.check(cc_fluid_boundary(i.backend.get(), id, values.data(), static_cast<uint32_t>(values.size())));
}

void FluidSystem::removeBoundary(uint64_t id) {
    auto& i = *impl_;
    i.ready();
    const auto entry = i.boundaries.find(id);
    if (entry == i.boundaries.end()) {
        return;
    }
    i.check(cc_fluid_remove_boundary(i.backend.get(), id));
    i.stats.boundaryPoints -= entry->second.count;
    i.boundaries.erase(entry);
}

FluidReaction FluidSystem::takeReaction(uint64_t id) {
    auto& reaction = impl_->boundaries.at(id).reaction;
    return std::exchange(reaction, FluidReaction{});
}

bool FluidSystem::advance(double elapsed) {
    range(elapsed, 0, 60);
    auto& i = *impl_;
    i.ready();
    if (!i.running || !i.stats.particles) {
        return false;
    }
    i.accumulator += elapsed;
    const auto requested = static_cast<uint64_t>(std::floor((i.accumulator + 1e-10) / kStep));
    const auto steps = std::min<uint64_t>(requested, i.settings.maximumSteps);
    i.stats.droppedSteps += requested - steps;
    i.accumulator = std::max(0., i.accumulator - requested * kStep);
    const auto scale = i.settings.pixelsPerMeter;
    for (uint64_t step = 0; step < steps; ++step) {
        i.check(cc_fluid_step(
            i.backend.get(), static_cast<float>(kStep),
            {static_cast<float>(i.settings.gravity.x / scale), static_cast<float>(i.settings.gravity.y / scale)}));
        for (auto& [id, boundary] : i.boundaries) {
            CfReaction reaction{};
            i.check(cc_fluid_reaction(i.backend.get(), id, &reaction));
            boundary.reaction.impulse.x += reaction.impulse.x;
            boundary.reaction.impulse.y += reaction.impulse.y;
            boundary.reaction.moment += reaction.moment;
        }
        ++i.stats.steps;
        ++i.stats.revision;
    }
    return steps != 0;
}

void FluidSystem::activity(bool visible, bool paused, bool reducedMotion) {
    const bool running = visible && !paused && !reducedMotion;
    if (impl_->running != running) {
        impl_->running = running;
        impl_->accumulator = 0;
    }
}

void FluidSystem::reset() {
    auto replacement = create(impl_->settings);
    const auto revision = impl_->stats.revision + 1;
    impl_->backend = std::move(replacement);
    impl_->boundaries.clear();
    impl_->stats = {};
    impl_->stats.revision = revision;
    impl_->accumulator = 0;
    impl_->failed = false;
}

FluidSnapshot FluidSystem::snapshot() const {
    auto& i = *impl_;
    i.ready();
    std::vector<CfSample> raw(i.stats.particles);
    if (!raw.empty()) {
        i.check(cc_fluid_copy(i.backend.get(), raw.data(), static_cast<uint32_t>(raw.size())));
    }
    FluidSnapshot result;
    result.stats = i.stats;
    result.radius = i.settings.radius;
    result.particles.reserve(raw.size());
    for (const auto& sample : raw) {
        result.particles.push_back(
            {{sample.position.x * i.settings.pixelsPerMeter, sample.position.y * i.settings.pixelsPerMeter},
             {sample.velocity.x * i.settings.pixelsPerMeter, sample.velocity.y * i.settings.pixelsPerMeter}});
    }
    return result;
}

FluidStats FluidSystem::stats() const {
    return impl_->stats;
}

const FluidSettings& FluidSystem::settings() const {
    return impl_->settings;
}

void FluidSnapshot::draw(Canvas& canvas, uint32_t color) const {
    for (const auto& particle : particles) {
        canvas.oval(particle.position.x, particle.position.y, radius, radius, color);
    }
}

struct FluidWorker::Impl {
    explicit Impl(FluidSettings options) : settings(validate(options)) {
        latest = std::make_shared<FluidSnapshot>();
        thread = std::jthread([this](std::stop_token stop) {
            run(stop);
        });
    }

    ~Impl() {
        thread.request_stop();
        wake.notify_one();
        thread.join();
    }

    void run(std::stop_token stop) {
        try {
            FluidSystem fluid(settings);
            while (!stop.stop_requested()) {
                std::function<void(FluidSystem&)> command;
                double elapsed = 0;
                bool run;
                uint64_t generation;
                {
                    std::unique_lock lock(mutex);
                    wake.wait(lock, [&] {
                        return stop.stop_requested() || changed || !commands.empty() || pending > 0;
                    });
                    if (stop.stop_requested()) {
                        return;
                    }
                    generation = epoch;
                    run = running;
                    changed = false;
                    if (!commands.empty()) {
                        command = std::move(commands.front());
                        commands.pop_front();
                    } else {
                        elapsed = std::exchange(pending, 0.);
                    }
                }
                fluid.activity(run);
                if (command) {
                    command(fluid);
                }
                if (elapsed > 0) {
                    fluid.advance(elapsed);
                }
                auto snapshot = std::make_shared<FluidSnapshot>(fluid.snapshot());
                {
                    std::lock_guard lock(mutex);
                    if (generation == epoch) {
                        latest = std::move(snapshot);
                    }
                }
            }
        } catch (const std::exception& error) {
            std::lock_guard lock(mutex);
            auto snapshot = std::make_shared<FluidSnapshot>(*latest);
            snapshot->error = error.what();
            latest = std::move(snapshot);
            failed = true;
            commands.clear();
        } catch (...) {
            std::lock_guard lock(mutex);
            auto snapshot = std::make_shared<FluidSnapshot>(*latest);
            snapshot->error = "Unknown fluid worker error";
            latest = std::move(snapshot);
            failed = true;
            commands.clear();
        }
    }

    FluidSettings settings;
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::function<void(FluidSystem&)>> commands;
    std::shared_ptr<const FluidSnapshot> latest;
    double pending = 0;
    uint64_t epoch = 0;
    bool running = true, changed = false, failed = false;
    std::jthread thread;
};

FluidWorker::FluidWorker(FluidSettings settings) : impl_(std::make_unique<Impl>(settings)) {}

FluidWorker::~FluidWorker() = default;

bool FluidWorker::submit(std::function<void(FluidSystem&)> command) {
    if (!command) {
        throw std::invalid_argument("Empty fluid worker command");
    }
    std::lock_guard lock(impl_->mutex);
    if (impl_->failed || impl_->commands.size() == 64) {
        return false;
    }
    impl_->commands.push_back(std::move(command));
    impl_->wake.notify_one();
    return true;
}

bool FluidWorker::advance(double elapsed) {
    range(elapsed, 0, 60);
    std::lock_guard lock(impl_->mutex);
    if (impl_->failed || !impl_->running) {
        return false;
    }
    impl_->pending = std::min(impl_->pending + elapsed, 60.);
    impl_->wake.notify_one();
    return true;
}

void FluidWorker::activity(bool visible, bool paused, bool reducedMotion) {
    std::lock_guard lock(impl_->mutex);
    const bool running = visible && !paused && !reducedMotion;
    if (impl_->running != running) {
        impl_->running = running;
        impl_->pending = 0;
        impl_->changed = true;
        ++impl_->epoch;
        impl_->wake.notify_one();
    }
}

std::shared_ptr<const FluidSnapshot> FluidWorker::snapshot() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->latest;
}

#ifdef CYBERCANVAS_FLUID_BOX2D
FluidBodyBinding::FluidBodyBinding(FluidSystem& fluid, physics::World& world, physics::Body body,
                                   std::span<const Point> localSamples)
    : fluid_(fluid), world_(world), body_(body), local_(localSamples.begin(), localSamples.end()) {
    boundary_ = fluid_.boundary(samples());
}

FluidBodyBinding::~FluidBodyBinding() {
    try {
        fluid_.removeBoundary(boundary_);
    } catch (...) {
        // A faulted solver/reset has already invalidated this boundary.
    }
}

std::vector<FluidSample> FluidBodyBinding::samples() const {
    const auto state = world_.state(body_);
    const auto center = world_.centerOfMass(body_);
    const auto scale = fluid_.settings().pixelsPerMeter;
    const auto cosine = std::cos(state.angle), sine = std::sin(state.angle);
    std::vector<FluidSample> result;
    result.reserve(local_.size());
    for (const auto& p : local_) {
        const Point position{state.position.x * scale + cosine * p.x - sine * p.y,
                             state.position.y * scale + sine * p.x + cosine * p.y};
        const Point offset{position.x / scale - center.x, position.y / scale - center.y};
        result.push_back({position,
                          {(state.velocity.x - state.angularVelocity * offset.y) * scale,
                           (state.velocity.y + state.angularVelocity * offset.x) * scale}});
    }
    return result;
}

void FluidBodyBinding::synchronize() {
    fluid_.updateBoundary(boundary_, samples());
}

void FluidBodyBinding::apply() {
    const auto reaction = fluid_.takeReaction(boundary_);
    const auto center = world_.centerOfMass(body_);
    world_.impulse(body_, reaction.impulse);
    world_.angularImpulse(body_, reaction.moment - center.x * reaction.impulse.y + center.y * reaction.impulse.x);
}
#endif
}  // namespace cybercanvas
