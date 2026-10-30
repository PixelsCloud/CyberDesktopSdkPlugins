#include <sdk/Icons.h>
#include <sdk/Language.h>
#include <sdk/Update.h>
#include <sdk/WindowsFonts.h>

#include <algorithm>
#include <cmath>

#include "plugins/pixel-desk/PixelDigits.h"
#include "plugins/pixel-desk/PixelImages.h"
#include "plugins/pixel-desk/PixelLayout.h"
#include "plugins/pixel-desk/PixelParts.h"
#include "plugins/pixel-desk/PixelProductivity.h"

namespace pixeldesk {
using cybercanvas::Canvas;
using cybercanvas::Rect;
using Microsoft::WRL::ComPtr;

std::optional<DiskUsage> pixelDiskUsage(const productivity::Data& data) {
    if (!data.valid || !data.total || data.free > data.total || data.available > data.total) {
        return {};
    }
    const uint64_t used = data.total - data.free;
    return DiskUsage{used, data.total, data.available, double(used) / data.total};
}

struct DiskFace::Impl {
    struct Key {
        double width, height, dpi;
        const cybercanvas::ImageData* body;
        bool operator==(const Key&) const = default;
    };

    cdsdk::Canvas canvas;
    cdsdk::Images images;
    cdsdk::Icons icons;
    cdsdk::RedrawRequests redraw;
    cdsdk::WindowsFonts fonts{canvas.nativeTextFactory()};
    std::filesystem::path path;
    std::shared_ptr<const cybercanvas::ImageData> body;
    std::optional<Key> key;
    ComPtr<ID2D1RenderTarget> owner;
    cybercanvas::SurfaceImage plate;
    double scale = 1, x = 0, y = 0;
    uint64_t builds = 0;
    bool compact = false;

    void label(Canvas& layer, std::wstring text, Rect area, double size, uint32_t color = kCream) {
        cybercanvas::TextStyle style;
        style.family = fonts.family();
        style.locale = cdsdk::drawingLocale();
        style.size = size;
        style.bold = true;
        style.align = cybercanvas::TextAlign::Center;
        style.vertical = cybercanvas::VerticalAlign::Center;
        const auto fit = layer.fitTextInk(text, style, area);
        layer.brush->SetColor(cybercanvas::color(color));
        layer.brush->SetOpacity(1);
        layer.target->DrawTextLayout(D2D1::Point2F(float(fit.origin.x), float(fit.origin.y)), fit.layout.Get(),
                                     layer.brush.Get());
    }

    void prepare(double width, double height, double dpi) {
        const double worldWidth = compact ? 430 : 365, worldHeight = compact ? 228 : 410;
        const auto bounds = fitBody(width, height, worldWidth, worldHeight);
        scale = bounds.width / worldWidth;
        x = bounds.x - 2 * scale;
        y = bounds.y - 2 * scale;
        const auto edge = uint32_t(
            std::clamp(std::ceil(std::max(worldWidth + 4, worldHeight + 4) * scale * dpi / 64) * 64, 64., 2048.));
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
                               layer.translate(x, y);
                               layer.scale(scale, scale);
                               if (body) {
                                   layer.image(*body, {0, 0, compact ? 434. : 369., compact ? 232. : 414.},
                                               {.fit = cybercanvas::ImageFit::Stretch,
                                                .clip = false,
                                                .sampling = cybercanvas::ImageSampling::Nearest});
                               }
                               for (double dash = compact ? 302 : 65; dash < 302; dash += 8) {
                                   layer.box(dash, 267, 4, 2, 0, 0xa2b0b8ff);
                               }
                           });
        plate = surface.snapshot();
        owner = canvas.target;
        ++builds;
    }
};

DiskFace::DiskFace() : impl_(std::make_unique<Impl>()) {}

DiskFace::~DiskFace() = default;

void DiskFace::bind(const CdCreateContext& context) {
    impl_->compact = std::string_view(context.plugin_id).ends_with(".2x1");
    impl_->images.bind(context);
    impl_->canvas.bindGpu(context);
    impl_->icons.bind(context);
    impl_->redraw.bind(context);
    impl_->images.onChange([this] {
        impl_->redraw.invalidate();
    });
    impl_->path =
        std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(context.plugin_directory))) /
        "assets/body.png";
}

void DiskFace::pause(bool paused) {
    impl_->images.pause(paused);
    impl_->redraw.pause(paused);
}

bool DiskFace::ready() const {
    return impl_->body && !impl_->images.pending() && impl_->images.get("body").image == impl_->body;
}

