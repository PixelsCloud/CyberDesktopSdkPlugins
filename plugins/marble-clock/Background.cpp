#include "plugins/marble-clock/Background.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <optional>
#include <span>
#include <stdexcept>
#include <type_traits>

#include "sdk/cpp/Background.h"

namespace marbleclock {
namespace {
enum Command : uint32_t { Time = 1, Release, Drain, BeginDrag, Drag, CancelDrag };

struct Definition {
    uint64_t generation = 0, geometryRevision = 0;
    bool compact = false;
    GeometryStyle style = GeometryStyle::Classic;
    int clock = -1;
    std::optional<std::filesystem::path> assets;
};

struct GeometryFrame {
    uint64_t generation = 0;
    Geometry shape;
};

struct DragValue {
    uint32_t token;
    uint64_t epoch;
    Point point;
};

struct Frame {
    uint64_t commandSequence = 0;
    uint32_t count = 0;
    bool active = false, dragging = false;
    physics::PhysicsStats statistics;
    std::array<Marble, kMaximumMarbles> marbles;
    std::array<physics::BodyState, kMaximumMarbles> states;
    double flapAngle = 0, time = 0, lastRelease = -1, clockChanged = -1;
    Phase phase = Phase::Collecting;
    int clock = -1;
    unsigned pending = 0;
    uint64_t epoch = 0, contacts = 0, recycled = 0, recoveries = 0, releases = 0;
};

static_assert(std::is_trivially_copyable_v<Frame> && sizeof(Frame) <= 65536);

template <class Value>
Value value(const CdBackgroundCommand& command) {
    if (command.size != sizeof(Value) || !command.data) {
        throw std::invalid_argument("Invalid marble-clock command");
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
            if (!model_ || geometryRevision_ != config->geometryRevision) {
                model_ = std::make_unique<Model>(config->compact, config->style);
                if (config->assets) {
                    model_->configure(*config->assets);
                }
                geometryRevision_ = config->geometryRevision;
            }
            if (config->clock >= 0 && config->clock != model_->clock_) {
                model_->setTime(config->clock / 3600, config->clock / 60 % 60, config->clock % 60, false);
            } else {
                model_->quiet();
            }
            auto shape = std::make_shared<GeometryFrame>();
            shape->generation = work.generation;
            shape->shape = model_->shape();
            currentGeometry_ = std::move(shape);
            commandSequence_ = 0;
        }
        if (work.flags & CD_BACKGROUND_SUSPEND) {
            model_->cancelDrag();
            return CD_OK;
        }
        model_->advance(double(work.elapsed_ns) / 1e9);
        for (uint32_t index = 0; index < work.command_count; ++index) {
            const auto& command = work.commands[index];
            switch (command.kind) {
                case Time: {
                    const auto clock = value<int>(command);
                    model_->setTime(clock / 3600, clock / 60 % 60, clock % 60, true);
                    break;
                }
                case Release:
                    model_->release();
                    break;
                case Drain:
                    model_->drain();
                    break;
                case BeginDrag: {
                    const auto drag = value<DragValue>(command);
                    if (drag.epoch == model_->generation_) {
                        model_->beginDrag(drag.token, drag.point);
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
            commandSequence_ = command.sequence;
        }
        geometry[output.slot].store(currentGeometry_);
        Frame frame{};
        frame.commandSequence = commandSequence_;
        frame.count = uint32_t(model_->marbles().size());
        if (frame.count > frame.marbles.size()) {
            return CD_ERROR_BUFFER;
        }
        std::copy(model_->marbles().begin(), model_->marbles().end(), frame.marbles.begin());
        for (uint32_t i = 0; i < frame.count; ++i) {
            frame.states[i] = model_->state(frame.marbles[i]);
        }
        frame.active = model_->active();
        frame.dragging = model_->dragging();
        frame.statistics = model_->stats();
        frame.flapAngle = model_->flapAngle();
        frame.time = model_->time_;
        frame.lastRelease = model_->lastRelease_;
        frame.clockChanged = model_->clockChanged_;
        frame.clock = model_->clock_;
        frame.phase = model_->phase_;
        frame.pending = model_->pending_;
        frame.epoch = model_->generation_;
        frame.contacts = model_->contacts_;
        frame.recycled = model_->recycled_;
        frame.recoveries = model_->recoveries_;
        frame.releases = model_->releases_;
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
    uint64_t geometryRevision_ = 0;
    uint64_t commandSequence_ = 0;
};

class Background::Impl {
public:
    Impl(const CdCreateContext& context, bool compact, GeometryStyle style) {
        definition.compact = compact;
        definition.style = style;
        state->definition.store(std::make_shared<const Definition>(definition));
        channel = std::make_unique<cdsdk::BackgroundChannel>(context, state, sizeof(Frame));
    }

    Definition definition;
    std::shared_ptr<BackgroundState> state = std::make_shared<BackgroundState>();
    std::unique_ptr<cdsdk::BackgroundChannel> channel;
    Frame frame{};
    std::shared_ptr<const GeometryFrame> geometry;
    bool dragWanted = false;
    uint64_t dragSequence = 0;

    template <class Value>
    uint64_t send(uint32_t command, const Value& data, bool latest = false) {
        return channel->submit(command, std::as_bytes(std::span(&data, 1)), latest, command);
    }

    void restart() {
        definition.generation = channel->generation() + 1;
        state->definition.store(std::make_shared<const Definition>(definition));
        channel->restart();
        frame = {};
        geometry.reset();
        dragWanted = false;
        dragSequence = 0;
    }
};

Background::Background(const CdCreateContext& context, bool compact, GeometryStyle style)
    : impl_(std::make_unique<Impl>(context, compact, style)) {}

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
    if (frame.count > frame.marbles.size()) {
        throw std::runtime_error("Invalid marble-clock snapshot");
    }
    self.frame = frame;
    view.marbles_.assign(frame.marbles.begin(), frame.marbles.begin() + frame.count);
    view.time_ = frame.time;
    view.lastRelease_ = frame.lastRelease;
    view.clockChanged_ = frame.clock == view.clock_ ? frame.clockChanged : frame.time;
    view.phase_ = frame.phase;
    view.pending_ = frame.pending;
    view.generation_ = frame.epoch;
    view.contacts_ = frame.contacts;
    view.recycled_ = frame.recycled;
    view.recoveries_ = frame.recoveries;
    view.releases_ = frame.releases;
    if (geometry != self.geometry) {
        view.geometry_ = geometry->shape;
        self.geometry = geometry;
    }
}

void Background::pause(bool paused) {
    if (impl_->channel->paused() == paused) {
        return;
    }
    impl_->channel->pause(paused);
    if (!paused && impl_->definition.clock >= 0) {
        impl_->send(Time, impl_->definition.clock);
    }
}

void Background::configure(const std::filesystem::path& assets) {
    impl_->definition.assets = assets;
    ++impl_->definition.geometryRevision;
    impl_->restart();
}

void Background::setTime(int clock, bool animate, bool continuous) {
    impl_->definition.clock = clock;
    if (!animate || !continuous) {
        impl_->restart();
    } else {
        impl_->send(Time, clock);
    }
}

void Background::quiet() {
    impl_->restart();
}

bool Background::release() {
    impl_->channel->submit(Release);
    return true;
}

void Background::drain() {
    impl_->channel->submit(Drain);
}

bool Background::beginDrag(uint32_t token, Point point) {
    const auto& frame = impl_->frame;
    const bool exists =
        std::any_of(frame.marbles.begin(), frame.marbles.begin() + frame.count, [token](const Marble& marble) {
            return marble.token == token;
        });
    if (exists) {
        impl_->dragSequence = impl_->send(BeginDrag, DragValue{token, frame.epoch, point});
        impl_->dragWanted = impl_->dragSequence != 0;
    }
    return exists && impl_->dragWanted;
}

void Background::drag(Point point) {
    impl_->send(Drag, point, true);
}

void Background::cancelDrag() {
    impl_->dragWanted = false;
    impl_->channel->submit(CancelDrag);
}

bool Background::active() const {
    return impl_->frame.active;
}

bool Background::dragging() const {
    return impl_->dragWanted && (impl_->frame.commandSequence < impl_->dragSequence || impl_->frame.dragging);
}

physics::BodyState Background::state(const Marble& marble) const {
    for (uint32_t i = 0; i < impl_->frame.count; ++i) {
        if (impl_->frame.marbles[i].body == marble.body && impl_->frame.marbles[i].token == marble.token) {
            return impl_->frame.states[i];
        }
    }
    return {};
}

double Background::flapAngle() const {
    return impl_->frame.flapAngle;
}

physics::PhysicsStats Background::stats() const {
    return impl_->frame.statistics;
}
}  // namespace marbleclock
