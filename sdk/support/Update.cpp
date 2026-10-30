#include "sdk/Update.h"

namespace cdsdk {
Timer::operator bool() const {
    return token_ != 0;
}

Timer::~Timer() {
    stop();
}

Timer::Timer(Timer&& other) noexcept {
    swap(other);
}

auto Timer::operator=(Timer&& other) noexcept -> Timer& {
    if (this != &other) {
        stop();
        swap(other);
    }
    return *this;
}

auto Timer::bind(const CdHostApi& host, void* context) -> void {
    stop();
    host_ = host.struct_size >= sizeof(CdHostApi) ? host : CdHostApi{};
    context_ = context;
}

auto Timer::bind(const CdCreateContext& context) -> void {
    if (context.host && context.host->struct_size >= sizeof(CdHostApi)) {
        bind(*context.host, context.host_context);
    } else {
        bind(CdHostApi{}, context.host_context);
    }
}

auto Timer::replace(uint32_t interval, CdTimerCallback callback, void* user) -> CdResult {
    if (!interval) {
        stop();
        return CD_OK;
    }
    if (!callback || !host_.subscribe_timer || !host_.unsubscribe) {
        return CD_ERROR_UNSUPPORTED;
    }
    if (token_ && interval_ == interval && callback_ == callback && user_ == user) {
        return CD_OK;
    }
    uint64_t next = 0;
    const auto result = host_.subscribe_timer(context_, interval, callback, user, &next);
    if (result != CD_OK || !next) {
        if (next) {
            host_.unsubscribe(context_, next);
        }
        return result == CD_OK ? CD_ERROR_INTERNAL : result;
    }
    stop();
    token_ = next;
    interval_ = interval;
    callback_ = callback;
    user_ = user;
    return CD_OK;
}

auto Timer::stop() -> void {
    if (token_) {
        host_.unsubscribe(context_, std::exchange(token_, 0));
    }
    interval_ = 0;
    callback_ = nullptr;
    user_ = nullptr;
}

auto Timer::interval() const -> uint32_t {
    return interval_;
}

auto Timer::swap(Timer& other) noexcept -> void {
    std::swap(host_, other.host_);
    std::swap(context_, other.context_);
    std::swap(token_, other.token_);
    std::swap(interval_, other.interval_);
    std::swap(callback_, other.callback_);
    std::swap(user_, other.user_);
}

auto RedrawRequests::bind(const CdCreateContext& context) -> void {
    host_ = context.host && context.host->struct_size >= sizeof(CdHostApi) ? *context.host : CdHostApi{};
    context_ = context.host_context;
    pending_ = false;
    requests_ = 0;
    invalidations_ = coalesced_ = 0;
    paused_ = deferred_ = false;
}

auto RedrawRequests::invalidate() -> bool {
    ++invalidations_;
    if (paused_) {
        ++coalesced_;
        deferred_ = true;
        return false;
    }
    if (pending_) {
        ++coalesced_;
        return false;
    }
    pending_ = true;
    ++requests_;
    if (host_.request_redraw) {
        host_.request_redraw(context_);
    }
    return true;
}

auto RedrawRequests::beginFrame() -> void {
    pending_ = false;
}

auto RedrawRequests::pause(bool paused) -> void {
    if (paused_ == paused) {
        return;
    }
    paused_ = paused;
    deferred_ |= pending_;
    pending_ = false;
    if (!paused && std::exchange(deferred_, false)) {
        invalidate();
    }
}

auto RedrawRequests::pending() const -> bool {
    return pending_;
}

auto RedrawRequests::requests() const -> uint64_t {
    return requests_;
}

auto RedrawRequests::stats() const -> RedrawStats {
    return {invalidations_, requests_, coalesced_};
}

UpdateLoop::~UpdateLoop() {
    stop();
}

auto UpdateLoop::bind(const CdCreateContext& context) -> void {
    timer_.bind(context);
    redraw_.bind(context);
    desired_ = 0;
    paused_ = false;
    clock_.setPaused(false);
    clock_.resetAnchor();
}

auto UpdateLoop::onUpdate(Callback callback) -> void {
    callback_ = std::move(callback);
    ++generation_;
}

auto UpdateLoop::onInvalidate(std::function<void()> callback) -> void {
    invalidated_ = std::move(callback);
}

auto UpdateLoop::start(uint32_t interval, std::optional<uint64_t> now) -> CdResult {
    if (!interval) {
        stop();
        return CD_OK;
    }
    if (!callback_) {
        return CD_ERROR_ARGUMENT;
    }
    if (paused_ || speed_ == 0) {
        desired_ = interval;
        ++generation_;
        return CD_OK;
    }
    const auto previous = timer_.interval();
    const auto result = timer_.replace(interval, tick, this);
    if (result == CD_OK) {
        desired_ = interval;
        if (previous != interval) {
            ++generation_;
        }
        if (!previous || previous > interval) {
            clock_.resetAnchor();
            if (now) {
                clock_.synchronize(*now);
            }
        }
    }
    return result;
}

auto UpdateLoop::stop() -> void {
    ++generation_;
    timer_.stop();
    desired_ = 0;
    clock_.resetAnchor();
}

auto UpdateLoop::pause(bool paused) -> CdResult {
    if (paused_ == paused) {
        return !paused && desired_ && !timer_ ? start(desired_) : CD_OK;
    }
    paused_ = paused;
    ++generation_;
    redraw_.pause(paused);
    clock_.setPaused(paused);
    if (paused) {
        timer_.stop();
        return CD_OK;
    }
    return desired_ ? start(desired_) : CD_OK;
}

auto UpdateLoop::speed(double value) -> void {
    clock_.setSpeed(value);
    speed_ = value;
    ++generation_;
    if (value == 0) {
        timer_.stop();
        clock_.resetAnchor();
    } else if (!paused_ && desired_) {
        start(desired_);
    }
}

auto UpdateLoop::beginFrame() -> void {
    redraw_.beginFrame();
}

auto UpdateLoop::active() const -> bool {
    return bool(timer_);
}

auto UpdateLoop::interval() const -> uint32_t {
    return timer_.interval();
}

void CD_CALL UpdateLoop::tick(void* user, uint64_t now) noexcept {
    auto& self = *static_cast<UpdateLoop*>(user);
    try {
        if (self.paused_ || !self.callback_) {
            return;
        }
        const auto callback = self.callback_;
        const auto generation = self.generation_;
        const auto result = callback(self.clock_.tick(now));
        if (result.changed) {
            if (self.invalidated_) {
                self.invalidated_();
            } else {
                self.redraw_.invalidate();
            }
        }
        if (!result.active && self.generation_ == generation) {
            self.stop();
        }
    } catch (...) {
        self.stop();
    }
}
}  // namespace cdsdk
