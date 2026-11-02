#include "plugins/physics-toys/Background.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "sdk/cpp/Background.h"

namespace toys {
namespace {
constexpr size_t kMaximumPieces = 16;

enum Command : uint32_t { Parameters = 1, Time, Seed, Pluck, BeginDrag, Drag, CancelDrag };

struct ParametersValue {
    double wind = 0, speed = 1;
};

struct TimeValue {
    int hour = 0, minute = 0, second = 0;
    bool twelve = false, animate = false;
};

struct DragValue {
    uint64_t index = 0;
    Point target;
};

struct Mapping {
    Point from, to;
};

struct Definition {
    uint64_t generation = 0, sourceRevision = 1;
    Kind kind = Kind::Chime;
    bool compact = false, mechanical = false;
    ModelOptions options;
    std::optional<ClockLayout> clock;
    std::optional<ChimeLayout> chime;
    std::optional<uint32_t> seed;
    std::optional<TimeValue> time;
    ParametersValue parameters;
    std::optional<std::filesystem::path> assets;
    Size fitSize;
    cybercanvas::Rect limits;
    std::vector<Mapping> mapping;
};

struct Geometry {
    uint64_t generation = 0, sourceRevision = 0;
    Size size;
    std::vector<Piece> pieces;
    std::vector<Point> registeredAnchors;
    std::vector<cybercanvas::Rect> registeredBodies;
};

struct Frame {
    uint32_t modelVersion = 1, count = 0;
    uint64_t sourceRevision = 0;
    Size size;
    physics::PhysicsStats statistics;
    bool active = false;
    std::array<physics::BodyState, kMaximumPieces> states;
    std::array<Point, kMaximumPieces> anchors, attachments;
};

static_assert(std::is_trivially_copyable_v<Frame> && sizeof(Frame) <= 65536);

template <class Value>
Value value(const CdBackgroundCommand& command) {
    if (command.size != sizeof(Value) || !command.data) {
        throw std::invalid_argument("Invalid physics-toys background command");
    }
    Value result{};
    std::memcpy(&result, command.data, sizeof(result));
    return result;
}

bool same(TimeValue first, TimeValue second) {
    return first.hour == second.hour && first.minute == second.minute && first.second == second.second &&
           first.twelve == second.twelve && first.animate == second.animate;
}
}  // namespace

// The UI only accesses definition/geometry atomics. The actual model is lazy,
// exclusively lane-owned and explicitly released there before shared-state
// destruction can happen on the UI thread.
class BackgroundModelState final : public cdsdk::BackgroundModel {
public:
    std::atomic<std::shared_ptr<const Definition>> definition;
    std::array<std::atomic<std::shared_ptr<const Geometry>>, CD_BACKGROUND_SNAPSHOT_SLOTS> geometry;

