#include <CyberCanvas/PanelLayout.h>
#include <sdk/Language.h>
#include <sdk/WindowsFonts.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>

#include "plugins/pixel-desk/PixelImages.h"
#include "plugins/pixel-desk/PixelLayout.h"
#include "plugins/pixel-desk/PixelParts.h"
#include "plugins/pixel-desk/PixelPlayer.h"

namespace pixeldesk {
using cybercanvas::Canvas;
using cybercanvas::Rect;
using Microsoft::WRL::ComPtr;

namespace {
cybercanvas::SceneViewport playerView(const PlayerSpec& spec, double width, double height) {
    const auto bounds = fitBody(width, height, spec.world.width, spec.world.height);
    return {bounds, {bounds.x, bounds.y}, bounds.width / spec.world.width};
}

widgetplugin::PlayerLayout worldLayout(const PlayerSpec& spec) {
    widgetplugin::PlayerLayout layout;
    if (spec.rows == 1) {
        layout.cover = {47, 24, 73, 72};
        layout.title = {145, 26, 161, 32};
        for (unsigned i = 0; i < 3; ++i) {
            layout.buttons[i] = {318 + i * 53.5, 26, 44, 45};
        }
        layout.timeline = {164, 83, 288, 7};
        layout.titleSize = 22;
        layout.detailSize = 15;
        return layout;
    }
    if (spec.columns == 2) {
        layout.cover = {42, 29, 234, 168};
        layout.title = {46, 216, 226, 38};
        for (unsigned i = 0; i < 3; ++i) {
            layout.buttons[i] = {48 + i * 77., 258, 64, 55};
        }
        layout.timeline = {63, 333, 200, 8};
        layout.titleSize = 22;
        layout.detailSize = 15;
        return layout;
    }
    layout.cover = {50, 44, 92, 102};
    layout.title = {163, 42, 153, 29};
    layout.artist = {163, 75, 152, 22};
    for (unsigned i = 0; i < 3; ++i) {
        layout.buttons[i] = {331 + i * 55., 47, 43, 45};
    }
    layout.timeline = {175, 111, 288, 8};
    layout.elapsed = {161, 131, 70, 19};
    layout.duration = {409, 131, 70, 19};
    layout.titleSize = 22;
    layout.detailSize = 15;
    return layout;
}

Rect progressGroove(CdRect path, double scale) {
    return {path.x - 13 * scale, path.y - 3 * scale, path.width + 26 * scale, path.height + 6 * scale};
}
}  // namespace

std::wstring pixelPlayerTime(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0) {
        return L"—";
    }
    const double whole = std::floor(seconds);
    const unsigned minute = unsigned(std::fmod(std::floor(whole / 60), 60)), second = unsigned(std::fmod(whole, 60));
    return whole < 3600 ? std::format(L"{:02}:{:02}", minute, second)
                        : std::format(L"{:.0f}:{:02}:{:02}", std::floor(whole / 3600), minute, second);
}

widgetplugin::PlayerLayout pixelPlayerLayout(const PlayerSpec& spec, double width, double height) {
    auto layout = worldLayout(spec);
    const auto view = playerView(spec, width, height);
    const auto map = [&](CdRect& r) {
        if (r.width > 0 && r.height > 0) {
            r = {view.origin.x + r.x * view.scale, view.origin.y + r.y * view.scale, r.width * view.scale,
                 r.height * view.scale};
        }
    };
    for (auto* rect : {&layout.cover, &layout.title, &layout.artist, &layout.album, &layout.source, &layout.timeline,
                       &layout.elapsed, &layout.duration}) {
        map(*rect);
    }
    for (auto& button : layout.buttons) {
        map(button);
    }
    layout.titleSize *= float(view.scale);
    layout.detailSize *= float(view.scale);
    return layout;
}

struct PlayerRenderer::Impl {
    struct Key {
        double width, height, dpi;
        bool background, progress;
        const cybercanvas::ImageData* body;
        bool operator==(const Key&) const = default;
    };

    const PlayerSpec& spec;
    cdsdk::Canvas canvas;
    cdsdk::Images images;
    cdsdk::Controls controls;
    cdsdk::WindowsFonts fonts{canvas.nativeTextFactory()};
    std::filesystem::path path;
    std::shared_ptr<const cybercanvas::ImageData> body;
    std::optional<Key> key;
    ComPtr<ID2D1RenderTarget> owner;
    cybercanvas::SurfaceImage plate, wheel;
    std::function<void()> changed;
    std::function<void(uint32_t)> action;
    bool english = false, seeking = false;
    uint64_t builds = 0;

