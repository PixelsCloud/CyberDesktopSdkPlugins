#include <sdk/Language.h>
#include <sdk/WindowsFonts.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>

#include "plugins/pixel-desk/PixelDigits.h"
#include "plugins/pixel-desk/PixelImages.h"
#include "plugins/pixel-desk/PixelLayout.h"
#include "plugins/pixel-desk/PixelMonitor.h"
#include "plugins/pixel-desk/PixelParts.h"

namespace pixeldesk {
using cybercanvas::Canvas;
using cybercanvas::Point;
using cybercanvas::Rect;
using Microsoft::WRL::ComPtr;

const MonitorSpec& monitorSpec(std::string_view id) {
    for (const auto& spec : kMonitorCatalog) {
        if (id == spec.id) {
            return spec;
        }
    }
    throw std::invalid_argument("Unknown Pixel Desk system monitor");
}

CdResult createMonitor(const CdCreateContext* context, CdInstanceApi* output, uint32_t size) {
    if (!context || context->struct_size < sizeof(CdCreateContext) || !context->plugin_id) {
        return CD_ERROR_ARGUMENT;
    }
    try {
        const auto& spec = monitorSpec(context->plugin_id);
        const auto result =
            monitorplugin::create({spec.id, int(spec.rows), int(spec.columns), true}, context, output, size);
        if (result != CD_OK || spec.columns != 1) {
            return result;
        }
        const CdSettingValue primary{sizeof(CdSettingValue), CD_SETTING_CHOICE, "primary", 0, 0, "ram"};
        const auto applied = output->apply_settings(output->instance, &primary, 1);
        if (applied != CD_OK) {
            output->destroy(output->instance);
            *output = {};
        }
        return applied;
    } catch (...) {
        return CD_ERROR_ARGUMENT;
    }
}

namespace {
Point polar(double radius, double angle) {
    const double radians = angle * std::numbers::pi / 180;
    return {std::round((100 + radius * std::cos(radians)) / 2) * 2,
            std::round((100 + radius * std::sin(radians)) / 2) * 2};
}

void needle(Canvas& canvas, double value) {
    const double angle = 150 + std::clamp(value, 0., 1.) * 240;
    const double radians = angle * std::numbers::pi / 180;
    const Point tip = polar(55, angle);
    const Point side{std::cos(radians + std::numbers::pi / 2) * 5, std::sin(radians + std::numbers::pi / 2) * 5};
    auto path = canvas.path();
    path.moveTo(tip)
        .lineTo({100 + side.x, 100 + side.y})
        .lineTo(polar(7, angle + 180))
        .lineTo({100 - side.x, 100 - side.y})
        .close();
    canvas.stroke(path, kInk, {.width = 4, .join = cybercanvas::LineJoin::Bevel});
    canvas.fill(path, kCream);
    canvas.line(100, 100, tip.x, tip.y, 0xfff9e7ff, 1.5);
    pixelDisc(canvas, 100, 100, 12, kInk);
    pixelDisc(canvas, 100, 100, 9, kCream);
    pixelDisc(canvas, 100, 100, 3, kClay);
}

struct FaceLayout {
    double width, height;
    std::array<Point, 4> centers;
    double dialScale;
};

FaceLayout faceLayout(monitorplugin::Spec spec) {
    // Register the full inner bezel to the source case, not the old painted
    // scale. The source apertures are larger and higher than the former mask.
    if (spec.rows == 2) {
        return {327, 338, {{{85, 85}, {243, 85}, {85, 248}, {243, 248}}}, .78};
    }
    if (spec.columns == 4) {
        return {603, 170, {{{88, 85}, {230, 85}, {373, 85}, {515, 85}}}, .75};
    }
    if (spec.columns == 2) {
        return {529, 250, {{{124, 125}, {406, 125}}}, 1.16};
    }
    return {258, 264, {{{128, 132}}}, 1.25};
}
}  // namespace

struct MonitorFace::Impl {
    struct Key {
        double width, height, dpi;
        unsigned columns, rows;
        const cybercanvas::ImageData* body;
        bool operator==(const Key&) const = default;
    };

