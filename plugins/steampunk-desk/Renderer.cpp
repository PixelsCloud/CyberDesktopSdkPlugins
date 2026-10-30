#include <algorithm>
#include <cmath>
#include <optional>
#include <tuple>

#include "plugins/steampunk-desk/SteamDesk.h"
#include "plugins/steampunk-desk/SteamSprite.h"

namespace steamdesk {
using cybercanvas::Canvas;
using Microsoft::WRL::ComPtr;

namespace {
void clockCase(Canvas& canvas, const cybercanvas::ImageData& image, MatrixKind kind, cybercanvas::Rect bounds) {
    const auto visible = caseRegistration(kind);
    const double sourceScale = kCaseSize.width / visible.width;
    const double naturalHeight = visible.height * sourceScale;
    const double extra = kCaseSize.height - naturalHeight;
    const double scale = bounds.width / kCaseSize.width;
    // Change only the straight central band. Crowns, curved bezels, feet, maker
    // plates and fasteners above/below it keep their uniform source scale.
    const std::array<double, 4> from{0, naturalHeight * .45, naturalHeight * .55, naturalHeight};
    const std::array<double, 4> to{0, from[1], from[2] + extra, kCaseSize.height};
    for (unsigned i = 0; i < 3; ++i) {
        sprite(canvas, image, kCaseSource,
               {visible.x, visible.y + from[i] / sourceScale, visible.width, (from[i + 1] - from[i]) / sourceScale},
               {bounds.x, bounds.y + to[i] * scale, bounds.width, (to[i + 1] - to[i]) * scale});
    }
}

void cell(Canvas& canvas, double x, double y, double size, uint32_t color, bool round) {
    if (!round) {
        canvas.box(x, y, size, size, size * .09, color);
        return;
    }
    canvas.oval(x + size / 2, y + size / 2, size / 2, size / 2, color);
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
        images.set("body", path, 1, edge);
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
                layer.box(g.aperture.x, g.aperture.y, g.aperture.width, g.aperture.height, 0, 0x100f0dff);
                if (body) {
                    clockCase(layer, *body, spec.kind, g.artwork);
                }
                const double inset = (g.pitch - g.diameter) / 2;
                for (unsigned row = 0; row < g.rows; ++row) {
                    for (unsigned column = 0; column < g.columns; ++column) {
                        cell(layer, g.grid.x + column * g.pitch + inset, g.grid.y + row * g.pitch + inset, g.diameter,
                             0x24211bff, mechanicalpixels::isRound(spec.kind));
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
                                  D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
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
}  // namespace steamdesk
