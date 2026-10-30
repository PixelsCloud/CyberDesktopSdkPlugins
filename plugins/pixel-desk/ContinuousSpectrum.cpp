#include "plugins/pixel-desk/ContinuousSpectrum.h"

#include <CyberCanvas/PanelLayout.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <tuple>

#include "plugins/pixel-desk/PixelImages.h"
#include "plugins/pixel-desk/PixelLayout.h"
#include "plugins/pixel-desk/PixelParts.h"
#include "plugins/widget-suite/MechanicalSpectrum.h"

namespace pixeldesk {
using cybercanvas::Canvas;
using Microsoft::WRL::ComPtr;

struct ContinuousSpectrum::Impl {
    using Key =
        std::tuple<double, double, double, double, double, std::array<uint32_t, 4>, const cybercanvas::ImageData*>;
    const MatrixSpec& spec;
    cdsdk::Canvas canvas;
    cdsdk::Images images;
    std::filesystem::path path;
    std::shared_ptr<const cybercanvas::ImageData> body;
    std::optional<Key> key;
    ComPtr<ID2D1RenderTarget> owner;
    cybercanvas::SurfaceImage plate, rod, peak;
    std::array<cybercanvas::SurfaceImage, 3> radialRods;
    cybercanvas::SceneViewport view;
    cybercanvas::Size world;
    uint64_t builds = 0;

    unsigned bands() const {
        return radial() ? 60 : spec.columns == 8 ? 64 : spec.columns == 6 ? 38 : 48;
    }

    double baseline() const {
        return spec.columns == 8 ? 193 : spec.columns == 6 ? 143 : 102;
    }

    double travel() const {
        return spec.columns == 8 ? 170 : spec.columns == 6 ? 123 : 62;
    }

    double left() const {
        return spec.columns == 8 ? 25 : spec.columns == 6 ? 96 : 34;
    }

    double span() const {
        return spec.columns == 8 ? 2016 : spec.columns == 6 ? 823 : 419;
    }

    double barWidth() const {
        return spec.columns == 8 ? 22 : spec.columns == 6 ? 16 : 5;
    }

    cybercanvas::Rect artwork() const {
        if (!radial() && spec.columns == 8) {
            return {1, 193, 2064, 25};
        }
        if (!radial() && spec.columns == 6) {
            return {2, 74, 997, 111};
        }
        return {-2, -2, world.width + 4, world.height + 4};
    }