bool DiskFace::render(const cdsdk::Settings& settings, const productivity::Data& data, const CdFrame& frame,
                      CdRect safe) {
    if (!impl_->canvas.valid(frame)) {
        return false;
    }
    impl_->redraw.beginFrame();
    const cdsdk::LanguageScope language(settings.at("language").text);
    const bool english = settings.at("language").text == "en";
    impl_->fonts.select(impl_->canvas.nativeTextFactory(), settings.at("font_family").text);
    impl_->canvas.fontCollection(impl_->fonts.collection());
    const auto usage = pixelDiskUsage(data);
    impl_->canvas.render(frame, safe, [&](Canvas& layer, CdRect area) {
        impl_->prepare(area.width, area.height, frame.device_pixel_ratio);
        layer.target->DrawBitmap(impl_->plate.Get(), D2D1::RectF(0, 0, float(area.width), float(area.height)), 1,
                                 D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
        layer.saved([&] {
            layer.translate(impl_->x, impl_->y);
            layer.scale(impl_->scale, impl_->scale);
            if (impl_->compact) {
                const double fraction =
                    usage ? std::clamp(data.needles[0] >= 0 ? data.needles[0] : usage->fraction, 0., 1.) : 0;
                const auto percent = usage ? std::to_string(unsigned(std::round(usage->fraction * 100))) + "%" : "--";
                const auto caption = !usage         ? (english ? L"Unavailable" : L"不可用")
                                     : data.stale   ? (english ? L"Cached" : L"缓存")
                                     : data.loading ? (english ? L"Reading" : L"读取中")
                                                    : productivity::wide(settings.at("drive").text.substr(0, 2));
                impl_->label(layer, caption, {177, 99, 85, 28}, 22, data.stale ? kAmber : kCream);
                pixelDigits(layer, percent.substr(0, percent == "--" ? 2 : percent.size() - 1), {268, 102, 89, 30},
                            kCream, true);
                if (usage) {
                    impl_->label(layer, L"%", {359, 106, 25, 25}, 23);
                }
                const auto available = usage ? pixelBytes(usage->available) : ByteLabel{"--", L""};
                impl_->label(
                    layer,
                    (english ? L"Free " : L"可用 ") + productivity::wide(available.number) + L" " + available.unit,
                    {201, 132, 183, 22}, 17, kMint);
                layer.box(172, 174, 214, 12, 0, 0x294052ff);
                for (unsigned i = 0; i < 24; ++i) {
                    if (usage && (i + .5) / 24 <= fraction) {
                        layer.box(173 + i * 8.8, 175, 7, 10, 0, i < 16 ? kMint : i < 21 ? kAmber : kClay);
                    }
                }
            } else {
                impl_->label(layer, productivity::wide(settings.at("drive").text.substr(0, 2)), {64, 64, 80, 51}, 39);
                const auto used = usage ? pixelBytes(usage->used, false, usage->total) : ByteLabel{"--", L""};
                const auto total = usage ? pixelBytes(usage->total) : ByteLabel{"--", L""};
                pixelDigits(layer, used.number, {68, 223, 93, 32}, kCream);
                impl_->label(layer, L"/ " + productivity::wide(total.number) + L" " + total.unit, {165, 222, 146, 34},
                             28);
                const double fraction =
                    usage ? std::clamp(data.needles[0] >= 0 ? data.needles[0] : usage->fraction, 0., 1.) : 0;
                for (unsigned i = 0; i < 20; ++i) {
                    const auto accent = i < 12 ? kMint : i < 16 ? kAmber : kClay;
                    layer.box(69 + i * 11.3, 171, 9.3, 28, 0, usage && (i + .5) / 20 <= fraction ? accent : 0x294052ff);
                }
                if (!usage || data.stale || data.loading) {
                    const auto message = data.stale     ? (english ? L"Cached capacity" : L"缓存容量")
                                         : data.loading ? (english ? L"Reading capacity" : L"正在读取容量")
                                                        : (english ? L"Drive unavailable" : L"磁盘不可用");
                    impl_->label(layer, message, {68, 135, 202, 23}, 18, data.stale ? kAmber : kCream);
                }
                impl_->label(layer, english ? L"Available" : L"可用", {65, 291, 70, 34}, 27);
                const auto available = usage ? pixelBytes(usage->available) : ByteLabel{"--", L""};
                pixelDigits(layer, available.number, {145, 285, 103, 43}, kMint);
                impl_->label(layer, available.unit, {250, 298, 60, 29}, 27, kMint);
            }
        });
        impl_->icons.paint(layer,
                           {impl_->x + (impl_->compact ? 181 : 280) * impl_->scale,
                            impl_->y + (impl_->compact ? 135 : 132) * impl_->scale,
                            (impl_->compact ? 17 : 30) * impl_->scale, (impl_->compact ? 17 : 25) * impl_->scale},
                           "hard-drive", kMint);
    });
    return true;
}

bool DiskFace::save(const std::filesystem::path& path, const CdFrame& frame) {
    return impl_->canvas.save(path.wstring(), frame);
}

uint64_t DiskFace::cacheBuilds() const {
    return impl_->builds;
}
}  // namespace pixeldesk
