#include "plugins/beat-machine/Background.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <optional>
#include <span>
#include <stdexcept>
#include <type_traits>

#include "sdk/cpp/Background.h"

namespace beatmachine {
namespace {
enum Command : uint32_t { Advance = 1, Strike, Nudge, Quiet, BeginDrag, Drag, CancelDrag };

struct Definition {
    uint64_t generation = 0;
    unsigned count = 36;
    bool compact = false;
    GeometryStyle style = GeometryStyle::Classic;
    double radius = kRadius;
    std::optional<std::filesystem::path> assets;
};

struct GeometryFrame {
    uint64_t generation = 0;
    std::vector<Segment> rails;
    std::array<Point, 3> bumpers;
};

struct AdvanceValue {
    double time;
    Signal signal;
};

struct StrikeValue {
    unsigned index;
    double strength;
};

struct DragValue {
    uint32_t token;
    Point point;
};

struct Frame {
    uint32_t count = 0;
    bool active = false, dragging = false;
    physics::PhysicsStats statistics;
    std::array<Ball, 48> balls;
    std::array<physics::BodyState, 55> states;
    std::array<Actuator, 5> pistons;
    std::array<Actuator, 2> paddles;
    std::array<double, 3> flashes;
    double time = 0;
    uint64_t launches = 0, ejections = 0, returns = 0, hits = 0, recoveries = 0;
};

static_assert(std::is_trivially_copyable_v<Frame> && sizeof(Frame) <= 65536);

template <class Value>
Value value(const CdBackgroundCommand& command) {
    if (command.size != sizeof(Value) || !command.data) {
        throw std::invalid_argument("Invalid beat-machine command");
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
        if (!model_ || (work.flags & CD_BACKGROUND_RESET)) {
            const auto config = definition.load();
            if (config->generation && config->generation != work.generation) {
                return CD_OK;
            }
            model_ = std::make_unique<Model>(config->count, config->compact, config->style, config->radius);
            if (config->assets) {
                model_->configure(*config->assets);
                model_->reset(config->count);
            }
            auto shape = std::make_shared<GeometryFrame>();
            shape->generation = work.generation;
            shape->rails = model_->rails();
            shape->bumpers = model_->bumpers();
            currentGeometry_ = std::move(shape);
            processedTime_ = 0;
        }
        if (work.flags & CD_BACKGROUND_SUSPEND) {
            model_->quiet();
            processedTime_ = 0;
            return CD_OK;
        }
        for (uint32_t index = 0; index < work.command_count; ++index) {
            const auto& command = work.commands[index];
            switch (command.kind) {
                case Advance: {
                    const auto intent = value<AdvanceValue>(command);
                    if (std::isfinite(intent.time) && intent.time >= processedTime_) {
                        model_->advance(intent.time - processedTime_, intent.signal);
                        processedTime_ = intent.time;
                    }
                    break;
                }
                case Strike: {
                    const auto strike = value<StrikeValue>(command);
                    model_->strike(strike.index, strike.strength);
                    break;
                }
                case Nudge:
                    model_->nudge(value<unsigned>(command));
                    break;
                case Quiet:
                    model_->quiet();
                    break;
                case BeginDrag: {
                    const auto drag = value<DragValue>(command);
                    model_->beginDrag(drag.token, drag.point);
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
        geometry[output.slot].store(currentGeometry_);
        Frame frame{};
        frame.count = uint32_t(model_->balls().size());
        if (frame.count > frame.balls.size()) {
            return CD_ERROR_BUFFER;
        }
        frame.active = model_->active();
        frame.dragging = model_->dragging();
        frame.statistics = model_->stats();
        std::copy(model_->balls().begin(), model_->balls().end(), frame.balls.begin());
        frame.pistons = model_->pistons();
        frame.paddles = model_->paddles();
        frame.flashes = model_->flashes();
        for (uint32_t i = 0; i < frame.count; ++i) {
            frame.states[i] = model_->state(frame.balls[i].body);
        }
        for (unsigned i = 0; i < 5; ++i) {
            frame.states[48 + i] = model_->state(frame.pistons[i].body);
        }
        for (unsigned i = 0; i < 2; ++i) {
            frame.states[53 + i] = model_->state(frame.paddles[i].body);
        }
        frame.time = model_->time();
        frame.launches = model_->launches();
        frame.ejections = model_->ejections();
        frame.returns = model_->returns();
        frame.hits = model_->hits();
        frame.recoveries = model_->recoveries();
        if (output.capacity < sizeof(frame)) {
            return CD_ERROR_BUFFER;
        }
        std::memcpy(output.data, &frame, sizeof(frame));
        output.size = sizeof(frame);
        // Only lightweight audio/business checkpoints drive this model. The
        // deadline lane computes them; UI rendering never advances a World.
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
    std::unique_ptr<Model> model_;
    std::shared_ptr<const GeometryFrame> currentGeometry_;
    double processedTime_ = 0;
};

class Background::Impl {
public:
    Impl(const CdCreateContext& context, Definition config) : definition(std::move(config)) {
        state->definition.store(std::make_shared<const Definition>(definition));
        channel = std::make_unique<cdsdk::BackgroundChannel>(context, state, sizeof(Frame));
    }

    Definition definition;
    std::shared_ptr<BackgroundState> state = std::make_shared<BackgroundState>();
    std::unique_ptr<cdsdk::BackgroundChannel> channel;
    Frame frame{};
    std::shared_ptr<const GeometryFrame> geometry;
    Signal previousSignal{};
    double desiredTime = 0;

    template <class Value>
    void send(uint32_t command, const Value& data, bool latest = false) {
        channel->submit(command, std::as_bytes(std::span(&data, 1)), latest, command);
    }

    void restart() {
        definition.generation = channel->generation() + 1;
        state->definition.store(std::make_shared<const Definition>(definition));
        channel->restart();
        desiredTime = 0;
        frame = {};
        geometry.reset();
        previousSignal = {};
    }
};

Background::Background(const CdCreateContext& context, unsigned count, bool compact, GeometryStyle style, double radius)
    : impl_(std::make_unique<Impl>(context, Definition{0, count, compact, style, radius})) {}

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
    if (!geometry || geometry->generation != self.channel->generation()) {
        throw std::runtime_error("Snapshot motion/geometry slot mismatch");
    }
    if (frame.count > frame.balls.size()) {
        throw std::runtime_error("Invalid beat-machine snapshot");
    }
    self.frame = frame;
    view.balls_.assign(frame.balls.begin(), frame.balls.begin() + frame.count);
    view.pistons_ = frame.pistons;
    view.paddles_ = frame.paddles;
    view.flashes_ = frame.flashes;
    view.time_ = frame.time;
    view.launches_ = frame.launches;
    view.ejections_ = frame.ejections;
    view.returns_ = frame.returns;
    view.hits_ = frame.hits;
    view.recoveries_ = frame.recoveries;
    if (geometry != self.geometry) {
        view.rails_ = geometry->rails;
        view.bumpers_ = geometry->bumpers;
        self.geometry = geometry;
    }
}

void Background::pause(bool paused) {
    const bool changed = impl_->channel->paused() != paused;
    impl_->channel->pause(paused);
    if (changed && paused) {
        impl_->desiredTime = 0;
        impl_->previousSignal = {};
    }
}

void Background::configure(const std::filesystem::path& assets) {
    impl_->definition.assets = assets;
    impl_->definition.count = 36;
    impl_->restart();
}

void Background::reset(unsigned count) {
    impl_->definition.count = std::clamp(count, 12u, 48u);
    impl_->restart();
}

void Background::advance(double seconds, const Signal& signal) {
    auto& self = *impl_;
    if (self.channel->paused() || !std::isfinite(seconds) || seconds < 0) {
        return;
    }
    self.desiredTime += std::min(seconds, .1);
    const bool event = signal.beat != self.previousSignal.beat || signal.accent != self.previousSignal.accent ||
                       signal.pistonBeat != self.previousSignal.pistonBeat;
    self.send(Advance, AdvanceValue{self.desiredTime, signal}, !event);
    self.previousSignal = signal;
}

void Background::strike(unsigned index, double strength) {
    impl_->send(Strike, StrikeValue{index, strength});
}

void Background::nudge(unsigned index) {
    impl_->send(Nudge, index);
}

void Background::quiet() {
    impl_->channel->submit(Quiet);
}

bool Background::beginDrag(uint32_t token, Point point) {
    const auto& frame = impl_->frame;
    const bool exists = std::any_of(frame.balls.begin(), frame.balls.begin() + frame.count, [token](const Ball& ball) {
        return ball.token == token;
    });
    if (exists) {
        impl_->send(BeginDrag, DragValue{token, point});
    }
    return exists;
}

void Background::drag(Point point) {
    impl_->send(Drag, point, true);
}

void Background::cancelDrag() {
    impl_->channel->submit(CancelDrag);
}

bool Background::active() const {
    return impl_->frame.active;
}

bool Background::dragging() const {
    return impl_->frame.dragging;
}

physics::BodyState Background::state(physics::Body body) const {
    for (const auto& state : impl_->frame.states) {
        if (state.body == body) {
            return state;
        }
    }
    return {};
}

physics::PhysicsStats Background::stats() const {
    return impl_->frame.statistics;
}
}  // namespace beatmachine
