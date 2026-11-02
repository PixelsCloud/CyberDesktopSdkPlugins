#include "plugins/deep-whale/BeatBackground.h"

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

namespace deep_motion {
namespace {
enum Command : uint32_t { Step = 1, BeginDrag, Drag, CancelDrag, Scatter };

struct Definition {
    uint64_t generation = 1;
    Rect area;
    unsigned count = 28;
    bool compact = false;
};

struct GeometryFrame {
    uint64_t generation = 0;
    std::vector<BeatScene::Piece> balls, pendulums;
    ph::Body ground, floor;
    Rect field;
    double floorY = 0;
};

struct StepValue {
    double time;
    beatmachine::Signal signal;
};

struct DragValue {
    ph::Body body;
    Point target;
};

struct Frame {
    ph::PhysicsStats statistics;
    bool active = false;
    double time = 0, pulse = 0, strokeAge = 0, strokeStrength = 0;
    uint64_t beat = 0, accent = 0;
    uint32_t count = 0;
    std::array<ph::BodyState, 52> states;
};

static_assert(std::is_trivially_copyable_v<Frame> && sizeof(Frame) <= 65536);

template <class Value>
Value value(const CdBackgroundCommand& command) {
    if (command.size != sizeof(Value) || !command.data) {
        throw std::invalid_argument("Invalid Deep Whale physics command");
    }
    Value result{};
    std::memcpy(&result, command.data, sizeof(result));
    return result;
}
}  // namespace

class BeatBackgroundState final : public cdsdk::BackgroundModel {
public:
    std::atomic<std::shared_ptr<const Definition>> definition;
    std::array<std::atomic<std::shared_ptr<const GeometryFrame>>, CD_BACKGROUND_SNAPSHOT_SLOTS> geometry;

    CdResult execute(const CdBackgroundWork& work, CdBackgroundOutput& output) override {
        if (!model_ || (work.flags & CD_BACKGROUND_RESET)) {
            const auto config = definition.load();
            if (config->generation != work.generation) {
                return CD_OK;
            }
            model_ = std::make_unique<BeatScene>();
            model_->resetBeat(config->area, config->count, config->compact);
            processedTime_ = 0;
            auto shape = std::make_shared<GeometryFrame>();
            shape->generation = work.generation;
            shape->balls = model_->balls;
            shape->pendulums = model_->pendulums;
            shape->ground = model_->ground;
            shape->floor = model_->floor;
            shape->field = model_->field;
            shape->floorY = model_->floorY;
            currentGeometry_ = std::move(shape);
        }
        if (work.flags & CD_BACKGROUND_SUSPEND) {
            model_->cancelDrag();
            processedTime_ = 0;
            return CD_OK;
        }
        for (uint32_t i = 0; i < work.command_count; ++i) {
            const auto& command = work.commands[i];
            switch (command.kind) {
                case Step: {
                    const auto intent = value<StepValue>(command);
                    if (std::isfinite(intent.time) && intent.time >= processedTime_) {
                        model_->stepBeat(intent.time - processedTime_, intent.signal);
                        processedTime_ = intent.time;
                    }
                    break;
                }
                case BeginDrag: {
                    const auto drag = value<DragValue>(command);
                    model_->beginDrag(drag.body, drag.target);
                    break;
                }
                case Drag:
                    model_->drag(value<Point>(command));
                    break;
                case CancelDrag:
                    model_->cancelDrag();
                    break;
                case Scatter:
                    model_->scatter(value<Point>(command));
                    break;
                default:
                    return CD_ERROR_ARGUMENT;
            }
        }
        geometry[output.slot].store(currentGeometry_);
        Frame frame{};
        frame.active = model_->active();
        frame.statistics = model_->stats();
        frame.time = model_->time;
        frame.pulse = model_->pulse;
        frame.strokeAge = model_->strokeAge;
        frame.strokeStrength = model_->strokeStrength;
        frame.beat = model_->beat;
        frame.accent = model_->accent;
        if (model_->balls.size() > 48 || model_->pendulums.size() > 3) {
            return CD_ERROR_BUFFER;
        }
        for (const auto& ball : model_->balls) {
            frame.states[frame.count++] = model_->state(ball.body);
        }
        for (const auto& pendulum : model_->pendulums) {
            frame.states[frame.count++] = model_->state(pendulum.body);
        }
        frame.states[frame.count++] = model_->state(model_->floor);
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
        for (auto& slot : geometry) {
            slot.store({});
        }
        currentGeometry_.reset();
    }

private:
    std::unique_ptr<BeatScene> model_;
    std::shared_ptr<const GeometryFrame> currentGeometry_;
    double processedTime_ = 0;
};

class BeatBackground::Impl {
public:
    explicit Impl(const CdCreateContext& context) : channel(context) {}

