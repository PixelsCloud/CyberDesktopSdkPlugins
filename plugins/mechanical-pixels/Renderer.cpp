#include <CyberCanvas/Sprites.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <tuple>

#include "plugins/mechanical-pixels/Pixels.h"

namespace mechanicalpixels {
using cybercanvas::Canvas;
using Microsoft::WRL::ComPtr;

namespace {
D2D1_RECT_F rectangle(cybercanvas::Rect value) {
    return D2D1::RectF(float(value.x), float(value.y), float(value.x + value.width), float(value.y + value.height));
}

void dot(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush, bool round, float x, float y, float diameter) {
    if (round) {
        target->FillEllipse(D2D1::Ellipse({x + diameter / 2, y + diameter / 2}, diameter / 2, diameter / 2), brush);
    } else {
        target->FillRectangle(D2D1::RectF(x, y, x + diameter, y + diameter), brush);
    }
}
}  // namespace

struct Renderer::Impl {
    using Key = std::tuple<Kind, double, double, double, bool, double, double, std::array<uint32_t, 4>,
                           const cybercanvas::ImageData*>;
    cdsdk::Canvas canvas;
    cdsdk::Images images;
    std::filesystem::path path;
    std::shared_ptr<const cybercanvas::ImageData> body;
    Geometry geometry;
    std::optional<Key> key;
    ComPtr<ID2D1RenderTarget> owner;
    ComPtr<ID2D1RoundedRectangleGeometry> mask;
    cybercanvas::SurfaceImage background;
    cybercanvas::SpritePalette lightPalette;
    double spriteSize = 0, halo = 0;
    uint64_t cacheBuilds = 0;

