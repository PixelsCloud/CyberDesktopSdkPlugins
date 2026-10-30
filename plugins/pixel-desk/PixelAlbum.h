#pragma once

#include <sdk/Controls.h>
#include <sdk/Environment.h>
#include <sdk/Images.h>
#include <sdk/Update.h>
#include <sdk/cpp/Services.h>
#include <sdk/services/ImageViewer.h>

#include <span>

#include "plugins/widget-suite/AlbumArtwork.h"
#include "plugins/widget-suite/Widget.h"

namespace pixeldesk {
widgetplugin::AlbumLayout pixelAlbumLayout(double width, double height);

class AlbumRenderer {
public:
    explicit AlbumRenderer(const CdCreateContext& context);
    ~AlbumRenderer();
    void onChange(std::function<void()> callback);
    void onAction(std::function<void(uint32_t)> callback);
    void pause(bool paused);
    void cancelInput();
    void syncInput(const widgetplugin::Photo& photo, widgetplugin::AlbumPlayback playback, CdRect safe);
    void input(const CdInput& event);
    bool ready() const;
    bool render(const cdsdk::Settings& settings, const widgetplugin::Photo& photo, const widgetplugin::Photo& previous,
                float fade, widgetplugin::AlbumPlayback playback, const CdFrame& frame, CdRect safe = {});
    bool save(const std::filesystem::path& path, const CdFrame& frame);
    uint64_t cacheBuilds() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class AlbumInstance {
public:
    explicit AlbumInstance(const CdCreateContext& context);
    ~AlbumInstance();
    cdsdk::Settings settings;
    CdResult render(const CdFrame& frame);
    CdResult applySettings(const CdSettingValue* values, uint32_t count);
    void layoutChanged(const CdLayout& layout);
    void setPaused(bool paused);
    void input(const CdInput& event);
    void invalidate();
    std::vector<uint8_t> saveState() const;
    CdResult restoreState(std::span<const uint8_t> bytes);
    const widgetplugin::Photo& photo() const;
    bool slideshowPaused() const;
    bool pending() const;
    bool ready() const;

private:
    bool motionAllowed() const;
    widgetplugin::AlbumPlayback playback() const;
    void request(int step = 0, bool rescan = false);
    void action(uint32_t id);
    void advance();
    void schedule();
    static void CD_CALL tick(void* pointer, uint64_t elapsed) noexcept;
    cdsdk::Services services_;
    cdsdk::Environment environment_;
    CdImageViewerApi viewer_{};
    void* context_ = nullptr;
    AlbumRenderer renderer_;
    widgetplugin::Album album_;
    widgetplugin::Photo photo_, previous_;
    cdsdk::Timer timer_;
    cdsdk::RedrawRequests redraw_;
    CdRect safe_{}, inputArea_{};
    uint64_t lastTick_ = 0, lastPhoto_ = 0, fadeStart_ = 0;
    double motion_ = 0, previousMotion_ = 0;
    float fade_ = 1;
    bool hidden_ = false, slideshowPaused_ = false, pending_ = true, needsRequest_ = true;
};
}  // namespace pixeldesk
