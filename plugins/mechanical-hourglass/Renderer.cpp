#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <tuple>

#include "plugins/mechanical-hourglass/MechanicalHourglass.h"

namespace mechanicalhourglass {
using cybercanvas::Canvas;
using cybercanvas::Rect;
using Microsoft::WRL::ComPtr;

namespace {
D2D1_RECT_F rectangle(Rect r) {
    return D2D1::RectF(float(r.x), float(r.y), float(r.x + r.width), float(r.y + r.height));
}

uint32_t tone(uint32_t rgba, double multiplier, double lift = 0) {
    const auto channel = [&](unsigned shift) {
        return uint32_t(std::clamp(((rgba >> shift) & 255) * multiplier + lift, 0., 255.));
    };
    return (channel(24) << 24) | (channel(16) << 16) | (channel(8) << 8) | (rgba & 255);
}
}  // namespace

struct Renderer::Impl {
    using Key = std::tuple<bool, double, double, double, uint32_t, uint32_t, int64_t, const cybercanvas::ImageData*,
                           const cybercanvas::ImageData*>;
    cdsdk::Canvas canvas;
    cdsdk::Images images;
    std::filesystem::path directory;
    std::shared_ptr<const cybercanvas::ImageData> body, glass;
    std::optional<Key> key;
    Geometry geometry;
    ComPtr<ID2D1RenderTarget> owner;
    cybercanvas::SurfaceImage background, lamp;
    std::array<cybercanvas::SurfaceImage, 3> grains;
    double grainSide = 0, lampSide = 0, halo = 0;
    uint64_t builds = 0;