    Impl(const CdCreateContext& context, const MatrixSpec& value) : spec(value) {
        images.bind(context);
        canvas.bindGpu(context);
        path = std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(context.plugin_directory))) /
               "assets/body.png";
    }

    bool radial() const {
        return spec.form == MatrixForm::Radial;
    }

    void prepare(const cdsdk::Settings& settings, double width, double height, double dpi) {
        world = radial()            ? cybercanvas::Size{353, 355}
                : spec.columns == 8 ? cybercanvas::Size{2066, 219}
                : spec.columns == 6 ? cybercanvas::Size{997, 185}
                                    : cybercanvas::Size{487, 131};
        view = cybercanvas::sceneViewport(fitBody(width, height, world.width, world.height), world);
        const auto edge = uint32_t(std::clamp(
            std::ceil(std::max(world.width + 4, world.height + 4) * view.scale * dpi / 64) * 64, 64., 4096.));
        pixeldesk::requestPixelImage(images, "body", path, 1, edge, dpi);
        images.poll();
        body = images.get("body").image;
        const Key next{width,
                       height,
                       dpi,
                       settings.at("brightness").number,
                       settings.at("glow").number,
                       mechanicalpixels::palette(MatrixKind::MusicSquare, settings),
                       body.get()};
        if (key == next && owner == canvas.target) {
            return;
        }
        cybercanvas::OffscreenSurface surface;
        canvas.renderLayer(surface, {uint32_t(std::ceil(width * dpi)), uint32_t(std::ceil(height * dpi)), dpi},
                           [&](Canvas& layer) {
                               layer.translate(view.origin.x, view.origin.y);
                               layer.scale(view.scale, view.scale);
                               if (body) {
                                   layer.image(*body, artwork(),
                                               {.fit = cybercanvas::ImageFit::Stretch,
                                                .clip = false,
                                                .sampling = cybercanvas::ImageSampling::Nearest});
                               }
                               for (unsigned i = 0; i < bands(); ++i) {
                                   if (radial()) {
                                       layer.saved([&] {
                                           layer.translate(176.5, 179);
                                           layer.rotate(i * 6.);
                                           layer.box(-1.5, -49, 3, 3, 0, 0x466c68ff);
                                       });
                                   } else {
                                       const double x = left() + (i + .5) * span() / bands();
                                       layer.box(x - barWidth() / 2, baseline() - 2, barWidth(), 2, 0, 0x466c68ff);
                                   }
                               }
                           });
        plate = surface.snapshot();
        const auto ink = [&](const char* id) {
            const uint32_t raw = uint32_t(settings.at(id).integer);
            uint32_t color = raw & 255;
            for (unsigned shift : {8u, 16u, 24u}) {
                color |= uint32_t(std::round(((raw >> shift) & 255) * settings.at("brightness").number / 100)) << shift;
            }
            return color;
        };
        const double rodWidth = radial() ? 4 : barWidth(), length = radial() ? 92 : travel() + 4;
        for (unsigned zone = 0; zone < (radial() ? 3u : 1u); ++zone) {
            cybercanvas::OffscreenSurface sprite;
            canvas.renderLayer(sprite,
                               {uint32_t(std::ceil((rodWidth + 4) * view.scale * dpi)),
                                uint32_t(std::ceil((length + 4) * view.scale * dpi)), dpi},
                               [&](Canvas& layer) {
                                   layer.scale(view.scale, view.scale);
                                   const uint32_t raw =
                                       ink(radial() ? std::array{"color", "color_middle", "color_top"}[zone] : "color");
                                   const double glow = settings.at("glow").number / 40;
                                   if (glow > 0) {
                                       layer.box(0, 0, rodWidth + 4, length + 4, 0,
                                                 (raw & 0xffffff00) | uint32_t((raw & 255) * glow * .16));
                                   }
                                   layer.box(2, 2, rodWidth, length, 0, raw);
                                   if (radial()) {
                                       layer.box(2 + rodWidth * .3, 2, rodWidth * .7, length, 0,
                                                 (raw & 0xffffff00) | uint32_t((raw & 255) * .6));
                                   } else {
                                       layer.box(2, 2, rodWidth, length * .55, 0, ink("color_middle"));
                                       layer.box(2, 2, rodWidth, length * .25, 0, ink("color_top"));
                                       layer.box(2, 2, 1, length, 0, 0xf5e7cd35);
                                   }
                               });
            if (radial()) {
                radialRods[zone] = sprite.snapshot();
            } else {
                rod = sprite.snapshot();
            }
        }
        cybercanvas::OffscreenSurface cap;
        canvas.renderLayer(cap,
                           {uint32_t(std::ceil(8 * view.scale * dpi)), uint32_t(std::ceil(8 * view.scale * dpi)), dpi},
                           [&](Canvas& layer) {
                               layer.scale(view.scale, view.scale);
                               pixelDisc(layer, 4, 4, 3.8, kInk);
                               pixelDisc(layer, 4, 4, 3, ink("color_middle"));
                               layer.box(2.5, 2, 1.5, 1.5, 0, kCream);
                           });
        peak = cap.snapshot();
        owner = canvas.target;
        key = next;
        ++builds;
    }

    void draw(Canvas& layer, const cdsdk::Settings& settings, const widgetplugin::Spectrum& spectrum) {
        const auto size = plate->GetSize();
        layer.target->DrawBitmap(plate.Get(), D2D1::RectF(0, 0, size.width, size.height), 1,
                                 D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
        layer.saved([&] {
            layer.translate(view.origin.x, view.origin.y);
            layer.scale(view.scale, view.scale);
            const unsigned count = bands();
            for (unsigned i = 0; i < count; ++i) {
                const double level = widgetplugin::mechanicalBand(spectrum, i, count);
                const double held = widgetplugin::mechanicalBand(spectrum, i, count, true);
                layer.saved([&] {
                    if (radial()) {
                        layer.translate(176.5, 179);
                        layer.rotate(i * 360. / count);
                        layer.clipped({-4, -134, 8, 86}, [&] {
                            if (level > 0) {
                                const float top = float(-48 - 84 * level);
                                const double side = std::sin(i * 2 * std::numbers::pi / count);
                                const unsigned zone = side > .33 ? 2 : side < -.33 ? 0 : 1;
                                layer.target->DrawBitmap(radialRods[zone].Get(), D2D1::RectF(-4, top - 2, 4, top + 94),
                                                         1, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
                            }
                        });
                        if (settings.at("peaks").integer && held > .01) {
                            const float y = float(-48 - 84 * held - 5);
                            layer.target->DrawBitmap(peak.Get(), D2D1::RectF(-4, y - 4, 4, y + 4), 1,
                                                     D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
                        }
                    } else {
                        const float x = float(left() + (i + .5) * span() / count);
                        if (level > 0) {
                            const double top = baseline() - travel() * level;
                            layer.clipped({x - (barWidth() + 4) / 2, top, barWidth() + 4, baseline() - top}, [&] {
                                layer.target->DrawBitmap(
                                    rod.Get(),
                                    D2D1::RectF(float(x - (barWidth() + 4) / 2), float(baseline() - travel() - 4),
                                                float(x + (barWidth() + 4) / 2), float(baseline() + 4)),
                                    1, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
                            });
                        }
                        if (settings.at("peaks").integer && held > .01) {
                            const float y = float(baseline() - travel() * held - (spec.columns >= 6 ? 10 : 6));
                            layer.target->DrawBitmap(
                                peak.Get(),
                                D2D1::RectF(x - (spec.columns >= 6 ? 6.f : 4.f), y - (spec.columns >= 6 ? 6.f : 4.f),
                                            x + (spec.columns >= 6 ? 6.f : 4.f), y + (spec.columns >= 6 ? 6.f : 4.f)),
                                1, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
                        }
                    }
                });
            }
        });
    }
};

ContinuousSpectrum::ContinuousSpectrum(const CdCreateContext& context, const MatrixSpec& spec)
    : impl_(std::make_unique<Impl>(context, spec)) {}

ContinuousSpectrum::~ContinuousSpectrum() = default;

void ContinuousSpectrum::onChange(std::function<void()> callback) {
    impl_->images.onChange(std::move(callback));
}

void ContinuousSpectrum::pause(bool paused) {
    impl_->images.pause(paused);
}

bool ContinuousSpectrum::ready() const {
    return impl_->body && !impl_->images.pending() && impl_->images.get("body").image == impl_->body;
}

uint64_t ContinuousSpectrum::cacheBuilds() const {
    return impl_->builds;
}

bool ContinuousSpectrum::render(const cdsdk::Settings& settings, const widgetplugin::Spectrum& spectrum,
                                const CdFrame& frame, CdRect safe) {
    if (!impl_->canvas.valid(frame)) {
        return false;
    }
    impl_->canvas.render(frame, safe, [&](auto& layer, CdRect area) {
        impl_->prepare(settings, area.width, area.height, frame.device_pixel_ratio);
        impl_->draw(layer, settings, spectrum);
    });
    return true;
}

bool ContinuousSpectrum::save(const std::filesystem::path& path, const CdFrame& frame) {
    return impl_->canvas.save(path.wstring(), frame);
}
}  // namespace pixeldesk
