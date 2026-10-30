#include <sdk/Controls.h>
#include <sdk/Environment.h>
#include <sdk/Images.h>
#include <sdk/Update.h>
#include <sdk/cpp/Services.h>

#include <algorithm>
#include <cmath>
#include <format>

#include "plugins/deep-whale/Player.h"
#include "plugins/deep-whale/Skin.h"

namespace deep_player {
widgetplugin::PlayerLayout pixelPlayerLayout(const PlayerSpec& spec, double width, double height) {
    const double w = spec.columns * 80., h = spec.rows * 100.;
    const double scale = std::min(std::max(1., width - 14) / (w - 14), std::max(1., height - 14) / (h - 14));
    cybercanvas::Rect r;
    if (spec.columns == 2) {
        r = {36.98438, 155.9375, 84.10938, 12};
    } else if (spec.rows == 1) {
        r = {100.8125, 64.4375, 174.35938, 10};
    } else {
        r = {35.39063, 105.20313, 147.53125, 12};
    }
    const double knob = std::min(4.5, r.height * .36);
    r.x += knob;
    r.width -= 2 * knob;
    widgetplugin::PlayerLayout layout{};
    layout.timeline = {(width - w * scale) / 2 + r.x * scale, (height - h * scale) / 2 + r.y * scale, r.width * scale,
                       r.height * scale};
    return layout;
}

std::wstring pixelPlayerTime(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0) {
        return L"--:--";
    }
    const auto count = uint64_t(seconds);
    return std::format(L"{:02}:{:02}", count / 60, count % 60);
}

struct PlayerRenderer::Impl {
    whaleskin::Skin skin;
    const PlayerSpec& spec;
    std::function<void(uint32_t)> action;
    bool seeking = false;

    Impl(const CdCreateContext& context, const PlayerSpec& value) : spec(value) {
        skin.bind(context, [this](uint32_t id) {
            if (action) {
                action(id);
            }
        });
    }

    bool enabled(const widgetplugin::Media& media, unsigned i) const {
        constexpr uint32_t flags[]{CD_MEDIA_CAN_PREVIOUS, CD_MEDIA_CAN_TOGGLE, CD_MEDIA_CAN_NEXT};
        return !media.source.empty() && (media.controls & flags[i]) && media.commandResult != CD_ERROR_BUSY;
    }
};

PlayerRenderer::PlayerRenderer(const CdCreateContext& c, const PlayerSpec& spec)
    : impl_(std::make_unique<Impl>(c, spec)) {}

PlayerRenderer::~PlayerRenderer() = default;

void PlayerRenderer::onChange(std::function<void()> callback) {
    impl_->skin.onChange(std::move(callback));
}

void PlayerRenderer::onAction(std::function<void(uint32_t)> callback) {
    impl_->action = std::move(callback);
}

void PlayerRenderer::pause(bool paused) {
    impl_->skin.pause(paused);
}

void PlayerRenderer::cancelInput() {
    impl_->skin.cancelInput();
}

void PlayerRenderer::setSeeking(bool seeking) {
    impl_->seeking = seeking;
}

void PlayerRenderer::syncInput(const widgetplugin::Media& media, CdRect safe) {
    if (impl_->seeking) {
        return;
    }
    std::vector<cdsdk::Controls::Item> items;
    constexpr const char* keys[]{"previous", "toggle", "next"};
    for (unsigned i = 0; i < 3; ++i) {
        items.push_back({i + 1, impl_->skin.region(keys[i]), keys[i], impl_->enabled(media, i)});
    }
    impl_->skin.controls(safe, items);
}

void PlayerRenderer::input(const CdInput& event) {
    impl_->skin.input(event);
}

bool PlayerRenderer::ready() const {
    return impl_->skin.ready();
}

uint64_t PlayerRenderer::cacheBuilds() const {
    return impl_->skin.cacheBuilds();
}

bool PlayerRenderer::save(const std::filesystem::path& path, const CdFrame& frame) {
    return impl_->skin.save(path, frame);
}

bool PlayerRenderer::render(const cdsdk::Settings& settings, const widgetplugin::Media& media, double position,
                            const CdFrame& frame, CdRect safe) {
    if (safe.width <= 0 || safe.height <= 0) {
        safe = {0, 0, frame.width / frame.device_pixel_ratio, frame.height / frame.device_pixel_ratio};
    }
    impl_->skin.appearance(settings);
    syncInput(media, safe);
    return impl_->skin.render(frame, safe, [&](auto& canvas) {
        auto& skin = impl_->skin;
        const auto cover = skin.region("cover");
        if (media.cover.bitmap) {
            skin.photo(canvas, cover, *media.cover.bitmap);
        } else {
            skin.clear(canvas, cover, 0xe3eef7ff, true);
            skin.icon(canvas,
                      {cover.x + cover.width * .25, cover.y + cover.height * .25, cover.width * .5, cover.height * .5},
                      "music-2");
        }
        if (impl_->spec.reels && settings.at("show_background").integer) {
            // Only the cassette hubs rotate; the tape shell and character stay fixed.
            for (const auto x : {83., 128.}) {
                canvas.saved([&] {
                    canvas.translate(x, 150);
                    canvas.rotate(media.recordDegrees);
                    canvas.oval(0, 0, 6.5, 6.5, 0xd2e9edff);
                    canvas.oval(0, 0, 2.1, 2.1, 0x234972ff);
                    for (unsigned spoke = 0; spoke < 3; ++spoke) {
                        canvas.saved([&] {
                            canvas.rotate(spoke * 120.);
                            canvas.box(-1.1, -5.1, 2.2, 2.3, .5, 0x234972ff);
                        });
                    }
                });
            }
        }
        const auto title = skin.region("title");
        const bool english = settings.at("language").text == "en";
        skin.caption(canvas, media.title.empty() ? (english ? L"Not playing" : L"未在播放") : media.title, title,
                     title.height * .70);
        const auto artist = skin.region("artist");
        skin.caption(canvas, media.artist, artist, artist.height * .70);
        if (settings.at("show_progress").integer) {
            skin.progress(canvas, skin.region("progress"), media.duration > 0 ? position / media.duration : 0);
        }
        for (const auto& field : {std::pair{"elapsed", pixelPlayerTime(position)},
                                  std::pair{"duration", pixelPlayerTime(media.duration)}}) {
            const auto r = skin.region(field.first);
            skin.label(canvas, field.second, r, r.height * .80);
        }
        constexpr const char* keys[]{"previous", "toggle", "next"};
        for (unsigned i = 0; i < 3; ++i) {
            const auto symbol = i == 0                             ? "skip-back"
                                : i == 2                           ? "skip-forward"
                                : media.status == CD_MEDIA_PLAYING ? "pause"
                                                                   : "play";
            skin.button(canvas, skin.region(keys[i]), symbol, false, impl_->enabled(media, i));
        }
    });
}
}  // namespace deep_player
