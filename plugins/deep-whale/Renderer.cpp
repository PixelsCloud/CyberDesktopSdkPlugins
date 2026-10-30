#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <tuple>

#include "plugins/deep-whale/DeepWhale.h"
#include "plugins/deep-whale/HorizontalFrame.h"

namespace deepwhale {
using cybercanvas::Canvas;
using Microsoft::WRL::ComPtr;

namespace {
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
    std::function<void(const std::string&)> report;
    std::map<std::string, std::string> errors;
    const Spec& spec;
    std::filesystem::path path;
    std::shared_ptr<const cybercanvas::ImageData> body;
    std::optional<Key> key;
    ComPtr<ID2D1RenderTarget> owner;
    cybercanvas::SurfaceImage background;
    Geometry geometry;
    uint64_t builds = 0;

    Impl(const CdCreateContext& context, const Spec& value) : spec(value) {
        canvas.bindGpu(context);
        images.bind(context);
        if (context.host && context.host->log) {
            const auto log = context.host->log;
            const auto user = context.host_context;
            report = [log, user](const std::string& message) {
                log(user, 3, message.data(), uint32_t(message.size()));
            };
        }

        path = std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(context.plugin_directory))) /
               "assets" / "body.png";
    }

    void prepare(const cdsdk::Settings& settings, double width, double height, double dpi) {
        const bool seconds = settings.at("seconds").integer != 0;
        const auto nextGeometry = arrange(spec, width, height, dpi, seconds);
        const auto edge = uint32_t(std::clamp(std::ceil(nextGeometry.originalBody.width * dpi / 64) * 64, 64., 4096.));
        const auto region = artworkRegion(spec.kind);
        images.set("body", path, 1, edge, region);
        images.poll();
        const auto snapshot = images.get("body");
        if (snapshot.state == cybercanvas::ImageState::Failed && errors["body"] != snapshot.error) {
            errors["body"] = snapshot.error;
            if (report) {
                report("Deep Whale body.png: " + snapshot.error);
            }
        }
        body = snapshot.image;
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
                layer.box(g.aperture.x, g.aperture.y, g.aperture.width, g.aperture.height, 0, 0x183457ff);
                if (body) {
                    cybercanvas::OffscreenSurface original;
                    layer.renderLayer(original,
                                      {uint32_t(std::ceil(g.originalBody.width * dpi)),
                                       uint32_t(std::ceil(g.originalBody.height * dpi)), dpi},
                                      [&](Canvas& base) {
                                          base.image(*body, {0, 0, g.originalBody.width, g.originalBody.height},
                                                     {.fit = cybercanvas::ImageFit::Stretch, .clip = false});
                                      });
                    const bool round = mechanicalpixels::isRound(spec.kind);
                    const double first = round ? (700. - region.x) * g.originalBody.width / region.width : g.bodyCut;
                    const std::array cuts{first, g.bodyCut};
                    whaleskin::drawFrame(layer, original.bitmap(), g.originalBody, g.artwork,
                                         std::span<const double>(cuts).first(round ? 2 : 1));
                }
                const double insetX = (g.pitchX - g.diameter) / 2, insetY = (g.pitchY - g.diameter) / 2;
                for (unsigned row = 0; row < g.rows; ++row) {
                    for (unsigned column = 0; column < g.columns; ++column) {
                        if (!g.emitters[column]) {
                            continue;
                        }
                        cell(layer, g.grid.x + g.columnOffsets[column] + insetX, g.grid.y + row * g.pitchY + insetY,
                             g.diameter, 0x7ea7cc22, mechanicalpixels::isRound(spec.kind));
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
        const double insetX = (g.pitchX - g.diameter) / 2, insetY = (g.pitchY - g.diameter) / 2;
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
                const double x = g.grid.x + g.columnOffsets[column] + insetX, y = g.grid.y + row * g.pitchY + insetY;
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
    if (!paused && impl_->images.stats().failed) {
        impl_->images.clear();
        impl_->key.reset();
        impl_->errors.clear();
    }
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
}  // namespace deepwhale
