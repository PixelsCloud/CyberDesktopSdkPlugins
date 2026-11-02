#include "plugins/streetlink-desk/PhysicsBackground.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <span>
#include <stdexcept>
#include <type_traits>

#include "plugins/beat-machine/Model.h"
#include "sdk/cpp/Background.h"

namespace streetlink {
namespace {
enum Command : uint32_t { Advance = 1, Spawn, Kick, BeginDrag, Drag, CancelDrag };

struct Definition {
    uint64_t generation = 1;
    bool clock = false;
    unsigned count = 36;
    std::vector<Rect> chambers;
    std::vector<std::vector<Point>> lanes;
};

struct AdvanceValue {
    double time, phase;
    beatmachine::Signal signal;
    bool audioActive;
};

struct SpawnValue {
    unsigned index;
    bool preload;
};

struct KickValue {
    double strength, phase;
};

struct DragValue {
    uint64_t index;
    ph::Body body;
    Point point;
};

struct Frame {
    uint32_t count = 0;
    bool active = false;
    ph::PhysicsStats statistics;
    std::array<PhysicsModel::Particle, 72> particles;
    std::array<ph::BodyState, 72> states;
};

static_assert(std::is_trivially_copyable_v<Frame> && sizeof(Frame) <= 65536);

template <class Value>
Value value(const CdBackgroundCommand& command) {
    if (command.size != sizeof(Value) || !command.data) {
        throw std::invalid_argument("Invalid Streetlink physics command");
    }
    Value result{};
    std::memcpy(&result, command.data, sizeof(result));
    return result;
}
}  // namespace

class PhysicsBackgroundState final : public cdsdk::BackgroundModel {
public:
    std::atomic<std::shared_ptr<const Definition>> definition;

    CdResult execute(const CdBackgroundWork& work, CdBackgroundOutput& output) override {
        if (!model_ || (work.flags & CD_BACKGROUND_RESET)) {
            const auto config = definition.load();
            if (config->generation != work.generation) {
                return CD_OK;
            }
            model_ = std::make_unique<PhysicsModel>();
            model_->clock = config->clock;
            model_->chambers = config->chambers;
            model_->lanes = config->lanes;
            model_->reset(config->count);
            processedTime_ = 0;
        }
        if (work.flags & CD_BACKGROUND_SUSPEND) {
            model_->cancelDrag();
            processedTime_ = 0;
            return CD_OK;
        }
        for (uint32_t index = 0; index < work.command_count; ++index) {
            const auto& command = work.commands[index];
            switch (command.kind) {
                case Advance: {
                    const auto intent = value<AdvanceValue>(command);
                    if (std::isfinite(intent.time) && intent.time >= processedTime_) {
                        model_->advancePhysics(intent.time - processedTime_, intent.signal, intent.audioActive,
                                               intent.phase);
                        processedTime_ = intent.time;
                    }
                    break;
                }
                case Spawn: {
                    const auto spawn = value<SpawnValue>(command);
                    model_->spawn(spawn.index, spawn.preload);
                    break;
                }
                case Kick: {
                    const auto kick = value<KickValue>(command);
                    model_->kick(kick.strength, kick.phase);
                    break;
                }
                case BeginDrag: {
                    const auto drag = value<DragValue>(command);
                    if (drag.index < model_->particles.size() &&
                        model_->particles[size_t(drag.index)].body == drag.body) {
                        model_->beginDrag(size_t(drag.index), drag.point);
                    }
                    break;
                }
                case Drag:
                    model_->drag(value<Point>(command));
                    break;
                case CancelDrag:
                    model_->cancelDrag();
                    break;
                default:
                    return CD_ERROR_ARGUMENT;
            }
        }
        Frame frame{};
        frame.count = uint32_t(model_->particles.size());
        if (frame.count > frame.particles.size()) {
            return CD_ERROR_BUFFER;
        }
        frame.active = model_->active();
        frame.statistics = model_->stats();
        std::copy(model_->particles.begin(), model_->particles.end(), frame.particles.begin());
        for (uint32_t i = 0; i < frame.count; ++i) {
            frame.states[i] = model_->state(frame.particles[i].body);
        }
        if (output.capacity < sizeof(frame)) {
            return CD_ERROR_BUFFER;
        }
        std::memcpy(output.data, &frame, sizeof(frame));
        output.size = sizeof(frame);
        output.flags = 0;
        return CD_OK;
    }