    explicit Impl(const CdCreateContext& context) {
        images.bind(context);
        canvas.bindGpu(context);
        path = std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(context.plugin_directory))) /
               "assets" / "body.png";
    }

    void prepare(Kind kind, const cdsdk::Settings& settings, double width, double height, double dpi) {
        const bool seconds = isClock(kind) && settings.at("seconds").integer;
        const auto nextGeometry = arrange(kind, width, height, dpi, seconds);
        const double fullWidth = nextGeometry.body.width;
        const auto edge = uint32_t(std::clamp(std::ceil(fullWidth * dpi / 64) * 64, 64., 4096.));
        images.set("body", path, 1, edge);
        images.poll();
        if (const auto next = images.get("body").image) {
            body = next;
        }
        const double brightness = settings.at("brightness").number / 100, glow = settings.at("glow").number / 100;
        const Key nextKey{kind, width, height, dpi, seconds, brightness, glow, palette(kind, settings), body.get()};
        if (owner == canvas.target && key == nextKey) {
            return;
        }
        geometry = nextGeometry;
        const auto& g = geometry;
        cdsdk::require(canvas.factory->CreateRoundedRectangleGeometry(
            D2D1::RoundedRect(rectangle(g.aperture), float(g.radius), float(g.radius)), mask.ReleaseAndGetAddressOf()));

        // Cache this widget's static frame and dormant matrix at its real DPI.
        // No decoding, full-grid rebuilding or material redraw occurs in live frames.
        cybercanvas::OffscreenSurface surface;
        canvas.renderLayer(
            surface, {uint32_t(std::ceil(width * dpi)), uint32_t(std::ceil(height * dpi)), dpi}, [&](Canvas& layer) {
                layer.box(g.aperture.x - 1, g.aperture.y - 1, g.aperture.width + 2, g.aperture.height + 2, g.radius,
                          0x101112ff);
                layer.masked(mask.Get(), [&] {
                    ComPtr<ID2D1SolidColorBrush> pen;
                    cdsdk::require(layer.target->CreateSolidColorBrush(cybercanvas::color(0x29292aff), &pen));
                    for (unsigned row = 0; row < g.rows; ++row) {
                        for (unsigned column = 0; column < g.columns; ++column) {
                            const double x = std::round((g.grid.x + column * g.pitch) * dpi) / dpi;
                            const double y = std::round((g.grid.y + row * g.pitch) * dpi) / dpi;
                            dot(layer.target.Get(), pen.Get(), isRound(kind), float(x), float(y), float(g.diameter));
                        }
                    }
                });
                if (body) {
                    // Extend only the straight middle section of long cases.
                    // Corner fasteners, bevels, feet and end shoulders keep
                    // their original uniform scale on every footprint.
                    const std::array<double, 4> from{0, g.body.width * .2, g.body.width * .8, g.body.width};
                    const std::array<double, 4> to{0, from[1], from[2] + g.extension, g.body.width + g.extension};
                    for (unsigned i = 0; i < 3; ++i) {
                        const double scale = (to[i + 1] - to[i]) / (from[i + 1] - from[i]);
                        layer.clipped({g.body.x + to[i], g.body.y, to[i + 1] - to[i], g.body.height}, [&] {
                            layer.image(
                                *body,
                                {g.body.x + to[i] - from[i] * scale, g.body.y, g.body.width * scale, g.body.height},
                                {.fit = cybercanvas::ImageFit::Stretch, .clip = false});
                        });
                    }
                }
            });
        background = surface.snapshot();

        halo = std::max(1. / dpi, g.diameter * .22 * glow);
        spriteSize = std::ceil((g.diameter + halo * 2) * dpi) / dpi;
        std::vector<cybercanvas::SurfaceImage> lights;
        for (unsigned row = 0; row <= g.rows; ++row) {
            const bool peak = row == g.rows;
            const auto rgba = cellColor(kind, settings, std::min(row, g.rows - 1), g.rows, peak);
            auto color = cybercanvas::color(rgba);
            color.r *= float(brightness);
            color.g *= float(brightness);
            color.b *= float(brightness);
            cybercanvas::OffscreenSurface lamp;
            canvas.renderLayer(
                lamp, {uint32_t(std::ceil(spriteSize * dpi)), uint32_t(std::ceil(spriteSize * dpi)), dpi},
                [&](Canvas& layer) {
                    ComPtr<ID2D1SolidColorBrush> pen;
                    cdsdk::require(layer.target->CreateSolidColorBrush(color, &pen));
                    if (glow > 0) {
                        auto emission = color;
                        emission.a = color.a * float(glow * .16);
                        pen->SetColor(emission);
                        dot(layer.target.Get(), pen.Get(), isRound(kind), 0, 0, float(g.diameter + halo * 2));
                    }
                    pen->SetColor(color);
                    dot(layer.target.Get(), pen.Get(), isRound(kind), float(halo), float(halo), float(g.diameter));
                    auto shine = color;
                    shine.r = std::lerp(shine.r, 1.f, .14f);
                    shine.g = std::lerp(shine.g, 1.f, .14f);
                    shine.b = std::lerp(shine.b, 1.f, .14f);
                    shine.a = color.a * .55f;
                    pen->SetColor(shine);
                    const float inset = float(g.diameter * .24);
                    dot(layer.target.Get(), pen.Get(), isRound(kind), float(halo) + inset, float(halo) + inset,
                        float(g.diameter) - inset * 2);
                });
            lights.emplace_back(lamp.snapshot());
        }
        lightPalette.build(canvas, lights);
        owner = canvas.target;
        key = nextKey;
        ++cacheBuilds;
    }

    void draw(Kind kind, const cdsdk::Settings& settings, std::string_view text, bool colonVisible,
              const widgetplugin::Spectrum& spectrum, double dpi) {
        const auto& g = geometry;
        const auto size = background->GetSize();
        canvas.target->DrawBitmap(background.Get(), D2D1::RectF(0, 0, size.width, size.height));
        canvas.masked(mask.Get(), [&] {
            lightPalette.begin(canvas);
            const auto light = [&](unsigned column, unsigned row, bool peak = false, float opacity = 1) {
                const double x = std::round((g.grid.x + column * g.pitch) * dpi) / dpi - halo;
                const double y = std::round((g.grid.y + row * g.pitch) * dpi) / dpi - halo;
                const size_t color = peak && (isClock(kind) || kind == Kind::MusicRound) ? g.rows : g.rows - 1 - row;
                lightPalette.add(unsigned(color), {x, y, spriteSize, spriteSize}, opacity);
            };
            if (isClock(kind)) {
                for (unsigned row = 0; row < g.rows; ++row) {
                    for (unsigned column = 0; column < g.columns; ++column) {
                        if (clockCell(text, column, row, colonVisible)) {
                            light(column, row, text.size() > 5 && column >= 29);
                        }
                    }
                }
                lightPalette.flush(canvas);
                return;
            }
            for (unsigned column = 0; column < g.columns; ++column) {
                const double position = double(column) * (widgetplugin::Spectrum::count - 1) / (g.columns - 1);
                const float level = std::clamp(spectrum.sample(position), 0.f, 1.f);
                for (unsigned row = 0; row < g.rows; ++row) {
                    const float opacity = std::clamp(level * g.rows - (g.rows - 1 - row), 0.f, 1.f);
                    if (opacity > 0) {
                        light(column, row, false, opacity);
                    }
                }
                const float peak = std::clamp(spectrum.sample(position, true), 0.f, 1.f);
                if (settings.at("peaks").integer && peak > level + .5f / g.rows) {
                    const auto row = g.rows - std::clamp(unsigned(std::ceil(peak * g.rows)), 1u, g.rows);
                    light(column, row, true);
                }
            }
            lightPalette.flush(canvas);
        });
    }
};

Renderer::Renderer(const CdCreateContext& context) : impl_(std::make_unique<Impl>(context)) {}

Renderer::~Renderer() = default;

void Renderer::onChange(std::function<void()> callback) {
    impl_->images.onChange(std::move(callback));
}

void Renderer::pause(bool paused) {
    impl_->images.pause(paused);
}

bool Renderer::ready() const {
    return impl_->body != nullptr;
}

bool Renderer::render(Kind kind, const cdsdk::Settings& settings, std::string_view text, bool colonVisible,
                      const widgetplugin::Spectrum& spectrum, const CdFrame& frame, CdRect safe) {
    if (!impl_->canvas.valid(frame)) {
        return false;
    }
    impl_->canvas.render(frame, safe, [&](auto&, CdRect area) {
        impl_->prepare(kind, settings, area.width, area.height, frame.device_pixel_ratio);
        impl_->draw(kind, settings, text, colonVisible, spectrum, frame.device_pixel_ratio);
    });
    return true;
}

uint64_t Renderer::cacheBuilds() const {
    return impl_->cacheBuilds;
}

bool Renderer::save(const std::filesystem::path& path, const CdFrame& frame) {
    return impl_->canvas.save(path.wstring(), frame);
}
}  // namespace mechanicalpixels