    Impl(const CdCreateContext& context, const PlayerSpec& value) : spec(value) {
        images.bind(context);
        canvas.bindGpu(context);
        images.onChange([this] {
            if (changed) {
                changed();
            }
        });
        controls.bind(context, [this](uint32_t id) {
            if (action) {
                action(id);
            }
        });
        path = std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(context.plugin_directory))) /
               "assets/body.png";
    }

    void text(Canvas& layer, const std::wstring& value, CdRect bounds, double size, uint32_t ink,
              bool centered = false) {
        if (bounds.width <= 0 || bounds.height <= 0 || value.empty()) {
            return;
        }
        cybercanvas::TextStyle style;
        style.family = fonts.family();
        style.locale = cdsdk::drawingLocale();
        style.size = size;
        style.bold = true;
        style.align = centered ? cybercanvas::TextAlign::Center : cybercanvas::TextAlign::Start;
        style.vertical = cybercanvas::VerticalAlign::Center;
        style.ellipsis = true;
        layer.text(value, style, {bounds.x, bounds.y, bounds.width, bounds.height}, ink);
    }

    void prepare(const cdsdk::Settings& settings, double width, double height, double dpi) {
        const auto view = playerView(spec, width, height);
        const auto edge = uint32_t(std::clamp(std::ceil(std::max(spec.source.width, spec.source.height) *
                                                        spec.partHeight / spec.visible.height * view.scale * dpi / 64) *
                                                  64,
                                              64., 4096.));
        pixeldesk::requestPixelImage(images, "body", path, 1, edge, dpi);
        images.poll();
        if (const auto next = images.get("body").image) {
            body = next;
        }
        const Key next{width,
                       height,
                       dpi,
                       settings.at("show_background").integer != 0,
                       settings.at("show_progress").integer != 0,
                       body.get()};
        if (key == next && owner == canvas.target) {
            return;
        }
        key = next;
        cybercanvas::OffscreenSurface surface;
        canvas.renderLayer(
            surface, {uint32_t(std::ceil(width * dpi)), uint32_t(std::ceil(height * dpi)), dpi}, [&](Canvas& layer) {
                layer.translate(view.origin.x, view.origin.y);
                layer.scale(view.scale, view.scale);
                if (key->background) {
                    if (body) {
                        layer.image(*body, {-spec.visible.x, -spec.visible.y, spec.source.width, spec.source.height},
                                    {.fit = cybercanvas::ImageFit::Stretch,
                                     .clip = false,
                                     .sampling = cybercanvas::ImageSampling::Nearest});
                    }
                }
                const auto geometry = worldLayout(spec);
                if (!key->background) {
                    for (const auto r : {geometry.title, geometry.artist, geometry.album, geometry.source,
                                         geometry.elapsed, geometry.duration}) {
                        if (r.width > 0 && r.height > 0) {
                            stepped(layer, {r.x - 4, r.y - 2, r.width + 8, r.height + 4}, 3, 0x14242ee6);
                        }
                    }
                }
                const auto cover = geometry.cover;
                stepped(layer, {cover.x - 3, cover.y - 3, cover.width + 6, cover.height + 6}, 3, kInk);
                if (!key->background) {
                    for (unsigned i = 0; i < geometry.buttons.size(); ++i) {
                        const auto b = geometry.buttons[i];
                        stepped(layer, {b.x - 2, b.y - 2, b.width + 4, b.height + 4}, 3, kInk);
                        stepped(layer, {b.x, b.y, b.width, b.height}, 3, i == 1 ? kMint : kCream);
                    }
                }
                const auto groove = progressGroove(geometry.timeline, 1);
                if (key->background) {
                    stepped(layer, {groove.x - 2, groove.y - 3, groove.width + 4, groove.height + 6}, 3, kInk);
                }
                if (key->progress) {
                    stepped(layer, groove, 3, 0x9ba8a6ff);
                    stepped(layer, {groove.x + 2, groove.y + 2, groove.width - 4, groove.height - 4}, 2, 0x294052ff);
                }
            });
        plate = surface.snapshot();
        owner = canvas.target;
        if (spec.reels) {
            cybercanvas::OffscreenSurface sprite;
            canvas.renderLayer(
                sprite, {uint32_t(std::ceil(92 * view.scale * dpi)), uint32_t(std::ceil(92 * view.scale * dpi)), dpi},
                [&](Canvas& layer) {
                    layer.scale(view.scale, view.scale);
                    pixelDisc(layer, 46, 46, 27, kInk);
                    pixelDisc(layer, 46, 46, 25, 0x294052ff);
                    pixelDisc(layer, 46, 46, 23, kInk);
                    for (unsigned i = 0; i < 4; ++i) {
                        layer.saved([&] {
                            layer.translate(46, 46);
                            layer.rotate(i * 90.);
                            stepped(layer, {-3, -25, 6, 10}, 1, kCream);
                        });
                    }
                    pixelDisc(layer, 46, 46, 9, kInk);
                    pixelDisc(layer, 46, 46, 6, kCream);
                    pixelDisc(layer, 46, 46, 3, 0x657174ff);
                });
            wheel = sprite.snapshot();
        }
        ++builds;
    }
};