    explicit Impl(const CdCreateContext& context) {
        images.bind(context);
        canvas.bindGpu(context);
        directory =
            std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(context.plugin_directory))) /
            "assets";
    }

    void prepare(bool timer, double width, double height, double dpi, const cdsdk::Settings& settings) {
        geometry = arrange(timer, width, height);
        const auto edge = uint32_t(
            std::clamp(std::ceil(std::max(geometry.body.width, geometry.body.height) * dpi / 64) * 64, 64., 4096.));
        const auto glassEdge = uint32_t(std::clamp(std::ceil(geometry.jar.height * dpi / 64) * 64, 64., 4096.));
        images.set("body", directory / "body.png", 1, edge);
        images.set("glass", directory / "glass.png", 1, glassEdge);
        images.poll();
        if (const auto image = images.get("body").image) {
            body = image;
        }
        if (const auto image = images.get("glass").image) {
            glass = image;
        }
        const uint32_t sandColor = uint32_t(settings.at("sand_color").integer);
        const uint32_t textColor = timer ? uint32_t(settings.at("text_color").integer) : 0;
        const Key next{timer,      width,      height, dpi, sandColor, textColor, settings.at("glow").integer,
                       body.get(), glass.get()};
        if (key == next && owner == canvas.target) {
            return;
        }
        const auto& g = geometry;
        cybercanvas::OffscreenSurface frame;
        canvas.renderLayer(
            frame, {uint32_t(std::ceil(width * dpi)), uint32_t(std::ceil(height * dpi)), dpi}, [&](Canvas& layer) {
                if (body) {
                    if (!timer) {
                        layer.image(*body, g.body, {.fit = cybercanvas::ImageFit::Stretch, .clip = false});
                    } else {
                        const std::array<double, 4> from{0, g.sliceStart, g.sliceEnd, g.body.width};
                        const std::array<double, 4> to{0, g.sliceStart, g.sliceEnd + g.extension,
                                                       g.body.width + g.extension};
                        for (unsigned i = 0; i < 3; ++i) {
                            const double scale = (to[i + 1] - to[i]) / (from[i + 1] - from[i]);
                            layer.clipped(Rect{g.body.x + to[i], g.body.y, to[i + 1] - to[i], g.body.height}, [&] {
                                layer.image(
                                    *body,
                                    {g.body.x + to[i] - from[i] * scale, g.body.y, g.body.width * scale, g.body.height},
                                    {.fit = cybercanvas::ImageFit::Stretch, .clip = false});
                            });
                        }
                    }
                }
                if (timer) {
                    for (unsigned row = 0; row < 7; ++row) {
                        for (unsigned column = 0; column < kDigitColumns; ++column) {
                            layer.rectangle({g.display.x + column * g.pitch, g.display.y + row * g.pitch, g.pitch * .84,
                                             g.pitch * .84},
                                            0x282829ff);
                        }
                    }
                    for (unsigned i = 0; i < 16; ++i) {
                        layer.box(g.progress.x + i * g.progress.width / 16, g.progress.y, g.progress.width / 16 * .80,
                                  g.progress.height, g.progress.height * .08, 0x222323ff);
                    }
                }
            });
        background = frame.snapshot();

        grainSide = std::max(1. / dpi, g.jar.height * .028);
        for (unsigned i = 0; i < grains.size(); ++i) {
            cybercanvas::OffscreenSurface grain;
            canvas.renderLayer(grain, {uint32_t(std::ceil(grainSide * dpi)), uint32_t(std::ceil(grainSide * dpi)), dpi},
                               [&](Canvas& layer) {
                                   const auto color = tone(sandColor, .83 + i * .12);
                                   layer.gradientBox({0, 0, grainSide, grainSide}, grainSide * .14, tone(color, 1., 16),
                                                     tone(color, .72));
                                   layer.rectangle({grainSide * .13, grainSide * .12, grainSide * .70, grainSide * .15},
                                                   tone(color, 1., 30), .70);
                               });
            grains[i] = grain.snapshot();
        }
        halo = g.pitch * .08;
        lampSide = g.pitch * .84 + halo * 2;
        if (timer) {
            cybercanvas::OffscreenSurface light;
            canvas.renderLayer(
                light, {uint32_t(std::ceil(lampSide * dpi)), uint32_t(std::ceil(lampSide * dpi)), dpi},
                [&](Canvas& layer) {
                    layer.rectangle({0, 0, lampSide, lampSide}, textColor, double(settings.at("glow").integer) / 100);
                    layer.gradientBox({halo, halo, g.pitch * .84, g.pitch * .84}, g.pitch * .04,
                                      tone(textColor, 1., 12), tone(textColor, .92));
                });
            lamp = light.snapshot();
        }
        owner = canvas.target;
        key = next;
        ++builds;
    }

    void draw(bool timer, const Model& model, const PhysicalSand* physical, const cdsdk::Settings& settings) {
        const auto& g = geometry;
        const cybercanvas::Point center{g.jar.x + g.jar.width / 2, g.jar.y + g.jar.height / 2};
        canvas.saved([&] {
            // Project a flip around the actual horizontal trunnion, without rotating
            // the fixed case, stretching the entire widget or repainting the desktop.
            const double projection = std::cos(model.angle() * std::numbers::pi / 180);
            canvas.scale(1, std::abs(projection) < .005 ? .005 : projection, center);
            std::vector<cybercanvas::Point> points;
            if (physical) {
                points = physical->sample();
            } else {
                const auto sample = model.sample();
                for (const auto* cells : {&sample.upper, &sample.lower, &sample.falling}) {
                    for (const auto cell : *cells) {
                        points.push_back({cell.x + .415, cell.y + .415});
                    }
                }
            }
            size_t index = 0;
            for (const auto point : points) {
                const auto p = grainPosition(point, g.jar);
                canvas.target->DrawBitmap(grains[(index++ * 7) % grains.size()].Get(),
                                          rectangle({p.x - grainSide / 2, p.y - grainSide / 2, grainSide, grainSide}));
            }
            if (glass) {
                // Keep solid collars, while the glass reflections stay subtle enough
                // to reveal the live grains and the user's real desktop behind them.
                const auto paint = [&](Rect clip, float opacity) {
                    canvas.clipped(CdRect{clip.x, clip.y, clip.width, clip.height}, [&] {
                        canvas.image(*glass, CdRect{g.jar.x, g.jar.y, g.jar.width, g.jar.height},
                                     {.fit = cybercanvas::ImageFit::Stretch, .opacity = opacity, .clip = false});
                    });
                };
                paint({g.jar.x, g.jar.y, g.jar.width, g.jar.height * .12}, 1);
                paint({g.jar.x, g.jar.y + g.jar.height * .12, g.jar.width, g.jar.height * .76}, .34f);
                paint({g.jar.x, g.jar.y + g.jar.height * .88, g.jar.width, g.jar.height * .12}, 1);
            }
        });
        const double radians = model.angle() * std::numbers::pi / 180;
        const double face = std::sin(radians);
        if (model.phase() == Phase::Turning && face > .12) {
            // A tilted vessel still has depth: its end collars become ellipses.
            // At a quarter turn the near circular cap occludes the far chamber;
            // the glass must not collapse into an invisible single-pixel line.
            for (int sign : {1, -1}) {
                const double y = center.y + sign * g.jar.height * .405 * std::cos(radians);
                const double rx = g.jar.width * .405, ry = rx * face;
                const auto ellipse = D2D1::Ellipse({float(center.x), float(y)}, float(rx), float(ry));
                const D2D1_GRADIENT_STOP stops[]{{0, cybercanvas::color(0x777875ff)},
                                                 {.26f, cybercanvas::color(0xc7c8c5ff)},
                                                 {.50f, cybercanvas::color(0x858682ff)},
                                                 {.78f, cybercanvas::color(0xc6c8c4ff)},
                                                 {1, cybercanvas::color(0x61635fff)}};
                const auto material = canvas.linearGradient({center.x - rx, y - ry}, {center.x + rx, y + ry}, stops);
                canvas.target->FillEllipse(ellipse, material.Get());
                canvas.brush->SetColor(cybercanvas::color(0x343632ff));
                canvas.target->DrawEllipse(ellipse, canvas.brush.Get(), float(std::max(.35, g.jar.height * .003)));
                canvas.brush->SetColor(cybercanvas::color(0xe2dbb8aa));
                canvas.target->DrawEllipse(D2D1::Ellipse({float(center.x), float(y)}, float(rx * .95), float(ry * .95)),
                                           canvas.brush.Get(), float(std::max(.2, g.jar.height * .0015)));
            }
        }
        const auto extent = background->GetSize();
        canvas.target->DrawBitmap(background.Get(), D2D1::RectF(0, 0, extent.width, extent.height));
        if (!timer) {
            return;
        }
        const auto text = remainingText(model.remaining());
        for (unsigned row = 0; row < 7; ++row) {
            for (unsigned column = 0; column < kDigitColumns; ++column) {
                if (digitCell(text, column, row)) {
                    canvas.target->DrawBitmap(lamp.Get(),
                                              rectangle({g.display.x + column * g.pitch - halo,
                                                         g.display.y + row * g.pitch - halo, lampSide, lampSide}));
                }
            }
        }
        const auto color = uint32_t(settings.at("sand_color").integer);
        for (unsigned i = 0; i < 16; ++i) {
            if (double(i + 1) / 16 <= model.progress() + .000001) {
                canvas.gradientBox({g.progress.x + i * g.progress.width / 16, g.progress.y, g.progress.width / 16 * .80,
                                    g.progress.height},
                                   g.progress.height * .08, tone(color, 1., 12), tone(color, .84));
            }
        }
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
    return impl_->body && impl_->glass && !impl_->images.pending() && impl_->images.get("body").image == impl_->body &&
           impl_->images.get("glass").image == impl_->glass;
}

bool Renderer::render(bool timer, const Model& model, const PhysicalSand* sand, const cdsdk::Settings& settings,
                      const CdFrame& frame, CdRect safe) {
    if (!impl_->canvas.valid(frame)) {
        return false;
    }
    impl_->canvas.render(frame, safe, [&](auto&, CdRect area) {
        impl_->prepare(timer, area.width, area.height, frame.device_pixel_ratio, settings);
        impl_->draw(timer, model, sand, settings);
    });
    return true;
}

bool Renderer::save(const std::filesystem::path& path, const CdFrame& frame) {
    return impl_->canvas.save(path.wstring(), frame);
}

uint64_t Renderer::cacheBuilds() const {
    return impl_->builds;
}
}  // namespace mechanicalhourglass
