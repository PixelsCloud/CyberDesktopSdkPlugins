#include "plugins/brick-clock/Background.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <span>
#include <stdexcept>
#include <type_traits>

#include "sdk/cpp/Background.h"

namespace brickclock {
namespace {
enum Command : uint32_t { Time = 1, Replay, ChipCount };

struct Definition {
    uint64_t generation = 0;
    EnclosureGeometry enclosure;
    uint32_t seed = kDefaultSeed;
    int clock = -1;
    unsigned chipCount = 12;
};

struct GeometryFrame {
    uint64_t generation = 0, revision = 0;
    std::vector<Chip> chips;
    std::array<std::vector<Point>, 6> samples;
};

struct CarvingFrame {
    int previous = 0;
    double elapsed = 1;
    size_t emitted = 0;
};

struct Frame {
    uint64_t geometryRevision = 0, contacts = 0;
    uint32_t count = 0;
    int clock = -1;
    bool active = false;
    double time = 0;
    physics::PhysicsStats statistics;
    std::array<CarvingFrame, 6> carvings;
    std::array<physics::BodyState, kMaximumChips> states;
};

static_assert(std::is_trivially_copyable_v<Frame> && sizeof(Frame) <= 65536);

template <class Value>
Value value(const CdBackgroundCommand& command) {
    if (command.size != sizeof(Value) || !command.data) {
        throw std::invalid_argument("Invalid brick-clock command");
    }
    Value result{};
    std::memcpy(&result, command.data, sizeof(result));
    return result;
}
}  // namespace

class BackgroundState final : public cdsdk::BackgroundModel {
public:
    std::atomic<std::shared_ptr<const Definition>> definition;
    std::array<std::atomic<std::shared_ptr<const GeometryFrame>>, CD_BACKGROUND_SNAPSHOT_SLOTS> geometry;

    CdResult execute(const CdBackgroundWork& work, CdBackgroundOutput& output) override {
        bool shapeChanged = false;
        if (!model_ || (work.flags & CD_BACKGROUND_RESET)) {
            const auto config = definition.load();
            if (config->generation && config->generation != work.generation) {
                return CD_OK;
            }
            if (!model_) {
                model_ = std::make_unique<Model>(config->enclosure, config->seed);
            }
            model_->setChipCount(config->chipCount);
            model_->settle();
            if (config->clock >= 0) {
                model_->setTime(config->clock / 3600, config->clock / 60 % 60, config->clock % 60, false);
            }
            shapeChanged = true;
        }
        if (work.flags & CD_BACKGROUND_SUSPEND) {
            return CD_OK;
        }
        model_->advance(double(work.elapsed_ns) / 1e9);
        for (uint32_t index = 0; index < work.command_count; ++index) {
            const auto& command = work.commands[index];
            switch (command.kind) {
                case Time: {
                    const auto clock = value<int>(command);
                    model_->setTime(clock / 3600, clock / 60 % 60, clock % 60, true);
                    shapeChanged = true;
                    break;
                }
                case Replay:
                    model_->replay();
                    shapeChanged = true;
                    break;
                case ChipCount:
                    model_->setChipCount(value<unsigned>(command));
                    break;
                default:
                    return CD_ERROR_ARGUMENT;
            }
        }
        auto shape = currentGeometry_;
        shapeChanged |= !shape || shape->generation != work.generation || shape->chips.size() != model_->chips().size();
        if (!shapeChanged) {
            for (size_t i = 0; i < model_->chips().size(); ++i) {
                shapeChanged |= shape->chips[i].body != model_->chips()[i].body;
            }
        }
        if (shapeChanged) {
            auto next = std::make_shared<GeometryFrame>();
            next->generation = work.generation;
            next->revision = ++revision_;
            next->chips = model_->chips();
            for (size_t i = 0; i < next->samples.size(); ++i) {
                next->samples[i] = model_->carvings()[i].samples;
            }
            shape = std::move(next);
            currentGeometry_ = shape;
        }
        geometry[output.slot].store(shape);
        Frame frame{};
        frame.geometryRevision = shape->revision;
        frame.count = uint32_t(model_->chips().size());
        if (frame.count > frame.states.size()) {
            return CD_ERROR_BUFFER;
        }
        frame.active = model_->active();
        frame.statistics = model_->stats();
        frame.contacts = model_->contacts();
        frame.time = model_->time_;
        frame.clock = model_->previousSecond_;
        for (size_t i = 0; i < frame.carvings.size(); ++i) {
            const auto& carving = model_->carvings()[i];
            frame.carvings[i] = {carving.previous, carving.elapsed, carving.emitted};
        }
        for (uint32_t i = 0; i < frame.count; ++i) {
            frame.states[i] = model_->state(model_->chips()[i]);
        }
        if (output.capacity < sizeof(frame)) {
            return CD_ERROR_BUFFER;
        }
        std::memcpy(output.data, &frame, sizeof(frame));
        output.size = sizeof(frame);
        output.flags = frame.active ? CD_BACKGROUND_ACTIVE : 0;
        return CD_OK;
    }