PlayerRenderer::PlayerRenderer(const CdCreateContext& context, const PlayerSpec& spec)
    : impl_(std::make_unique<Impl>(context, spec)) {}

PlayerRenderer::~PlayerRenderer() = default;

void PlayerRenderer::onChange(std::function<void()> callback) {
    impl_->changed = std::move(callback);
}

void PlayerRenderer::onAction(std::function<void(uint32_t)> callback) {
    impl_->action = std::move(callback);
}

void PlayerRenderer::pause(bool paused) {
    impl_->images.pause(paused);
    if (paused) {
        cancelInput();
    }
}

void PlayerRenderer::cancelInput() {
    impl_->controls.cancel();
}

void PlayerRenderer::setSeeking(bool seeking) {
    impl_->seeking = seeking;
}

void PlayerRenderer::syncInput(const widgetplugin::Media& media, CdRect safe) {
    if (impl_->seeking) {
        return;
    }
    const auto geometry = pixelPlayerLayout(impl_->spec, safe.width, safe.height);
    constexpr uint32_t flags[]{CD_MEDIA_CAN_PREVIOUS, CD_MEDIA_CAN_TOGGLE, CD_MEDIA_CAN_NEXT};
    std::vector<cdsdk::Controls::Item> items;
    for (unsigned i = 0; i < 3; ++i) {
        const auto name = impl_->english ? (i == 0                             ? "Previous track"
                                            : i == 2                           ? "Next track"
                                            : media.status == CD_MEDIA_PLAYING ? "Pause"
                                                                               : "Play")
                                         : (i == 0                             ? "上一曲"
                                            : i == 2                           ? "下一曲"
                                            : media.status == CD_MEDIA_PLAYING ? "暂停"
                                                                               : "播放");
        const auto r = geometry.buttons[i];
        items.push_back({i + 1,
                         {r.x, r.y, r.width, r.height},
                         name,
                         !media.source.empty() && (media.controls & flags[i]) && media.commandResult != CD_ERROR_BUSY});
    }
    impl_->controls.items(std::move(items), {safe.x, safe.y});
}

void PlayerRenderer::input(const CdInput& event) {
    impl_->controls.input(event);
}

bool PlayerRenderer::ready() const {
    return impl_->body && !impl_->images.pending() && impl_->images.get("body").image == impl_->body;
}