    CdResult execute(const CdBackgroundWork& work, CdBackgroundOutput& output) override {
        if (!model_ || (work.flags & (CD_BACKGROUND_INITIALIZE | CD_BACKGROUND_RESET))) {
            const auto config = definition.load();
            if (config->generation && config->generation != work.generation) {
                return CD_OK;  // Superseded initialization/reset; host rejects this generation.
            }
            model_.reset();
            if (config->chime) {
                model_ = std::make_unique<Model>(*config->chime);
            } else if (config->clock) {
                model_ = std::make_unique<Model>(config->kind, config->compact, config->mechanical, *config->clock);
            } else {
                model_ = std::make_unique<Model>(config->kind, config->compact, config->mechanical, config->options);
            }
            if (config->assets) {
                model_->configure(*config->assets);
            }
            if (!config->mapping.empty()) {
                model_->fit(config->fitSize, config->limits, [config](Point point) {
                    for (const auto& mapping : config->mapping) {
                        if (mapping.from.x == point.x && mapping.from.y == point.y) {
                            return mapping.to;
                        }
                    }
                    return point;
                });
            }
            if (config->seed) {
                model_->setWindSeed(*config->seed);
            }
            if (config->time) {
                const auto time = *config->time;
                model_->setTime(time.hour, time.minute, time.second, time.twelve, false);
            }
            parameters_ = config->parameters;
            auto shape = std::make_shared<Geometry>();
            shape->generation = work.generation;
            shape->sourceRevision = config->sourceRevision;
            shape->size = model_->size();
            shape->pieces = model_->pieces();
            shape->registeredAnchors = model_->registeredAnchors_;
            shape->registeredBodies = model_->registeredBodies_;
            if (shape->pieces.size() > kMaximumPieces) {
                throw std::length_error("Physics-toys snapshot piece budget exceeded");
            }
            currentGeometry_ = std::move(shape);
        }
        if (work.flags & CD_BACKGROUND_SUSPEND) {
            model_->cancelDrag();
            return CD_OK;
        }
        for (uint32_t index = 0; index < work.command_count; ++index) {
            const auto& command = work.commands[index];
            switch (command.kind) {
                case Parameters:
                    parameters_ = value<ParametersValue>(command);
                    break;
                case Time: {
                    const auto time = value<TimeValue>(command);
                    model_->setTime(time.hour, time.minute, time.second, time.twelve, time.animate);
                    break;
                }
                case Seed:
                    model_->setWindSeed(value<uint32_t>(command));
                    break;
                case Pluck:
                    model_->pluck();
                    break;
                case BeginDrag: {
                    const auto drag = value<DragValue>(command);
                    model_->beginDrag(size_t(drag.index), drag.target);
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
        model_->advance(double(work.elapsed_ns) / 1e9, parameters_.wind, parameters_.speed);
        const auto shape = currentGeometry_;
        geometry[output.slot].store(currentGeometry_);
        Frame frame{};
        frame.count = uint32_t(model_->pieces().size());
        frame.sourceRevision = shape->sourceRevision;
        frame.size = model_->size();
        frame.statistics = model_->stats();
        frame.active = model_->active() || (model_->kind() == Kind::Chime && parameters_.wind > 0);
        for (size_t index = 0; index < frame.count; ++index) {
            frame.states[index] = model_->state(index);
            frame.anchors[index] = model_->anchor(model_->pieces()[index]);
            frame.attachments[index] = model_->attachment(model_->pieces()[index]);
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
    std::shared_ptr<const Geometry> currentGeometry_;
    ParametersValue parameters_;
};

class Background::Impl {
public:
    explicit Impl(const CdCreateContext& context) : task(context) {
        if (context.host) {
            host = *context.host;
        }
        hostContext = context.host_context;
    }

    Definition definition;
    std::shared_ptr<BackgroundModelState> state = std::make_shared<BackgroundModelState>();
    cdsdk::BackgroundTask task;
    std::shared_ptr<const Geometry> geometry = std::make_shared<Geometry>();
    Frame frame{};
    uint64_t sequence = 0, fittedSource = 0;
    bool paused = false;
    CdResult fault = CD_OK;
    CdHostApi host{};
    void* hostContext = nullptr;

    void fail(CdResult result) {
        const auto statistics = task.stats();
        if (result == CD_ERROR_BUSY &&
            (statistics.state == CD_BACKGROUND_CANCELING || statistics.state == CD_BACKGROUND_FINISHED ||
             statistics.state == CD_BACKGROUND_RETAINED)) {
            return;
        }
        if (fault == CD_OK) {
            fault = result;
            task.pause(true);
            if (host.log) {
                constexpr char message[] =
                    "Physics Toys background operation failed; model paused (no synchronous fallback)";
                host.log(hostContext, 3, message, sizeof(message) - 1);
            }
            if (host.request_redraw) {
                host.request_redraw(hostContext);
            }
        }
    }

    void submit(uint32_t kind, std::span<const std::byte> data = {}, bool latest = false) {
        if (paused || fault != CD_OK) {
            return;
        }
        const auto result = task.submit(kind, data, latest, kind);
        if (result != CD_OK) {
            fail(result);
        }
    }

    template <class Value>
    void send(uint32_t kind, const Value& value, bool latest = false) {
        submit(kind, std::as_bytes(std::span(&value, 1)), latest);
    }

    void restart() {
        definition.generation = task.generation() + 1;
        state->definition.store(std::make_shared<const Definition>(definition));
        const auto result = task.restart({});
        if (result != CD_OK) {
            fail(result);
            return;
        }
        sequence = 0;
        frame = {};
        geometry = std::make_shared<Geometry>();
    }
};

Background::Background(const CdCreateContext& context, Kind kind, bool compact, bool mechanical, ModelOptions options,
                       const ClockLayout* clock, const ChimeLayout* chime)
    : impl_(std::make_unique<Impl>(context)) {
    auto& config = impl_->definition;
    config.kind = kind;
    config.compact = compact;
    config.mechanical = mechanical;
    config.options = options;
    if (clock) {
        config.clock = *clock;
    }
    if (chime) {
        config.chime = *chime;
    }
    impl_->state->definition.store(std::make_shared<const Definition>(config));
    const auto result = impl_->task.create(impl_->state, sizeof(Frame), 33000000);
    if (result != CD_OK) {
        impl_->fail(result);
        throw std::runtime_error("Physics Toys requires a supported host.background/v1 execution mode");
    }
}

Background::~Background() = default;

void Background::consume() {
    auto& self = *impl_;
    const auto statistics = self.task.stats();
    if (statistics.fault) {
        self.fail(statistics.fault);
    }
    if (self.fault != CD_OK) {
        throw std::runtime_error("Physics Toys background model is faulted");
    }
    auto snapshot = self.task.acquire();
    if (!snapshot || snapshot.generation() != self.task.generation() || snapshot.sequence() == self.sequence) {
        return;
    }
    if (snapshot.bytes().size() != sizeof(Frame) || snapshot.slot() >= CD_BACKGROUND_SNAPSHOT_SLOTS) {
        self.fail(CD_ERROR_VERSION);
        return;
    }
    Frame frame{};
    std::memcpy(&frame, snapshot.bytes().data(), sizeof(frame));
    const auto geometry = self.state->geometry[snapshot.slot()].load();
    if (!geometry || geometry->generation != snapshot.generation() ||
        geometry->sourceRevision != frame.sourceRevision) {
        throw std::runtime_error("Snapshot motion/geometry slot mismatch");
    }
    if (frame.modelVersion != 1 || frame.count != geometry->pieces.size() || frame.count > kMaximumPieces) {
        self.fail(CD_ERROR_VERSION);
        return;
    }
    self.frame = frame;
    self.geometry = geometry;
    self.sequence = snapshot.sequence();
}

void Background::pause(bool paused) {
    auto& self = *impl_;
    if (self.paused == paused) {
        return;
    }
    self.paused = paused;
    const auto result = self.task.pause(paused);
    if (result != CD_OK) {
        self.fail(result);
    }
    if (!paused) {
        self.send(Parameters, self.definition.parameters, true);
        if (self.definition.time) {
            auto time = *self.definition.time;
            time.animate = false;
            self.send(Time, time);
        }
    }
}

void Background::reset(bool compact) {
    impl_->definition.compact = compact;
    ++impl_->definition.sourceRevision;
    impl_->restart();
}

void Background::configure(const std::filesystem::path& assets) {
    impl_->definition.assets = assets;
    impl_->definition.mapping.clear();
    ++impl_->definition.sourceRevision;
    impl_->restart();
}

bool Background::fit(Size size, cybercanvas::Rect limits, const std::function<Point(Point)>& map) {
    auto& self = *impl_;
    auto& config = self.definition;
    if (self.geometry->registeredBodies.empty()) {
        return false;
    }
    if (self.fittedSource == self.geometry->sourceRevision &&
        std::abs(size.width - config.fitSize.width) + std::abs(size.height - config.fitSize.height) +
                std::abs(limits.x - config.limits.x) + std::abs(limits.y - config.limits.y) +
                std::abs(limits.width - config.limits.width) + std::abs(limits.height - config.limits.height) <
            .001) {
        return false;
    }
    config.fitSize = size;
    config.limits = limits;
    config.mapping.clear();
    for (const auto& point : self.geometry->registeredAnchors) {
        config.mapping.push_back({point, map(point)});
    }
    for (const auto& body : self.geometry->registeredBodies) {
        const Point point{body.x, body.y};
        config.mapping.push_back({point, map(point)});
    }
    self.fittedSource = self.geometry->sourceRevision;
    self.restart();
    return true;
}

void Background::parameters(double wind, double speed) {
    auto& parameters = impl_->definition.parameters;
    if (parameters.wind != wind || parameters.speed != speed) {
        parameters = {wind, speed};
        impl_->send(Parameters, parameters, true);
    }
}

void Background::setTime(int hour, int minute, int second, bool twelve, bool animate) {
    const TimeValue next{hour, minute, second, twelve, animate};
    auto& previous = impl_->definition.time;
    if (!previous || !same(*previous, next)) {
        previous = next;
        impl_->send(Time, next);
    }
}

void Background::setWindSeed(uint32_t seed) {
    impl_->definition.seed = seed;
    impl_->send(Seed, seed, true);
}

void Background::pluck() {
    impl_->submit(Pluck);
}

void Background::beginDrag(size_t index, Point target) {
    if (index < impl_->frame.count) {
        impl_->send(BeginDrag, DragValue{index, target});
    }
}

void Background::drag(Point target) {
    impl_->send(Drag, target, true);
}

void Background::cancelDrag() {
    impl_->submit(CancelDrag);
}

bool Background::active() const {
    return impl_->frame.active;
}

Size Background::size() const {
    return impl_->frame.size;
}

const std::vector<Piece>& Background::pieces() const {
    return impl_->geometry->pieces;
}

physics::BodyState Background::state(size_t index) const {
    return index < impl_->frame.count ? impl_->frame.states[index] : physics::BodyState{};
}

Point Background::anchor(const Piece& piece) const {
    for (size_t index = 0; index < impl_->geometry->pieces.size(); ++index) {
        if (impl_->geometry->pieces[index].body == piece.body) {
            return impl_->frame.anchors[index];
        }
    }
    return {};
}

Point Background::attachment(const Piece& piece) const {
    for (size_t index = 0; index < impl_->geometry->pieces.size(); ++index) {
        if (impl_->geometry->pieces[index].body == piece.body) {
            return impl_->frame.attachments[index];
        }
    }
    return {};
}

physics::PhysicsStats Background::stats() const {
    return impl_->frame.statistics;
}
}  // namespace toys
