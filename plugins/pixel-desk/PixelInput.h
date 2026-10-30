#pragma once

#include <sdk/Environment.h>
#include <sdk/Images.h>
#include <sdk/Update.h>
#include <sdk/cpp/Services.h>

#include <algorithm>
#include <array>
#include <cmath>

#include "plugins/widget-suite/InputCounter.h"
#include "plugins/widget-suite/Widget.h"

namespace pixeldesk {
struct InputGeometry {
    std::array<cybercanvas::Rect, 2> channels, icons;
    std::array<cybercanvas::Rect, 10> cards, faces, glyphs;
};

InputGeometry inputGeometry();

class InputRenderer {
public:
    explicit InputRenderer(const CdCreateContext& context);
    ~InputRenderer();
    void onChange(std::function<void()> callback);
    void pause(bool paused);
    bool ready() const;
    bool render(const CdInputStats& stats, const widgetplugin::CounterDrums::Positions& positions, const CdFrame& frame,
                CdRect safe = {});
    bool save(const std::filesystem::path& path, const CdFrame& frame);
    uint64_t cacheBuilds() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class InputInstance {
public:
    explicit InputInstance(const CdCreateContext& context);
    ~InputInstance();
    cdsdk::Settings settings;
    CdResult render(const CdFrame& frame);
    CdResult applySettings(const CdSettingValue* values, uint32_t count);
    void layoutChanged(const CdLayout& layout);
    void setPaused(bool paused);
    void input(const CdInput& event);
    void invalidate();
    const CdInputStats& stats() const;
    widgetplugin::CounterDrums::Positions positions() const;

private:
    CdResult subscribe();
    void receive(const CdInputStats& stats);
    bool motionAllowed() const;
    static void CD_CALL tick(void* value, uint64_t elapsed) noexcept;
    cdsdk::Services services_;
    cdsdk::Environment environment_;
    InputRenderer renderer_;
    cdsdk::InputWatch watch_;
    cdsdk::Timer timer_;
    cdsdk::RedrawRequests redraw_;
    widgetplugin::CounterDrums drums_;
    CdInputStats stats_{sizeof(CdInputStats)};
    CdRect safe_{};
    bool hidden_ = false;
};
}  // namespace pixeldesk
