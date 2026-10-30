// Media controller binding reuses the mature Pixel Desk session/seek controller.
#pragma once

#include <sdk/Controls.h>
#include <sdk/Environment.h>
#include <sdk/Images.h>
#include <sdk/Update.h>
#include <sdk/cpp/Services.h>

#include <optional>

#include "plugins/widget-suite/Widget.h"

namespace deep_player {
struct PlayerSpec {
    const char* id;
    const char* folder;
    unsigned columns, rows;
    cybercanvas::Size source;
    cybercanvas::Rect visible;
    cybercanvas::Size world;
    double partHeight;
    bool reels;
};

inline constexpr std::array<PlayerSpec, 3> kPlayerCatalog{{
    {"com.cyberdesktop.music.player.mechanical.1x4",
     "player-4x1",
     4,
     1,
     {525, 135},
     {2, 2, 521, 131},
     {521, 131},
     131,
     false},
    {"com.cyberdesktop.music.player.mechanical.2x2",
     "player-2x2",
     2,
     2,
     {320, 392},
     {2, 2, 316, 388},
     {316, 388},
     388,
     false},
    {"com.cyberdesktop.music.player.mechanical.2x4",
     "player-4x2",
     4,
     2,
     {531, 310},
     {2, 2, 527, 306},
     {527, 306},
     306,
     true},
}};

const PlayerSpec& playerSpec(std::string_view id);
widgetplugin::PlayerLayout pixelPlayerLayout(const PlayerSpec& spec, double width, double height);
std::wstring pixelPlayerTime(double seconds);
widgetplugin::Media pixelMedia(const cdsdk::MediaSnapshot& value, const widgetplugin::Media& previous = {});

class PlayerRenderer {
public:
    PlayerRenderer(const CdCreateContext& context, const PlayerSpec& spec);
    ~PlayerRenderer();
    void onChange(std::function<void()> callback);
    void onAction(std::function<void(uint32_t)> callback);
    void pause(bool paused);
    void cancelInput();
    void setSeeking(bool seeking);
    void syncInput(const widgetplugin::Media& media, CdRect safe);
    void input(const CdInput& event);
    bool ready() const;
    bool render(const cdsdk::Settings& settings, const widgetplugin::Media& media, double position,
                const CdFrame& frame, CdRect safe = {});
    bool save(const std::filesystem::path& path, const CdFrame& frame);
    uint64_t cacheBuilds() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class PlayerInstance {
public:
    explicit PlayerInstance(const CdCreateContext& context);
    ~PlayerInstance();
    cdsdk::Settings settings;
    CdResult render(const CdFrame& frame);
    CdResult applySettings(const CdSettingValue* values, uint32_t count);
    void layoutChanged(const CdLayout& layout);
    void setPaused(bool paused);
    void input(const CdInput& event);
    void invalidate();
    const widgetplugin::Media& media() const;
    uint64_t pendingRequest() const;
    bool ready() const;

private:
    CdResult subscribe(cdsdk::Settings next, bool clear);
    void receive(const cdsdk::MediaSnapshot& value);
    void command(uint32_t command, double value = 0);
    void action(uint32_t id);
    void syncSeek();
    void cancelSeek();
    bool motionAllowed(const cdsdk::Settings* candidate = nullptr) const;
    CdResult schedule(const cdsdk::Settings* candidate = nullptr);
    static void CD_CALL tick(void* pointer, uint64_t elapsed) noexcept;
    const PlayerSpec& spec_;
    cdsdk::Services services_;
    void* context_ = nullptr;
    cdsdk::Environment environment_;
    PlayerRenderer renderer_;
    cdsdk::MediaWatch watch_;
    cdsdk::Timer timer_;
    cdsdk::RedrawRequests redraw_;
    cdsdk::Interaction seek_;
    widgetplugin::Media media_;
    CdRect safe_{}, inputArea_{};
    std::optional<double> seekPosition_;
    std::optional<uint64_t> commandBegan_;
    uint64_t session_ = 0, lastCommand_ = 0, pendingRequest_ = 0, lastTick_ = 0;
    double shownSecond_ = -1;
    bool hidden_ = false, seeking_ = false;
};
}  // namespace deep_player