    void dispose() noexcept override {
        model_.reset();
        for (auto& slot : geometry) {
            slot.store({});
        }
        currentGeometry_.reset();
    }

private:
    std::unique_ptr<Model> model_;
    std::shared_ptr<const GeometryFrame> currentGeometry_;
    uint64_t revision_ = 0;
};

class Background::Impl {
public:
    Impl(const CdCreateContext& context, EnclosureGeometry enclosure, uint32_t seed) {
        definition.enclosure = enclosure;
        definition.seed = seed;
        state->definition.store(std::make_shared<const Definition>(definition));
        channel = std::make_unique<cdsdk::BackgroundChannel>(context, state, sizeof(Frame));
    }

    Definition definition;
    std::shared_ptr<BackgroundState> state = std::make_shared<BackgroundState>();
    std::unique_ptr<cdsdk::BackgroundChannel> channel;
    Frame frame{};
    std::shared_ptr<const GeometryFrame> geometry = std::make_shared<GeometryFrame>();

    template <class Value>
    void send(uint32_t command, const Value& data, bool latest = false) {
        channel->submit(command, std::as_bytes(std::span(&data, 1)), latest, command);
    }

    void restart() {
        definition.generation = channel->generation() + 1;
        state->definition.store(std::make_shared<const Definition>(definition));
        channel->restart();
        frame = {};
        geometry = std::make_shared<GeometryFrame>();
    }
};

Background::Background(const CdCreateContext& context, EnclosureGeometry enclosure, uint32_t seed)
    : impl_(std::make_unique<Impl>(context, enclosure, seed)) {}

Background::~Background() = default;

void Background::consume(Model& view) {
    auto& self = *impl_;
    Frame frame{};
    auto snapshot = self.channel->acquire();
    if (!snapshot) {
        return;
    }
    std::memcpy(&frame, snapshot.bytes().data(), sizeof(frame));
    const auto geometry = self.state->geometry[snapshot.slot()].load();
    if (!geometry || geometry->generation != self.channel->generation() ||
        geometry->revision != frame.geometryRevision) {
        throw std::runtime_error("Snapshot motion/geometry slot mismatch");
    }
    if (frame.count != geometry->chips.size() || frame.count > frame.states.size()) {
        throw std::runtime_error("Invalid brick-clock snapshot");
    }
    view.time_ = frame.time;
    view.contacts_ = frame.contacts;
    for (size_t i = 0; i < frame.carvings.size(); ++i) {
        const auto& source = frame.carvings[i];
        auto& destination = view.carvings_[i];
        destination.previous = source.previous;
        destination.elapsed = frame.clock == view.previousSecond_ ? source.elapsed : 0;
        destination.emitted = source.emitted;
        if (geometry != self.geometry) {
            destination.samples = geometry->samples[i];
        }
    }
    self.frame = frame;
    self.geometry = geometry;
}

void Background::pause(bool paused) {
    const bool changed = impl_->channel->paused() != paused;
    impl_->channel->pause(paused);
    if (changed && !paused) {
        impl_->send(ChipCount, impl_->definition.chipCount, true);
    }
}

void Background::setTime(int clock, bool animate, bool continuous) {
    impl_->definition.clock = clock;
    if (!animate || !continuous) {
        impl_->restart();
    } else {
        impl_->send(Time, clock);
    }
}

void Background::settle() {
    impl_->restart();
}

void Background::replay() {
    impl_->channel->submit(Replay);
}

void Background::setChipCount(unsigned count) {
    impl_->definition.chipCount = std::clamp(count, 8u, 20u);
    impl_->send(ChipCount, impl_->definition.chipCount, true);
}

bool Background::active() const {
    return impl_->frame.active;
}

const std::vector<Chip>& Background::chips() const {
    return impl_->geometry->chips;
}

physics::BodyState Background::state(const Chip& chip) const {
    const auto& frame = impl_->frame;
    for (uint32_t i = 0; i < frame.count; ++i) {
        if (frame.states[i].body == chip.body) {
            return frame.states[i];
        }
    }
    return {};
}

physics::PhysicsStats Background::stats() const {
    return impl_->frame.statistics;
}
}  // namespace brickclock