bool PlayerRenderer::render(const cdsdk::Settings& settings, const widgetplugin::Media& media, double position,
                            const CdFrame& frame, CdRect safe) {
    if (!impl_->canvas.valid(frame)) {
        return false;
    }
    const cdsdk::LanguageScope language(settings.at("language").text);
    impl_->english = settings.at("language").text == "en";
    impl_->fonts.select(impl_->canvas.nativeTextFactory(), settings.at("font_family").text);
    impl_->canvas.fontCollection(impl_->fonts.collection());
    impl_->canvas.render(frame, safe, [&](Canvas& layer, CdRect area) {
        impl_->prepare(settings, area.width, area.height, frame.device_pixel_ratio);
        syncInput(media, area);
        const auto view = playerView(impl_->spec, area.width, area.height);
        const auto geometry = pixelPlayerLayout(impl_->spec, area.width, area.height);
        const double scale = view.scale;
        layer.target->DrawBitmap(impl_->plate.Get(), D2D1::RectF(0, 0, float(area.width), float(area.height)), 1,
                                 D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
        const auto cover = geometry.cover;
        if (!media.source.empty() && media.cover.bitmap) {
            layer.image(*media.cover.bitmap, {cover.x, cover.y, cover.width, cover.height},
                        {.fit = cybercanvas::ImageFit::Cover});
        } else {
            impl_->controls.icons.paint(
                layer, {cover.x + cover.width * .25, cover.y + cover.height * .25, cover.width * .5, cover.height * .5},
                "music-2", kMint);
        }
        const auto title =
            !media.source.empty()
                ? (!media.title.empty() ? media.title : widgetplugin::playerName(media.source, impl_->english))
            : impl_->english ? L"No music playing"
                             : cdsdk::displayText(L"暂无正在播放的音乐");
        impl_->text(layer, title, geometry.title, geometry.titleSize, kCream,
                    impl_->spec.rows > 1 && impl_->spec.columns == 2);
        if (!media.source.empty()) {
            impl_->text(layer, media.artist, geometry.artist, geometry.detailSize, kCream);
            impl_->text(layer, media.album, geometry.album, geometry.detailSize, kCream);
        }
        impl_->text(layer, widgetplugin::playerName(media.source, impl_->english), geometry.source, geometry.detailSize,
                    kMint);
        constexpr uint32_t flags[]{CD_MEDIA_CAN_PREVIOUS, CD_MEDIA_CAN_TOGGLE, CD_MEDIA_CAN_NEXT};
        for (unsigned i = 0; i < 3; ++i) {
            const auto b = geometry.buttons[i];
            const auto visual = impl_->controls.visual(i + 1);
            const bool enabled =
                !media.source.empty() && (media.controls & flags[i]) && media.commandResult != CD_ERROR_BUSY;
            if (visual.hovered || visual.pressed || !enabled) {
                stepped(layer, {b.x + scale * 2, b.y + scale * 2, b.width - scale * 4, b.height - scale * 7}, 3 * scale,
                        !enabled         ? 0x14242e55
                        : visual.pressed ? kMint
                                         : 0x9bcdb480);
            }
            const double inset = std::min(b.width, b.height) * .24, shift = visual.pressed ? scale : 0;
            impl_->controls.icons.paint(layer,
                                        {b.x + inset, b.y + inset + shift, b.width - inset * 2, b.height - inset * 2},
                                        i == 0                             ? "skip-back"
                                        : i == 2                           ? "skip-forward"
                                        : media.status == CD_MEDIA_PLAYING ? "pause"
                                                                           : "play",
                                        enabled ? kInk : 0x677670ff);
        }
        if (settings.at("show_progress").integer && !media.source.empty() && std::isfinite(position) &&
            media.duration > 0) {
            const auto r = geometry.timeline;
            const double fraction = std::clamp(position / media.duration, 0., 1.), center = r.x + r.width * fraction;
            layer.box(r.x - 10 * scale, r.y, 10 * scale + r.width * fraction, r.height, scale, kMint);
            stepped(layer, {center - 10 * scale, r.y + r.height / 2 - 13 * scale, 20 * scale, 26 * scale}, 2 * scale,
                    kInk);
            stepped(layer, {center - 7 * scale, r.y + r.height / 2 - 10 * scale, 14 * scale, 20 * scale}, scale, kMint);
            layer.line(center - 4 * scale, r.y + r.height / 2 - 8 * scale, center + 4 * scale,
                       r.y + r.height / 2 - 8 * scale, kCream, scale);
        }
        if (settings.at("show_progress").integer) {
            const bool timeline = !media.source.empty() && media.duration > 0;
            impl_->text(layer, timeline ? pixelPlayerTime(position) : L"—", geometry.elapsed, geometry.detailSize,
                        kCream, true);
            impl_->text(layer, timeline ? pixelPlayerTime(media.duration) : L"—", geometry.duration,
                        geometry.detailSize, kCream, true);
        }
        if (impl_->spec.reels && settings.at("show_background").integer && impl_->wheel) {
            for (double center : {133., 383.}) {
                layer.saved([&] {
                    layer.translate(view.origin.x + center * scale, view.origin.y + 218 * scale);
                    layer.rotate(media.recordDegrees);
                    layer.target->DrawBitmap(
                        impl_->wheel.Get(),
                        D2D1::RectF(float(-46 * scale), float(-46 * scale), float(46 * scale), float(46 * scale)), 1,
                        D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
                });
            }
        }
    });
    return true;
}

bool PlayerRenderer::save(const std::filesystem::path& path, const CdFrame& frame) {
    return impl_->canvas.save(path.wstring(), frame);
}

uint64_t PlayerRenderer::cacheBuilds() const {
    return impl_->builds;
}
}  // namespace pixeldesk
