#include <sdk/Language.h>
#include <sdk/WindowsFonts.h>

#include <optional>

#include "plugins/pixel-desk/PixelAlbum.h"
#include "plugins/pixel-desk/PixelImages.h"
#include "plugins/pixel-desk/PixelLayout.h"
#include "plugins/pixel-desk/PixelParts.h"

namespace pixeldesk {
using cybercanvas::Canvas;
using cybercanvas::Rect;
using Microsoft::WRL::ComPtr;

widgetplugin::AlbumLayout pixelAlbumLayout(double width, double height) {
    const auto bounds = fitBody(width, height, 472, 280);
    widgetplugin::AlbumLayout layout;
    layout.stage = {bounds.width / 472, {bounds.x, bounds.y}};
    layout.photo = layout.stage.map({47, 30, 378, 214});
    layout.buttons[0] = layout.stage.map({6, 113, 30, 59});
    layout.buttons[1] = layout.stage.map({218, 218, 36, 20});
    layout.buttons[2] = layout.stage.map({433, 113, 30, 59});
    layout.dock = layout.buttons[1];
    layout.counter = layout.stage.map({338, 220, 80, 17});
    layout.digits = layout.counter;
    return layout;
}

struct AlbumRenderer::Impl {
    struct Key {
        double width, height, dpi;
        const cybercanvas::ImageData* body;
        bool operator==(const Key&) const = default;
    };

    cdsdk::Canvas canvas;
    cdsdk::Images images;
    cdsdk::Controls controls;
    cdsdk::WindowsFonts fonts{canvas.nativeTextFactory()};
    std::filesystem::path path;
    std::shared_ptr<const cybercanvas::ImageData> body;
    std::optional<Key> key;
    ComPtr<ID2D1RenderTarget> owner;
    cybercanvas::SurfaceImage plate;
    ComPtr<ID2D1PathGeometry> mask;
    std::function<void()> changed;
    std::function<void(uint32_t)> action;
    CdRect safe{};
    bool hovered = false, english = false;
    uint64_t builds = 0;