    cdsdk::BackgroundChannel channel;
    std::shared_ptr<BeatBackgroundState> state = std::make_shared<BeatBackgroundState>();
    std::shared_ptr<const GeometryFrame> geometry;
    Frame frame{};
    bool started = false;
    double desiredTime = 0;
    beatmachine::Signal previousSignal{};

    template <class Value>
    void send(uint32_t command, const Value& data, bool latest = false) {
        if (started) {
            channel.submit(command, std::as_bytes(std::span(&data, 1)), latest, command);
        }
    }
};

BeatBackground::BeatBackground(const CdCreateContext& context) : impl_(std::make_unique<Impl>(context)) {}

BeatBackground::~BeatBackground() = default;

void BeatBackground::reset(Rect area, unsigned count, bool compact) {
    auto& self = *impl_;
    const Definition config{self.channel.generation() + (self.started ? 1 : 0), area, std::clamp(count, 12u, 48u),
                            compact};
    self.state->definition.store(std::make_shared<const Definition>(config));
    if (!self.started) {
        self.channel.create(self.state, sizeof(Frame));
        self.started = true;
    } else {
        self.channel.restart();
    }
    self.geometry.reset();
    self.frame = {};
    self.desiredTime = 0;
    self.previousSignal = {};
}

void BeatBackground::step(double dt, const beatmachine::Signal& signal) {
    auto& self = *impl_;
    if (!self.started || self.channel.paused() || !std::isfinite(dt) || dt < 0) {
        return;
    }
    self.desiredTime += std::min(dt, .1);
    const bool event = signal.beat != self.previousSignal.beat || signal.accent != self.previousSignal.accent;
    self.send(Step, StepValue{self.desiredTime, signal}, !event);
    self.previousSignal = signal;
}

void BeatBackground::consume(BeatScene& view) {
    auto& self = *impl_;
    Frame frame{};
    auto snapshot = self.started ? self.channel.acquire() : cdsdk::BackgroundSnapshot{};
    if (!snapshot) {
        return;
    }
    std::memcpy(&frame, snapshot.bytes().data(), sizeof(frame));
    const auto geometry = self.state->geometry[snapshot.slot()].load();
    if (!geometry || geometry->generation != self.channel.generation()) {
        throw std::runtime_error("Snapshot motion/geometry slot mismatch");
    }
    if (frame.count != geometry->balls.size() + geometry->pendulums.size() + 1 || frame.count > frame.states.size()) {
        throw std::runtime_error("Invalid Deep Whale snapshot");
    }
    if (geometry != self.geometry) {
        view.balls = geometry->balls;
        view.pendulums = geometry->pendulums;
        view.ground = geometry->ground;
        view.floor = geometry->floor;
        view.field = geometry->field;
        view.floorY = geometry->floorY;
        self.geometry = geometry;
    }
    view.time = frame.time;
    view.pulse = frame.pulse;
    view.strokeAge = frame.strokeAge;
    view.strokeStrength = frame.strokeStrength;
    view.beat = frame.beat;
    view.accent = frame.accent;
    self.frame = frame;
}

void BeatBackground::pause(bool paused) {
    auto& self = *impl_;
    if (!self.started) {
        return;
    }
    const bool changed = self.channel.paused() != paused;
    self.channel.pause(paused);
    if (changed && paused) {
        self.desiredTime = 0;
        self.previousSignal = {};
    }
}

bool BeatBackground::active() const {
    return impl_->frame.active;
}

ph::PhysicsStats BeatBackground::stats() const {
    return impl_->frame.statistics;
}

ph::BodyState BeatBackground::state(ph::Body body) const {
    for (uint32_t i = 0; i < impl_->frame.count; ++i) {
        if (impl_->frame.states[i].body == body) {
            return impl_->frame.states[i];
        }
    }
    return {};
}

bool BeatBackground::beginDrag(ph::Body body, Point target) {
    const auto& frame = impl_->frame;
    const bool exists =
        std::any_of(frame.states.begin(), frame.states.begin() + frame.count, [body](const auto& state) {
            return state.body == body;
        });
    if (exists) {
        impl_->send(BeginDrag, DragValue{body, target});
    }
    return exists;
}

void BeatBackground::drag(Point target) {
    impl_->send(Drag, target, true);
}

void BeatBackground::cancelDrag() {
    if (impl_->started) {
        impl_->channel.submit(CancelDrag);
    }
}

void BeatBackground::scatter(Point point) {
    impl_->send(Scatter, point);
}
}  // namespace deep_motion