    cdsdk::Canvas canvas;
    cdsdk::Images images;
    cdsdk::WindowsFonts fonts{canvas.nativeTextFactory()};
    std::filesystem::path path;
    std::shared_ptr<const cybercanvas::ImageData> body;
    std::optional<Key> key;
    ComPtr<ID2D1RenderTarget> owner;
    cybercanvas::SurfaceImage plate;
    double scale = 1, x = 0, y = 0;
    uint64_t builds = 0;

    void label(Canvas& layer, std::wstring text, Rect area, double size) {
        cybercanvas::TextStyle style;
        style.family = fonts.family();
        style.locale = cdsdk::drawingLocale();
        style.size = size;
        style.bold = true;
        style.align = cybercanvas::TextAlign::Center;
        style.vertical = cybercanvas::VerticalAlign::Center;
        const auto fit = layer.fitTextInk(text, style, area);
        layer.brush->SetOpacity(1);
        // Keep the dial name legible when an animated needle passes behind it.
        layer.brush->SetColor(cybercanvas::color(kInk));
        for (int y : {-2, 0, 2}) {
            for (int x : {-2, 0, 2}) {
                if (x || y) {
                    layer.target->DrawTextLayout(D2D1::Point2F(float(fit.origin.x + x), float(fit.origin.y + y)),
                                                 fit.layout.Get(), layer.brush.Get());
                }
            }
        }
        layer.brush->SetColor(cybercanvas::color(kCream));
        layer.target->DrawTextLayout(D2D1::Point2F(float(fit.origin.x), float(fit.origin.y)), fit.layout.Get(),
                                     layer.brush.Get());
    }

    void prepare(monitorplugin::Spec spec, double width, double height, double dpi) {
        const auto g = faceLayout(spec);
        const auto bounds = fitBody(width, height, g.width - 4, g.height - 4);
        scale = bounds.width / (g.width - 4);
        x = bounds.x - 2 * scale;
        y = bounds.y - 2 * scale;
        const auto edge =
            uint32_t(std::clamp(std::ceil(std::max(g.width, g.height) * scale * dpi / 64) * 64, 64., 4096.));
        pixeldesk::requestPixelImage(images, "body", path, 1, edge, dpi);
        images.poll();
        if (const auto next = images.get("body").image) {
            body = next;
        }
        const Key next{width, height, dpi, unsigned(spec.columns), unsigned(spec.rows), body.get()};
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
                                   layer.image(*body, {0, 0, g.width, g.height},
                                               {.fit = cybercanvas::ImageFit::Stretch,
                                                .clip = false,
                                                .sampling = cybercanvas::ImageSampling::Nearest});
                               }
                               for (unsigned i = 0; i < (spec.rows == 2 ? 4u : unsigned(spec.columns)); ++i) {
                                   layer.saved([&] {
                                       layer.translate(g.centers[i].x, g.centers[i].y);
                                       layer.scale(g.dialScale, g.dialScale);
                                       pixelGauge(layer, 0, 0, 91);
                                   });
                               }
                           });
        plate = surface.snapshot();
        owner = canvas.target;
        ++builds;
    }
};

MonitorFace::MonitorFace() : impl_(std::make_unique<Impl>()) {}

MonitorFace::~MonitorFace() = default;

void MonitorFace::bind(const CdCreateContext& context, std::function<void()> changed) {
    impl_->images.bind(context);
    impl_->canvas.bindGpu(context);
    impl_->images.onChange(std::move(changed));
    impl_->path =
        std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(context.plugin_directory))) /
        "assets/body.png";
}

void MonitorFace::pause(bool paused) {
    impl_->images.pause(paused);
}

bool MonitorFace::ready() const {
    return impl_->body && !impl_->images.pending() && impl_->images.get("body").image == impl_->body;
}

