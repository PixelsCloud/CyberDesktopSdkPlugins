#include <sdk/Icons.h>
#include <sdk/Language.h>
#include <sdk/Update.h>
#include <sdk/WindowsFonts.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>
#include <optional>

#include "plugins/pixel-desk/PixelDigits.h"
#include "plugins/pixel-desk/PixelImages.h"
#include "plugins/pixel-desk/PixelLayout.h"
#include "plugins/pixel-desk/PixelPanels.h"
#include "plugins/pixel-desk/PixelParts.h"
#include "plugins/pixel-desk/PixelProductivity.h"

namespace pixeldesk {
using cybercanvas::Canvas;
using cybercanvas::Rect;
using Microsoft::WRL::ComPtr;

const ProductivitySpec& productivitySpec(std::string_view id) {
    for (const auto& spec : kProductivityCatalog) {
        if (id == spec.id) {
            return spec;
        }
    }
    throw std::invalid_argument("Unknown Pixel Desk productivity widget");
}

CdResult createProductivity(const CdCreateContext* context, CdInstanceApi* output, uint32_t size) {
    if (!context || context->struct_size < sizeof(CdCreateContext) || !context->plugin_id) {
        return CD_ERROR_ARGUMENT;
    }
    try {
        const auto& spec = productivitySpec(context->plugin_id);
        return productivity::create({spec.id, spec.kind, int(spec.rows), int(spec.columns), true}, context, output,
                                    size);
    } catch (...) {
        return CD_ERROR_ARGUMENT;
    }
}

ByteLabel pixelBytes(uint64_t bytes, bool rate, uint64_t scaleBytes) {
    constexpr const wchar_t* units[]{L"B", L"KiB", L"MiB", L"GiB", L"TiB", L"PiB", L"EiB"};
    double basis = double(scaleBytes ? scaleBytes : bytes), divisor = 1;
    unsigned unit = 0;
    while (basis >= 1024 && unit + 1 < std::size(units)) {
        basis /= 1024;
        divisor *= 1024;
        ++unit;
    }
    const double amount = double(bytes) / divisor;
    const unsigned precision =
        rate ? (!unit || amount >= 100 ? 0 : 1) : (!unit || amount == std::floor(amount) ? 0 : 1);
    return {std::format("{:.{}f}", amount, precision), std::wstring(units[unit]) + (rate ? L"/s" : L"")};
}

NetworkGeometry networkGeometry(double width, double height, bool compact) {
    const double worldWidth = compact ? 434 : 714, worldHeight = compact ? 234 : 203;
    const auto bounds = fitBody(width, height, worldWidth, worldHeight);
    return {bounds, bounds.width / worldWidth, worldWidth};
}

namespace {
constexpr uint32_t kDownload = 0x85d7aaff, kUpload = 0xe89c64ff;

void meter(Canvas& layer, double center, double fraction, bool valid, uint32_t) {
    if (!valid) {
        return;
    }
    const double angle = (150 + std::clamp(fraction, 0., 1.) * 240) * std::numbers::pi / 180;
    const cybercanvas::Point tip{center + std::cos(angle) * 45, 101 + std::sin(angle) * 45};
    const cybercanvas::Point side{std::cos(angle + std::numbers::pi / 2) * 5,
                                  std::sin(angle + std::numbers::pi / 2) * 5};
    auto pointer = layer.path();
    pointer.moveTo(tip)
        .lineTo({center + side.x, 101 + side.y})
        .lineTo({center - std::cos(angle) * 8, 101 - std::sin(angle) * 8})
        .lineTo({center - side.x, 101 - side.y})
        .close();
    layer.stroke(pointer, kInk, {.width = 4, .join = cybercanvas::LineJoin::Bevel});
    layer.fill(pointer, kCream);
    pixelDisc(layer, center, 101, 10, kInk);
    pixelDisc(layer, center, 101, 7, kCream);
    pixelDisc(layer, center, 101, 2, kClay);
}

void history(Canvas& layer, const productivity::Data& data, bool upload, Rect area, uint32_t color = 0) {
    if (data.history.empty()) {
        return;
    }
    double maximum = 1024;
    for (const auto& item : data.history) {
        maximum = std::max(maximum, double(upload ? item.second : item.first));
    }
    const auto begin = data.history.size() > 60 ? data.history.size() - 60 : 0;
    const double step = area.width / 60;
    for (size_t i = begin; i < data.history.size(); ++i) {
        const auto& item = data.history[i];
        const double value = double(upload ? item.second : item.first);
        const double x = area.x + (60 - (data.history.size() - begin) + i - begin) * step;
        const double height = std::max(2., std::round(value / maximum * area.height / 2) * 2);
        layer.box(x, area.y + area.height - height, std::max(1., step - 1.5), height, 0,
                  color    ? color
                  : upload ? kUpload
                           : kDownload);
    }
}
}  // namespace

struct NetworkFace::Impl {
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
    double worldWidth = 952;
    bool compact = false;
    uint64_t builds = 0;