    explicit Impl(const CdCreateContext& context) {
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

    void label(Canvas& layer, std::wstring text, Rect area, double size, uint32_t ink) {
        cybercanvas::TextStyle style;
        style.family = fonts.family();
        style.locale = cdsdk::drawingLocale();
        style.size = size;
        style.bold = true;
        style.align = cybercanvas::TextAlign::Center;
        style.vertical = cybercanvas::VerticalAlign::Center;
        const auto fit = layer.fitTextInk(cdsdk::displayText(text), style, area);
        layer.brush->SetColor(cybercanvas::color(ink));
        layer.brush->SetOpacity(1);
        layer.target->DrawTextLayout(D2D1::Point2F(float(fit.origin.x), float(fit.origin.y)), fit.layout.Get(),
                                     layer.brush.Get());
    }

    void prepare(double width, double height, double dpi) {
        const auto layout = pixelAlbumLayout(width, height);
        const auto edge = uint32_t(std::clamp(std::ceil(476 * layout.stage.scale * dpi / 64) * 64, 64., 4096.));
        pixeldesk::requestPixelImage(images, "body", path, 1, edge, dpi);
        images.poll();
        if (const auto next = images.get("body").image) {
            body = next;
        }
        const Key next{width, height, dpi, body.get()};
        if (key == next && owner == canvas.target) {
            return;
        }
        key = next;
        cybercanvas::OffscreenSurface surface;
        canvas.renderLayer(surface, {uint32_t(std::ceil(width * dpi)), uint32_t(std::ceil(height * dpi)), dpi},
                           [&](Canvas& layer) {
                               layout.stage.draw(layer, [&] {
                                   if (body) {
                                       layer.image(*body, {-2, -2, 476, 284},
                                                   {.fit = cybercanvas::ImageFit::Stretch,
                                                    .clip = false,
                                                    .sampling = cybercanvas::ImageSampling::Nearest});
                                   }
                               });
                           });
        plate = surface.snapshot();
        owner = canvas.target;
        const auto r = layout.photo;
        const double step = 4 * layout.stage.scale;
        auto opening = canvas.path();
        opening.moveTo({r.x + step, r.y})
            .lineTo({r.x + r.width - step, r.y})
            .lineTo({r.x + r.width - step, r.y + step})
            .lineTo({r.x + r.width, r.y + step})
            .lineTo({r.x + r.width, r.y + r.height - step})
            .lineTo({r.x + r.width - step, r.y + r.height - step})
            .lineTo({r.x + r.width - step, r.y + r.height})
            .lineTo({r.x + step, r.y + r.height})
            .lineTo({r.x + step, r.y + r.height - step})
            .lineTo({r.x, r.y + r.height - step})
            .lineTo({r.x, r.y + step})
            .lineTo({r.x + step, r.y + step})
            .close();
        mask = opening.geometry(canvas.factory.Get());
        ++builds;
    }
};

AlbumRenderer::AlbumRenderer(const CdCreateContext& context) : impl_(std::make_unique<Impl>(context)) {}

AlbumRenderer::~AlbumRenderer() = default;

void AlbumRenderer::onChange(std::function<void()> callback) {
    impl_->changed = std::move(callback);
}

void AlbumRenderer::onAction(std::function<void(uint32_t)> callback) {
    impl_->action = std::move(callback);
}

void AlbumRenderer::pause(bool paused) {
    impl_->images.pause(paused);
    if (paused) {
        cancelInput();
    }
}

void AlbumRenderer::cancelInput() {
    impl_->controls.cancel();
    impl_->hovered = false;
}

void AlbumRenderer::syncInput(const widgetplugin::Photo& photo, widgetplugin::AlbumPlayback playback, CdRect safe) {
    impl_->safe = safe;
    const auto layout = pixelAlbumLayout(safe.width, safe.height);
    std::vector<cdsdk::Controls::Item> items;
    for (unsigned i = 0; i < 3; ++i) {
        const auto name = impl_->english ? (i == 0            ? "Previous photo"
                                            : i == 2          ? "Next photo"
                                            : playback.paused ? "Resume slideshow"
                                                              : "Pause slideshow")
                                         : (i == 0            ? "上一张"
                                            : i == 2          ? "下一张"
                                            : playback.paused ? "继续轮播"
                                                              : "暂停轮播");
        items.push_back(
            {i + 1, layout.buttons[i], name, bool(photo.bitmap) && (i == 1 || (photo.count > 1 && !playback.pending))});
    }
    impl_->controls.items(std::move(items), {safe.x, safe.y});
}

void AlbumRenderer::input(const CdInput& event) {
    const auto r = impl_->safe;
    const bool hovered = event.kind != CD_INPUT_LEAVE && event.kind != CD_INPUT_CANCEL && std::isfinite(event.x) &&
                         std::isfinite(event.y) && event.x >= r.x && event.y >= r.y && event.x < r.x + r.width &&
                         event.y < r.y + r.height;
    if (hovered != impl_->hovered) {
        impl_->hovered = hovered;
        if (impl_->changed) {
            impl_->changed();
        }
    }
    impl_->controls.input(event);
}

bool AlbumRenderer::ready() const {
    return impl_->body && !impl_->images.pending() && impl_->images.get("body").image == impl_->body;
}

bool AlbumRenderer::render(const cdsdk::Settings& settings, const widgetplugin::Photo& photo,
                           const widgetplugin::Photo& previous, float fade, widgetplugin::AlbumPlayback playback,
                           const CdFrame& frame, CdRect safe) {
    if (!impl_->canvas.valid(frame)) {
        return false;
    }
    const cdsdk::LanguageScope language(settings.at("language").text);
    impl_->english = settings.at("language").text == "en";
    impl_->fonts.select(impl_->canvas.nativeTextFactory(), settings.at("font_family").text);
    impl_->canvas.fontCollection(impl_->fonts.collection());
    impl_->canvas.render(frame, safe, [&](Canvas& layer, CdRect area) {
        impl_->prepare(area.width, area.height, frame.device_pixel_ratio);
        syncInput(photo, playback, area);
        const auto layout = pixelAlbumLayout(area.width, area.height);
        const auto r = layout.photo;
        layer.target->DrawBitmap(impl_->plate.Get(), D2D1::RectF(0, 0, float(area.width), float(area.height)), 1,
                                 D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
        layer.masked(impl_->mask.Get(), [&] {
            layer.box(r.x, r.y, r.width, r.height, 0, kInk);
            const bool cover = settings.at("fit").text == "cover";
            const bool motion =
                playback.motionEnabled && settings.at("photo_motion").integer && !settings.at("reduced_motion").integer;
            const auto paintPhoto = [&](const widgetplugin::Photo& value, double seconds, double opacity) {
                if (value.bitmap) {
                    const auto bounds = motion ? widgetplugin::albumPhotoRect(r, seconds, cover, value.index) : r;
                    layer.image(*value.bitmap, bounds,
                                {.fit = cover ? cybercanvas::ImageFit::Cover : cybercanvas::ImageFit::Contain,
                                 .opacity = opacity,
                                 .clip = false,
                                 .sampling = cybercanvas::ImageSampling::Nearest});
                }
            };
            if (previous.bitmap && fade < 1) {
                paintPhoto(previous, playback.previousMotionSeconds, 1);
            }
            paintPhoto(photo, playback.motionSeconds, previous.bitmap ? std::clamp(double(fade), 0., 1.) : 1);
            if (!photo.bitmap && !previous.bitmap) {
                impl_->controls.icons.paint(
                    layer, {r.x + r.width * .44, r.y + r.height * .25, r.width * .12, r.height * .24}, "image", kMint);
                impl_->label(layer,
                             settings.at("language").text == "en" ? L"Choose a photo folder in settings"
                                                                  : L"请在设置中选择图片目录",
                             {r.x + 8, r.y + r.height * .56, r.width - 16, r.height * .20}, 24 * layout.stage.scale,
                             kCream);
            }
        });
        const bool details = photo.bitmap && (impl_->hovered || impl_->controls.hasFocus());
        for (unsigned i : {0u, 1u, 2u}) {
            if (i == 1 && !details) {
                continue;
            }
            const auto b = layout.buttons[i];
            const auto visual = impl_->controls.visual(i + 1);
            const bool enabled = photo.bitmap && (i == 1 || (photo.count > 1 && !playback.pending));
            if (i == 1 || visual.hovered || visual.pressed) {
                stepped(layer, b, 4 * layout.stage.scale, visual.pressed ? kMint : 0xf5e7cdcc);
            }
            const double inset = std::min(b.width, b.height) * .24, shift = visual.pressed ? layout.stage.scale : 0;
            impl_->controls.icons.paint(layer,
                                        {b.x + inset, b.y + inset + shift, b.width - inset * 2, b.height - inset * 2},
                                        i == 0            ? "chevron-left"
                                        : i == 2          ? "chevron-right"
                                        : playback.paused ? "play"
                                                          : "pause",
                                        enabled ? kInk : 0x677670ff);
        }
        if (details && settings.at("counter").integer && photo.count > 0) {
            const auto counter = layout.digits;
            stepped(layer,
                    {counter.x - 3 * layout.stage.scale, counter.y - 2 * layout.stage.scale,
                     counter.width + 6 * layout.stage.scale, counter.height + 4 * layout.stage.scale},
                    2 * layout.stage.scale, 0x14242ed9);
            impl_->label(layer, std::to_wstring(photo.index + 1) + L" / " + std::to_wstring(photo.count), layout.digits,
                         15 * layout.stage.scale, kCream);
        }
    });
    return true;
}

bool AlbumRenderer::save(const std::filesystem::path& path, const CdFrame& frame) {
    return impl_->canvas.save(path.wstring(), frame);
}

uint64_t AlbumRenderer::cacheBuilds() const {
    return impl_->builds;
}
}  // namespace pixeldesk