bool MonitorFace::render(monitorplugin::Spec spec, const monitorplugin::Options& options, monitorplugin::Values values,
                         const CdFrame& frame, CdRect safe) {
    if (!impl_->canvas.valid(frame)) {
        return false;
    }
    const cdsdk::LanguageScope language(options.language);
    impl_->fonts.select(impl_->canvas.nativeTextFactory(), options.fontFamily);
    impl_->canvas.fontCollection(impl_->fonts.collection());
    impl_->canvas.render(frame, safe, [&](Canvas& layer, CdRect area) {
        impl_->prepare(spec, area.width, area.height, frame.device_pixel_ratio);
        layer.target->DrawBitmap(impl_->plate.Get(), D2D1::RectF(0, 0, float(area.width), float(area.height)), 1,
                                 D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
        layer.translate(impl_->x, impl_->y);
        layer.scale(impl_->scale, impl_->scale);
        const unsigned columns = spec.rows == 2 ? 2 : spec.columns, count = columns * spec.rows;
        const int primary = options.primary == "ram"           ? 1
                            : options.primary == "gpu"         ? 2
                            : options.primary == "temperature" ? 3
                                                               : 0;
        const std::array<double, 4> data{values.cpu, values.ram, values.gpu, values.temperature};
        const auto moving = options.needles;
        const std::array<double, 4> needles{moving.cpu, moving.ram, moving.gpu, moving.temperature};
        constexpr uint32_t flags[]{CD_SYSTEM_CPU_VALID, CD_SYSTEM_MEMORY_VALID, CD_SYSTEM_GPU_VALID,
                                   CD_SYSTEM_CPU_TEMPERATURE_VALID};
        constexpr const wchar_t* names[]{L"CPU", L"RAM", L"GPU", L"TEMP"};
        for (unsigned i = 0; i < count; ++i) {
            layer.saved([&] {
                const auto g = faceLayout(spec);
                layer.translate(g.centers[i].x - 100 * g.dialScale, g.centers[i].y - 100 * g.dialScale);
                layer.scale(g.dialScale, g.dialScale);
                const unsigned metric = count == 1 ? unsigned(primary) : i;
                const bool valid = values.valid & flags[metric];
                if (valid) {
                    needle(layer, (options.interpolate ? needles[metric] : data[metric]) / (metric == 3 ? 120 : 100));
                }
                impl_->label(layer, names[metric], {62, 50, 76, 21}, 21);
                const auto number = valid ? std::to_string(std::llround(data[metric])) : "--";
                const double numberWidth = (number.size() * 6 - 1) * 4;
                const double unitWidth = metric == 3 ? 34 : 25;
                const double left = (200 - numberWidth - unitWidth - 4) / 2;
                pixelDigits(layer, number, {left, 136, numberWidth, 28}, kCream);
                impl_->label(layer, metric == 3 ? L"\u00b0C" : L"%", {left + numberWidth + 4, 135, unitWidth, 30}, 29);
            });
        }
    });
    return true;
}

bool MonitorFace::save(const std::filesystem::path& path, const CdFrame& frame) {
    return impl_->canvas.save(path.wstring(), frame);
}

uint64_t MonitorFace::cacheBuilds() const {
    return impl_->builds;
}
}  // namespace pixeldesk

namespace monitorplugin {
struct Renderer::Impl {
    pixeldesk::MonitorFace face;
};

Renderer::Renderer() : m_impl(std::make_unique<Impl>()) {}

Renderer::~Renderer() = default;

void Renderer::bind(const CdCreateContext& context, std::function<void()> changed) {
    m_impl->face.bind(context, std::move(changed));
}

void Renderer::pause(bool paused) {
    m_impl->face.pause(paused);
}

bool Renderer::render(Spec spec, const Options& options, Values values, const CdFrame& frame, CdRect safe) {
    return m_impl->face.render(spec, options, values, frame, safe);
}

bool Renderer::savePng(const std::wstring& path, const CdFrame& frame, CdColor) {
    return m_impl->face.save(path, frame);
}
}  // namespace monitorplugin