    void label(Canvas& layer, std::wstring text, Rect area, double size, uint32_t color = kCream,
               cybercanvas::TextAlign align = cybercanvas::TextAlign::Center) {
        cybercanvas::TextStyle style;
        style.family = fonts.family();
        style.locale = cdsdk::drawingLocale();
        style.size = size;
        style.bold = true;
        style.align = align;
        style.vertical = cybercanvas::VerticalAlign::Center;
        const auto fit = layer.fitTextInk(text, style, area);
        layer.brush->SetColor(cybercanvas::color(color));
        layer.brush->SetOpacity(1);
        layer.target->DrawTextLayout(D2D1::Point2F(float(fit.origin.x), float(fit.origin.y)), fit.layout.Get(),
                                     layer.brush.Get());
    }

    void prepare(double width, double height, double dpi) {
        const auto geometry = networkGeometry(width, height, compact);
        scale = geometry.scale;
        x = geometry.artwork.x;
        y = geometry.artwork.y;
        worldWidth = geometry.worldWidth;
        const auto edge = uint32_t(std::clamp(std::ceil(1000 * scale * dpi / 64) * 64, 64., 4096.));
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
                                   if (!compact) {
                                       panelStrip(layer, *body, {-2, -2, 956, 207}, 714, 194, 762);
                                   } else {
                                       layer.image(*body, {-2, -2, 438, 238},
                                                   {.fit = cybercanvas::ImageFit::Stretch,
                                                    .clip = false,
                                                    .sampling = cybercanvas::ImageSampling::Nearest});
                                   }
                               }
                               if (!compact) {
                                   pixelGauge(layer, 96, 101, 82);
                                   pixelGauge(layer, 614, 101, 82);
                                   for (double row = 40; row < 166; row += 9) {
                                       layer.box(356, row, 2, 4, 0, kMint);
                                   }
                               }
                           });
        plate = surface.snapshot();
        owner = canvas.target;
        ++builds;
    }
};

NetworkFace::NetworkFace() : impl_(std::make_unique<Impl>()) {}

NetworkFace::~NetworkFace() = default;

