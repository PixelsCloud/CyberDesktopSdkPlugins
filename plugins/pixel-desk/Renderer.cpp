#include <algorithm>
#include <cmath>
#include <optional>
#include <tuple>

#include "plugins/pixel-desk/PixelDesk.h"
#include "plugins/pixel-desk/PixelImages.h"

namespace pixeldesk {
using cybercanvas::Canvas;
using Microsoft::WRL::ComPtr;

namespace {
void cell(Canvas& canvas, double x, double y, double size, uint32_t color, bool round) {
    if (!round) {
        canvas.box(x, y, size, size, 0, color);
        return;
    }
    // A stepped pixel-circle sprite, distinct from both square dots and smooth
    // vector LEDs. Direct2D still samples the widget at the host's actual DPI.
    constexpr unsigned inset[]{2, 1, 0, 0, 0, 1, 2};
    const double unit = size / 7;
    for (unsigned row = 0; row < 7; ++row) {
        canvas.box(x + inset[row] * unit, y + row * unit, size - inset[row] * unit * 2, unit, 0, color);
    }
}
}  // namespace

struct Renderer::Impl {
    using Key = std::tuple<double, double, double, bool, const cybercanvas::ImageData*>;
    cdsdk::Canvas canvas;
    cdsdk::Images images;
    const Spec& spec;
    std::filesystem::path path;
    std::shared_ptr<const cybercanvas::ImageData> body;
    std::optional<Key> key;
    ComPtr<ID2D1RenderTarget> owner;
    cybercanvas::SurfaceImage background;
    Geometry geometry;
    uint64_t builds = 0;

    Impl(const CdCreateContext& context, const Spec& value) : spec(value) {
        images.bind(context);
        canvas.bindGpu(context);
        path = std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(context.plugin_directory))) /
               "assets" / "body.png";
    }

    void prepare(const cdsdk::Settings& settings, double width, double height, double dpi) {
        const bool seconds = settings.at("seconds").integer != 0;
        const auto nextGeometry = arrange(spec, width, height, dpi, seconds);
        const auto edge = uint32_t(std::clamp(std::ceil(nextGeometry.artwork.width * dpi / 64) * 64, 64., 4096.));
        pixeldesk::requestPixelImage(images, "body", path, 1, edge, dpi);
        images.poll();
        body = images.get("body").image;
        const Key next{width, height, dpi, seconds, body.get()};
        if (owner == canvas.target && key == next) {
            return;
        }
        geometry = nextGeometry;
        cybercanvas::OffscreenSurface surface;
        canvas.renderLayer(
            surface, {uint32_t(std::ceil(width * dpi)), uint32_t(std::ceil(height * dpi)), dpi}, [&](Canvas& layer) {
                const auto& g = geometry;
                // The placeholder covers only the glass, never a host
                // cell. Artwork decoding and filesystem access run in
                // the SDK's shared background image workers.
                layer.box(g.aperture.x, g.aperture.y, g.aperture.width, g.aperture.height, 0, 0x14242eff);
                if (body) {
                    layer.image(*body, g.artwork,
                                {.fit = cybercanvas::ImageFit::Stretch,
                                 .clip = false,
                                 .sampling = cybercanvas::ImageSampling::Nearest});
                }
                const double inset = (g.pitch - g.diameter) / 2;
                for (unsigned row = 0; row < g.rows; ++row) {
                    for (unsigned column = 0; column < g.columns; ++column) {
                        cell(layer, g.grid.x + column * g.pitch + inset, g.grid.y + row * g.pitch + inset, g.diameter,
                             0x203341ff, mechanicalpixels::isRound(spec.kind));
                    }
                }
            });
        background = surface.snapshot();
        owner = canvas.target;
        key = next;
        ++builds;
    }

    void draw(const cdsdk::Settings& settings, std::string_view text, bool colon) {
        const auto size = background->GetSize();
        canvas.target->DrawBitmap(background.Get(), D2D1::RectF(0, 0, size.width, size.height), 1,
                                  D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
        const auto& g = geometry;
        const double inset = (g.pitch - g.diameter) / 2;
        const double brightness = settings.at("brightness").number / 100, glow = settings.at("glow").number / 100;
        for (unsigned row = 0; row < g.rows; ++row) {
            for (unsigned column = 0; column < g.columns; ++column) {
                if (!mechanicalpixels::clockCell(text, column + 1, row + 1, colon)) {
                    continue;
                }
                uint32_t rgba =
                    uint32_t(settings.at(text.size() > 5 && column >= 28 ? "seconds_color" : "text_color").integer);
                uint32_t color = rgba & 255;
                for (unsigned shift : {8u, 16u, 24u}) {
                    color |= uint32_t(std::round(((rgba >> shift) & 255) * brightness)) << shift;
                }
                const double x = g.grid.x + column * g.pitch + inset, y = g.grid.y + row * g.pitch + inset;
                if (glow > 0) {
                    const double halo = g.diameter * glow * .3;
                    cell(canvas, x - halo, y - halo, g.diameter + halo * 2,
                         (color & 0xffffff00) | uint32_t(48 * glow * (rgba & 255) / 255),
                         mechanicalpixels::isRound(spec.kind));
                }
                cell(canvas, x, y, g.diameter, color, mechanicalpixels::isRound(spec.kind));
                cell(canvas, x + g.diameter * .18, y + g.diameter * .14, g.diameter * .28,
                     0xffffff00 | uint32_t(std::round((rgba & 255) * .15 * brightness)),
                     mechanicalpixels::isRound(spec.kind));
            }
        }
    }
};

Renderer::Renderer(const CdCreateContext& context, const Spec& spec) : impl_(std::make_unique<Impl>(context, spec)) {}

Renderer::~Renderer() = default;

void Renderer::onChange(std::function<void()> callback) {
    impl_->images.onChange(std::move(callback));
}

void Renderer::pause(bool paused) {
    impl_->images.pause(paused);
}

bool Renderer::ready() const {
    return impl_->body && !impl_->images.pending() && impl_->images.get("body").image == impl_->body;
}

uint64_t Renderer::cacheBuilds() const {
    return impl_->builds;
}

cdsdk::ImageSlotStats Renderer::imageStats() const {
    return impl_->images.stats();
}

bool Renderer::render(const cdsdk::Settings& settings, std::string_view text, bool colon, const CdFrame& frame,
                      CdRect safe) {
    if (!impl_->canvas.valid(frame)) {
        return false;
    }
    impl_->canvas.render(frame, safe, [&](auto&, CdRect area) {
        impl_->prepare(settings, area.width, area.height, frame.device_pixel_ratio);
        impl_->draw(settings, text, colon);
    });
    return true;
}

bool Renderer::save(const std::filesystem::path& path, const CdFrame& frame) {
    return impl_->canvas.save(path.wstring(), frame);
}
}  // namespace pixeldesk
