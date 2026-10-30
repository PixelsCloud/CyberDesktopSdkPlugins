// Media controller binding reuses the mature Pixel Desk session/seek controller.
#pragma once

#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "plugins/widget-suite/MediaController.h"
#include "plugins/widget-suite/Widget.h"

namespace aeris_player {
using widgetplugin::media::PlayerSpec;

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

class PlayerRenderer final : public widgetplugin::media::View {
public:
    PlayerRenderer(const CdCreateContext& context, const PlayerSpec& spec);
    ~PlayerRenderer() override;
    void onChange(std::function<void()> callback) override;
    void onAction(std::function<void(uint32_t)> callback) override;
    void pause(bool paused) override;
    void cancelInput() override;
    void setSeeking(bool seeking) override;
    void syncInput(const widgetplugin::Media& media, CdRect safe) override;
    void input(const CdInput& event) override;
    bool ready() const override;
    bool animatedArtwork() const override;
    cybercanvas::Rect timeline(CdRect safe) const;
    bool render(const cdsdk::Settings& settings, const widgetplugin::Media& media, double position,
                const CdFrame& frame, CdRect safe = {}) override;
    cybercanvas::Rect seekRegion(const PlayerSpec&, double width, double height) const override;
    bool save(const std::filesystem::path& path, const CdFrame& frame);
    uint64_t cacheBuilds() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class PlayerInstance final : public widgetplugin::media::Controller {
public:
    explicit PlayerInstance(const CdCreateContext& context);
};

}  // namespace aeris_player
