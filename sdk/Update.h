#pragma once

#include <CyberCanvas/Animation.h>

#include <functional>
#include <utility>

#include "sdk/PluginApi.h"

namespace cdsdk {
// UI-thread subscription ownership. Replacement is transactional: a failed new
// subscription leaves the old timer intact. Moving transfers ownership.
class Timer {
public:
    Timer() = default;

    ~Timer();

    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;

    Timer(Timer&& other) noexcept;

    Timer& operator=(Timer&& other) noexcept;

    void bind(const CdHostApi& host, void* context);

    void bind(const CdCreateContext& context);

    CdResult replace(uint32_t interval, CdTimerCallback callback, void* user);

    void stop();

    explicit operator bool() const;

    uint32_t interval() const;

private:
    void swap(Timer& other) noexcept;

    CdHostApi host_{};
    void* context_ = nullptr;
    uint64_t token_ = 0;
    uint32_t interval_ = 0;
    CdTimerCallback callback_ = nullptr;
    void* user_ = nullptr;
};

// Coalesce invalidations until the host begins a new texture. Data polling is
// independent; request a frame only when it changes visible state.
struct RedrawStats {
    uint64_t invalidations = 0, requests = 0, coalesced = 0;
};

class RedrawRequests {
public:
    void bind(const CdCreateContext& context);

    bool invalidate();

    void beginFrame();

    void pause(bool paused);

    bool pending() const;

    uint64_t requests() const;

    RedrawStats stats() const;

private:
    CdHostApi host_{};
    void* context_ = nullptr;
    bool pending_ = false, paused_ = false, deferred_ = false;
    uint64_t requests_ = 0;
    uint64_t invalidations_ = 0, coalesced_ = 0;
};

struct UpdateStep {
    bool changed = false, active = false;
};

// Optional finite/continuous visual work. The engine clock stays host-driven;
// hidden time is excluded and the final changed frame is delivered before stop.
class UpdateLoop {
public:
    using Callback = std::function<UpdateStep(cybercanvas::AnimationTick)>;
    UpdateLoop() = default;

    ~UpdateLoop();

    UpdateLoop(const UpdateLoop&) = delete;
    UpdateLoop& operator=(const UpdateLoop&) = delete;

    void bind(const CdCreateContext& context);

    void onUpdate(Callback callback);

    void onInvalidate(std::function<void()> callback);

    CdResult start(uint32_t interval, std::optional<uint64_t> now = {});

    void stop();

    CdResult pause(bool paused);

    void speed(double value);

    void beginFrame();

    bool active() const;

    uint32_t interval() const;

private:
    static void CD_CALL tick(void* user, uint64_t now) noexcept;

    Timer timer_;
    RedrawRequests redraw_;
    cybercanvas::AnimationClock clock_;
    Callback callback_;
    std::function<void()> invalidated_;
    uint32_t desired_ = 0;
    uint64_t generation_ = 0;
    double speed_ = 1;
    bool paused_ = false;
};
}  // namespace cdsdk