void NetworkFace::bind(const CdCreateContext& context) {
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

void NetworkFace::pause(bool paused) {
    impl_->images.pause(paused);
    impl_->redraw.pause(paused);
}

bool NetworkFace::ready() const {
    return impl_->body && !impl_->images.pending() && impl_->images.get("body").image == impl_->body;
}

bool NetworkFace::render(const cdsdk::Settings& settings, const productivity::Data& data, const CdFrame& frame,
                         CdRect safe) {
    if (!impl_->canvas.valid(frame)) {
        return false;
    }
    impl_->redraw.beginFrame();
    const cdsdk::LanguageScope language(settings.at("language").text);
    impl_->fonts.select(impl_->canvas.nativeTextFactory(), settings.at("font_family").text);
    impl_->canvas.fontCollection(impl_->fonts.collection());
    impl_->canvas.render(frame, safe, [&](Canvas& layer, CdRect area) {
        impl_->prepare(area.width, area.height, frame.device_pixel_ratio);
        layer.target->DrawBitmap(impl_->plate.Get(), D2D1::RectF(0, 0, float(area.width), float(area.height)), 1,
                                 D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
        layer.saved([&] {
            layer.translate(impl_->x, impl_->y);
            layer.scale(impl_->scale, impl_->scale);
            if (impl_->compact) {
                for (unsigned channel = 0; channel < 2; ++channel) {
                    const auto rate = channel ? data.up : data.down;
                    const auto formatted = pixelBytes(rate, true);
                    const double row = 116 + channel * 36.;
                    pixelDigits(layer, data.valid ? formatted.number : "--", {128, row, 135, 30}, kCream, true,
                                cybercanvas::TextAlign::End);
                    impl_->label(layer, data.valid ? formatted.unit : L"B/s", {272, row + 8, 100, 24}, 22, kCream,
                                 cybercanvas::TextAlign::Start);
                }
                if (settings.at("history").integer && data.valid) {
                    history(layer, data, false, {92, 201, 134, 5}, 0x477c62ff);
                    history(layer, data, true, {239, 201, 134, 5}, 0xa15f3aff);
                }
            } else {
                for (unsigned channel = 0; channel < 2; ++channel) {
                    const uint64_t rate = channel ? data.up : data.down;
                    const double offset = channel * 163.;
                    const uint32_t accent = channel ? kUpload : kDownload;
                    const double fraction = data.needles[channel] >= 0
                                                ? data.needles[channel]
                                                : std::log1p(double(rate) / 1024) / std::log1p(1048576.);
                    meter(layer, channel ? 614 : 96, fraction, data.valid, accent);
                    const bool english = settings.at("language").text == "en";
                    impl_->label(layer, channel ? (english ? L"Upload" : L"上传") : (english ? L"Download" : L"下载"),
                                 {offset + 228, 38, 121, 30}, 28);
                    const auto formatted = pixelBytes(rate, true);
                    pixelDigits(layer, data.valid ? formatted.number : "--", {offset + 201, 79, 100, 48}, kCream);
                    impl_->label(layer, data.valid ? formatted.unit : L"B/s", {offset + 303, 99, 49, 25}, 21);
                }
                if (settings.at("history").integer && data.valid) {
                    history(layer, data, false, {201, 139, 148, 25});
                    history(layer, data, true, {364, 139, 148, 25});
                }
            }
        });
        for (unsigned channel = 0; channel < 2; ++channel) {
            const Rect icon =
                impl_->compact ? Rect{96, 119 + channel * 36., 24, 24} : Rect{203 + channel * 163., 42, 22, 26};
            impl_->icons.paint(layer,
                               {impl_->x + icon.x * impl_->scale, impl_->y + icon.y * impl_->scale,
                                icon.width * impl_->scale, icon.height * impl_->scale},
                               channel ? "arrow-up" : "arrow-down",
                               data.valid ? (channel ? kUpload : kDownload) : 0x65828aff);
        }
    });
    return true;
}

bool NetworkFace::save(const std::filesystem::path& path, const CdFrame& frame) {
    return impl_->canvas.save(path.wstring(), frame);
}

uint64_t NetworkFace::cacheBuilds() const {
    return impl_->builds;
}
}  // namespace pixeldesk

namespace productivity {
struct Renderer::Impl {
    std::unique_ptr<pixeldesk::NetworkFace> network;
    std::unique_ptr<pixeldesk::DiskFace> disk;
    std::unique_ptr<pixeldesk::TimerFace> timer;
    std::unique_ptr<pixeldesk::CountdownFace> countdown;
};

Renderer::Renderer() : impl(std::make_unique<Impl>()) {}

Renderer::~Renderer() = default;

void Renderer::bind(const CdCreateContext& context, std::function<void(uint32_t)> action) {
    if (pixeldesk::productivitySpec(context.plugin_id).kind == Kind::Timer) {
        impl->timer = std::make_unique<pixeldesk::TimerFace>();
        impl->timer->bind(context, std::move(action));
    } else if (pixeldesk::productivitySpec(context.plugin_id).kind == Kind::Countdown) {
        impl->countdown = std::make_unique<pixeldesk::CountdownFace>();
        impl->countdown->bind(context);
    } else if (pixeldesk::productivitySpec(context.plugin_id).kind == Kind::Disk) {
        impl->disk = std::make_unique<pixeldesk::DiskFace>();
        impl->disk->bind(context);
    } else {
        impl->network = std::make_unique<pixeldesk::NetworkFace>();
        impl->network->bind(context);
    }
}

void Renderer::pause(bool paused) {
    if (impl->countdown) {
        impl->countdown->pause(paused);
    }
    if (impl->network) {
        impl->network->pause(paused);
    }
    if (impl->disk) {
        impl->disk->pause(paused);
    }
    if (impl->timer) {
        impl->timer->pause(paused);
    }
}

void Renderer::input(const CdInput& event) {
    if (impl->timer) {
        impl->timer->input(event);
    }
}

void Renderer::cancelInput() {
    if (impl->timer) {
        impl->timer->cancelInput();
    }
}

void Renderer::syncInput(Spec, const cdsdk::Settings& settings, const Data& data, CdRect safe) {
    if (impl->timer) {
        impl->timer->syncInput(settings, data, safe);
    }
}

bool Renderer::render(Spec spec, const cdsdk::Settings& settings, const Data& data, const CdFrame& frame, CdRect safe) {
    if (spec.kind == Kind::Countdown && impl->countdown) {
        return impl->countdown->render(settings, data, frame, safe);
    }
    return spec.kind == Kind::Network && impl->network ? impl->network->render(settings, data, frame, safe)
           : spec.kind == Kind::Disk && impl->disk     ? impl->disk->render(settings, data, frame, safe)
           : spec.kind == Kind::Timer && impl->timer   ? impl->timer->render(settings, data, frame, safe)
                                                       : false;
}

bool Renderer::savePng(const std::filesystem::path& path, const CdFrame& frame) {
    if (impl->countdown) {
        return impl->countdown->save(path, frame);
    }
    return impl->network ? impl->network->save(path, frame)
           : impl->disk  ? impl->disk->save(path, frame)
                         : impl->timer && impl->timer->save(path, frame);
}
}  // namespace productivity
