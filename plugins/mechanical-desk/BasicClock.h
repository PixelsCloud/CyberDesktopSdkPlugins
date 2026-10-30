#pragma once

#include <sdk/Canvas.h>
#include <sdk/Environment.h>
#include <sdk/Images.h>
#include <sdk/Settings.h>
#include <sdk/Update.h>

#include <array>
#include <filesystem>
#include <memory>

#include "plugins/clock-suite/Clock.h"
#include "plugins/clock-suite/FlipClock.h"

namespace mechanicaldesk {
enum class BasicKind { Analog, Digital, Flip, Lcd };

struct BasicSpec {
    const char* id;
    const char* folder;
    BasicKind kind;
    unsigned columns, rows;
    cybercanvas::Size image;
    cybercanvas::Rect visible;
    cybercanvas::Point center;
    double radius = 0;
    std::array<cybercanvas::Rect, 6> readouts{};
    unsigned readoutCount = 0;

    clockplugin::Spec clock() const;
};

const BasicSpec& basicSpec(std::string_view id);
const BasicSpec& basicFolder(std::string_view folder);
cdsdk::Settings basicDefaults(const BasicSpec& spec);

class BasicFace {
public:
    BasicFace(const CdCreateContext& context, const BasicSpec& spec);
    ~BasicFace();
    void onChange(std::function<void()> callback);
    void pause(bool paused);
    void themeText(CdColor color);
    bool ready() const;
    uint64_t cacheBuilds() const;
    double panelExtension() const;
    bool render(const cdsdk::Settings& settings, clockplugin::Time time, const clockplugin::FlipPose& pose,
                const CdFrame& frame, CdRect safe = {});
    bool save(const std::filesystem::path& path, const CdFrame& frame);
    bool renderContent(const CdFrame& frame, CdRect safe, const std::function<void(cybercanvas::Canvas&)>& draw,
                       const std::function<void(cybercanvas::Canvas&)>& overlay = {});

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class BasicClockInstance {
public:
    explicit BasicClockInstance(const CdCreateContext& context);
    ~BasicClockInstance();
    cdsdk::Settings settings;
    CdResult render(const CdFrame& frame);
    CdResult applySettings(const CdSettingValue* values, uint32_t count);
    void layoutChanged(const CdLayout& layout);
    void setPaused(bool paused);
    void invalidate();

private:
    CdResult schedule(const cdsdk::Settings& values);
    void synchronize();
    static void CD_CALL tick(void* pointer, uint64_t elapsed) noexcept;
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
}  // namespace mechanicaldesk