    void dispose() noexcept override {
        model_.reset();
    }

private:
    std::unique_ptr<PhysicsModel> model_;
    double processedTime_ = 0;
};

class PhysicsBackground::Impl {
public:
    explicit Impl(const CdCreateContext& context) : channel(context) {}

    Definition definition;
    std::shared_ptr<PhysicsBackgroundState> state = std::make_shared<PhysicsBackgroundState>();
    cdsdk::BackgroundChannel channel;
    Frame frame{};
    bool started = false;
    double desiredTime = 0;

    template <class Value>
    void send(uint32_t command, const Value& data, bool latest = false) {
        channel.submit(command, std::as_bytes(std::span(&data, 1)), latest, command);
    }
};

PhysicsBackground::PhysicsBackground(const CdCreateContext& context) : impl_(std::make_unique<Impl>(context)) {}

PhysicsBackground::~PhysicsBackground() = default;

void PhysicsBackground::reset(const PhysicsModel& view, unsigned count) {
    auto& self = *impl_;
    auto& config = self.definition;
    config.generation = self.channel.generation() + (self.started ? 1 : 0);
    config.clock = view.clock;
    config.count = count;
    config.chambers = view.chambers;
    config.lanes = view.lanes;
    self.state->definition.store(std::make_shared<const Definition>(config));
    if (!self.started) {
        self.channel.create(self.state, sizeof(Frame));
        self.started = true;
    } else {
        self.channel.restart();
    }
    self.frame = {};
    self.desiredTime = 0;
}

void PhysicsBackground::consume(PhysicsModel& view) {
    auto& self = *impl_;
    Frame frame{};
    if (self.channel.consume(&frame, sizeof(Frame))) {
        if (frame.count > frame.particles.size()) {
            throw std::runtime_error("Invalid Streetlink snapshot");
        }
        view.particles.assign(frame.particles.begin(), frame.particles.begin() + frame.count);
        self.frame = frame;
    }
}

void PhysicsBackground::pause(bool paused) {
    const bool changed = impl_->channel.paused() != paused;
    impl_->channel.pause(paused);
    if (changed && paused) {
        impl_->desiredTime = 0;
    }
}

void PhysicsBackground::spawn(unsigned index, bool preload) {
    impl_->send(Spawn, SpawnValue{index, preload});
}

void PhysicsBackground::advance(double dt, const beatmachine::Signal& signal, bool audioActive, double phase) {
    auto& self = *impl_;
    if (self.channel.paused() || !std::isfinite(dt) || dt < 0) {
        return;
    }
    self.desiredTime += std::min(dt, .1);
    self.send(Advance, AdvanceValue{self.desiredTime, phase, signal, audioActive}, true);
}

void PhysicsBackground::kick(double strength, double phase) {
    impl_->send(Kick, KickValue{strength, phase});
}

bool PhysicsBackground::beginDrag(size_t index, Point point) {
    const auto& frame = impl_->frame;
    if (index >= frame.count) {
        return false;
    }
    impl_->send(BeginDrag, DragValue{index, frame.particles[index].body, point});
    return true;
}

void PhysicsBackground::drag(Point point) {
    impl_->send(Drag, point, true);
}

void PhysicsBackground::cancelDrag() {
    if (impl_->started) {
        impl_->channel.submit(CancelDrag);
    }
}

bool PhysicsBackground::active() const {
    return impl_->frame.active;
}

ph::BodyState PhysicsBackground::state(ph::Body body) const {
    for (uint32_t i = 0; i < impl_->frame.count; ++i) {
        if (impl_->frame.states[i].body == body) {
            return impl_->frame.states[i];
        }
    }
    return {};
}

ph::PhysicsStats PhysicsBackground::stats() const {
    return impl_->frame.statistics;
}
}  // namespace streetlink
