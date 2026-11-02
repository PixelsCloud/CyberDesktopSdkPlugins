#include "plugins/hourglass-suite/SandBackground.h"

#include <array>
#include <cmath>
#include <cstring>
#include <span>
#include <stdexcept>
#include <type_traits>

#include "plugins/hourglass-suite/PhysicalSand.h"
#include "sdk/cpp/Background.h"

namespace hourglassplugin {
namespace {
struct TimerCommand {
    double time, seconds;
    SandTiming timer;
    bool projectedFlip;
};

struct SandFrame {
    uint32_t count = 0, released = 0;
    cybercanvas::physics::PhysicsStats statistics;
    std::array<cybercanvas::Point, 128> positions;
};

static_assert(std::is_trivially_copyable_v<SandFrame> && sizeof(SandFrame) <= 65536);
}  // namespace

class SandBackgroundState final : public cdsdk::BackgroundModel {
public:
    CdResult execute(const CdBackgroundWork& work, CdBackgroundOutput& output) override {
        if (!model_ || (work.flags & CD_BACKGROUND_RESET)) {
            model_ = std::make_unique<PhysicalSand>();
            processedTime_ = 0;
        }
        if (work.flags & CD_BACKGROUND_SUSPEND) {
            processedTime_ = 0;
            return CD_OK;
        }
        for (uint32_t i = 0; i < work.command_count; ++i) {
            const auto& command = work.commands[i];
            if (command.kind != 1 || command.size != sizeof(TimerCommand) || !command.data) {
                return CD_ERROR_ARGUMENT;
            }
            TimerCommand timer{};
            std::memcpy(&timer, command.data, sizeof(timer));
            if (!std::isfinite(timer.time) || timer.time < processedTime_) {
                return CD_ERROR_ARGUMENT;
            }
            const double accumulated = timer.time - processedTime_;
            // Preserve the original duration exactly in the usual uncoalesced
            // path. Coalesced continuous checkpoints use their total duration.
            const double seconds = std::abs(accumulated - timer.seconds) < 1e-9 ? timer.seconds : accumulated;
            model_->advanceTiming(seconds, timer.timer, timer.projectedFlip);
            processedTime_ = timer.time;
        }
        SandFrame frame{};
        frame.count = uint32_t(model_->count());
        frame.released = uint32_t(model_->released());
        frame.statistics = model_->stats();
        if (frame.count > frame.positions.size()) {
            return CD_ERROR_BUFFER;
        }
        const double cosine = std::cos(model_->angle_), sine = std::sin(model_->angle_);
        for (uint32_t i = 0; i < frame.count; ++i) {
            const auto p = model_->world_->state(model_->grains_[i].body).position;
            const double x = p.x - .9415, y = p.y - 1.4415;
            frame.positions[i] = {(x * cosine + y * sine + .9415) * 10, (-x * sine + y * cosine + 1.4415) * 10};
        }
        if (output.capacity < sizeof(frame)) {
            return CD_ERROR_BUFFER;
        }
        std::memcpy(output.data, &frame, sizeof(frame));
        output.size = sizeof(frame);
        // UI owns the inexpensive countdown business timer. Each metering
        // checkpoint is ordered and computes exactly once on the owner lane.
        // No background polling or additional Box2D steps between checkpoints.
        output.flags = 0;
        return CD_OK;
    }

    void dispose() noexcept override {
        model_.reset();
    }

private:
    std::unique_ptr<PhysicalSand> model_;
    double processedTime_ = 0;
};

class SandBackground::Impl {
public:
    explicit Impl(const CdCreateContext& context)
        : channel(context, std::make_shared<SandBackgroundState>(), sizeof(SandFrame)) {}

    cdsdk::BackgroundChannel channel;
    SandFrame frame{};
    double desiredTime = 0;
    SandTiming previous{};
    bool havePrevious = false, previousFlip = false;
};

SandBackground::SandBackground(const CdCreateContext& context) : impl_(std::make_unique<Impl>(context)) {}

SandBackground::~SandBackground() = default;

void SandBackground::advance(double seconds, const SandTiming& timer, bool projectedFlip) {
    if (impl_->channel.paused() || !std::isfinite(seconds) || seconds <= 0) {
        return;
    }
    auto& self = *impl_;
    seconds = std::min(seconds, .1);
    self.desiredTime += seconds;
    const TimerCommand command{self.desiredTime, seconds, timer, projectedFlip};
    const bool event = !self.havePrevious || timer.upper != self.previous.upper ||
                       timer.cycles != self.previous.cycles || timer.angle != self.previous.angle ||
                       projectedFlip != self.previousFlip;
    // Metering and flip samples are discrete barriers. Between them at most one
    // pending continuous update intent carries the latest accumulated time.
    self.channel.submit(1, std::as_bytes(std::span(&command, 1)), !event, 1);
    self.previous = timer;
    self.previousFlip = projectedFlip;
    self.havePrevious = true;
}

void SandBackground::reset() {
    impl_->channel.restart();
    impl_->frame = {};
    impl_->desiredTime = 0;
    impl_->havePrevious = false;
}

void SandBackground::consume() {
    impl_->channel.consume(&impl_->frame, sizeof(SandFrame));
}

void SandBackground::pause(bool paused) {
    const bool changed = impl_->channel.paused() != paused;
    impl_->channel.pause(paused);
    if (changed && paused) {
        impl_->desiredTime = 0;
        impl_->havePrevious = false;
    }
}

std::vector<cybercanvas::Point> SandBackground::sample() const {
    const auto& frame = impl_->frame;
    return {frame.positions.begin(), frame.positions.begin() + frame.count};
}

size_t SandBackground::count() const {
    return impl_->frame.count;
}

size_t SandBackground::released() const {
    return impl_->frame.released;
}

cybercanvas::physics::PhysicsStats SandBackground::stats() const {
    return impl_->frame.statistics;
}
}  // namespace hourglassplugin
