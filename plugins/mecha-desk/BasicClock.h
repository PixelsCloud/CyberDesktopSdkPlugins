#pragma once
#include <sdk/Canvas.h>
#include <sdk/Environment.h>
#include <sdk/Update.h>

#include "plugins/clock-suite/Clock.h"
#include "plugins/clock-suite/FlipClock.h"
#include "plugins/mecha-desk/Skin.h"

namespace clockplugin {
Time currentTime(const Options&, CdEnvironmentSnapshot);
}

namespace aeris_basic {
enum class BasicKind { Analog, Digital, Flip, Lcd };

struct BasicSpec {
    const char* id;
    const char* folder;
    BasicKind kind;
    unsigned columns, rows;

    clockplugin::Spec clock() const;
};

const BasicSpec& basicSpec(std::string_view id);
cdsdk::Settings basicDefaults(const BasicSpec& spec);

class BasicFace {
public:
    BasicFace(const CdCreateContext&, const BasicSpec&);
    ~BasicFace();
    void onChange(std::function<void()>);
    void pause(bool);
    bool render(const cdsdk::Settings&, clockplugin::Time, const clockplugin::FlipPose&, const CdFrame&, CdRect);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class BasicClockInstance {
public:
    explicit BasicClockInstance(const CdCreateContext&);
    ~BasicClockInstance();
    cdsdk::Settings settings;
    CdResult render(const CdFrame&);
    CdResult applySettings(const CdSettingValue*, uint32_t);
    void layoutChanged(const CdLayout&);
    void setPaused(bool);
    void invalidate();

private:
    CdResult schedule(const cdsdk::Settings&);
    void synchronize();
    static void CD_CALL tick(void*, uint64_t) noexcept;
    const BasicSpec& spec_;
    cdsdk::Environment environment_;
    cdsdk::Timer timer_;
    cdsdk::RedrawRequests redraw_;
    BasicFace renderer_;
    clockplugin::Time time_{};
    clockplugin::FlipTimeline timeline_;
    clockplugin::FlipPose pose_{};
    CdRect safe_{};
    bool paused_ = false;
    uint64_t pausedAt_ = 0;
};
}  // namespace aeris_basic
