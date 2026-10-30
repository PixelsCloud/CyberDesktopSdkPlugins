#include <CyberCanvas/Sprites.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <tuple>

#include "plugins/pixel-desk/ContinuousSpectrum.h"
#include "plugins/pixel-desk/PixelImages.h"
#include "plugins/pixel-desk/PixelLayout.h"
#include "plugins/pixel-desk/PixelMatrix.h"
#include "plugins/pixel-desk/PixelPanels.h"
#include "plugins/pixel-desk/PixelParts.h"

namespace pixeldesk {
using cybercanvas::Canvas;
using Microsoft::WRL::ComPtr;

const MatrixSpec& matrixSpec(std::string_view id) {
    for (const auto& spec : kMatrixCatalog) {
        if (id == spec.id) {
            return spec;
        }
    }
    throw std::invalid_argument("Unknown pixel matrix");
}

cdsdk::Settings matrixDefaults(const MatrixSpec& spec) {
    auto settings = mechanicalpixels::defaults(spec.round ? MatrixKind::MusicRound : MatrixKind::MusicSquare);
    const CdSettingValue colors[]{
        {sizeof(CdSettingValue), CD_SETTING_COLOR, "color", 0x82e9adff},
        {sizeof(CdSettingValue), CD_SETTING_COLOR, spec.round ? "peak_color" : "color_middle", 0xffcd68ff},
        {sizeof(CdSettingValue), CD_SETTING_COLOR, "color_top", 0xec794cff},
        {sizeof(CdSettingValue), CD_SETTING_NUMBER, "glow", 0, 0},
    };
    if (settings.apply(colors, uint32_t(std::size(colors))) != CD_OK) {
        throw std::logic_error("Invalid pixel spectrum defaults");
    }
    return settings;
}

namespace {
struct MatrixArtwork {
    cybercanvas::Size source;
    cybercanvas::Rect visible, aperture;
};

MatrixArtwork artwork(const MatrixSpec& spec) {
    if (spec.columns == 8 && spec.round) {
        return {{1988, 254}, {2, 2, 1984, 250}, {78, 55, 1828, 144}};
    }
    if (spec.columns == 8) {
        return {{976, 139}, {2, 2, 972, 135}, {46, 33, 880, 72}};
    }
    if (spec.columns == 6) {
        return {{802, 139}, {2, 2, 798, 135}, {44, 33, 710, 72}};
    }
    return {{527, 140}, {2, 2, 523, 136}, {46, 34, 431, 72}};
}
}  // namespace

MatrixGeometry matrixGeometry(const MatrixSpec& spec, double width, double height) {
    const auto art = artwork(spec);
    const double worldWidth = spec.columns == 8
                                  ? std::max(art.visible.width, (width - 14) / (height - 14) * art.visible.height)
                                  : art.visible.width;
    const auto bounds = fitBody(width, height, worldWidth, art.visible.height);
    const double scale = bounds.height / art.visible.height;
    MatrixGeometry g;
    g.artwork = bounds;
    g.aperture = {bounds.x + art.aperture.x * scale, bounds.y + art.aperture.y * scale,
                  (art.aperture.width + worldWidth - art.visible.width) * scale, art.aperture.height * scale};
    g.rows = spec.round ? 5 : 4;
    g.columns = spec.columns == 4 ? 20 : spec.columns == 6 ? 33 : spec.round ? 51 : 43;
    g.pitch = {g.aperture.width / g.columns, g.aperture.height / g.rows};
    g.diameter = std::min(g.pitch.x, g.pitch.y) * .8;
    g.grid = {g.aperture.x, g.aperture.y};
    return g;
}

namespace {
void matrixDot(Canvas& layer, double x, double y, double size, uint32_t rgba, bool round) {
    if (round) {
        pixelDisc(layer, x + size / 2, y + size / 2, size / 2, rgba);
    } else {
        layer.box(x, y, size, size, 0, rgba);
    }
}
}  // namespace

struct MatrixRenderer::Impl {
    using Key =
        std::tuple<double, double, double, double, double, std::array<uint32_t, 4>, const cybercanvas::ImageData*>;
    const MatrixSpec& spec;
    std::unique_ptr<ContinuousSpectrum> continuous;
    cdsdk::Canvas canvas;
    cdsdk::Images images;
    std::filesystem::path path;
    std::shared_ptr<const cybercanvas::ImageData> body;
    std::optional<Key> key;
    ComPtr<ID2D1RenderTarget> owner;
    cybercanvas::SurfaceImage background;
    cybercanvas::SpritePalette palette;
    MatrixGeometry geometry;
    double halo = 0, spriteSize = 0;
    uint64_t builds = 0;

    Impl(const CdCreateContext& context, const MatrixSpec& value) : spec(value) {
        if (spec.form != MatrixForm::Dots) {
            continuous = std::make_unique<ContinuousSpectrum>(context, spec);
            return;
        }
        images.bind(context);
        canvas.bindGpu(context);
        path = std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(context.plugin_directory))) /
               "assets/body.png";
    }

    void prepare(const cdsdk::Settings& settings, double width, double height, double dpi) {
        const auto g = matrixGeometry(spec, width, height);
        const auto art = artwork(spec);
        const double scale = g.artwork.height / art.visible.height;
        const double extension = g.artwork.width / scale - art.visible.width;
        const auto edge = uint32_t(
            std::clamp(std::ceil(std::max(art.source.width, art.source.height) * scale * dpi / 64) * 64, 64., 4096.));
        pixeldesk::requestPixelImage(images, "body", path, 1, edge, dpi);
        images.poll();
        body = images.get("body").image;
        const auto kind = spec.round ? MatrixKind::MusicRound : MatrixKind::MusicSquare;
        const Key next{width,
                       height,
                       dpi,
                       settings.at("brightness").number,
                       settings.at("glow").number,
                       mechanicalpixels::palette(kind, settings),
                       body.get()};
        if (key == next && owner == canvas.target) {
            return;
        }
        geometry = g;
        cybercanvas::OffscreenSurface surface;
        canvas.renderLayer(
            surface, {uint32_t(std::ceil(width * dpi)), uint32_t(std::ceil(height * dpi)), dpi}, [&](Canvas& layer) {
                layer.saved([&] {
                    layer.translate(g.artwork.x, g.artwork.y);
                    layer.scale(scale, scale);
                    if (body) {
                        if (extension > 0) {
                            panelStrip(layer, *body,
                                       {-art.visible.x, -art.visible.y, art.source.width, art.source.height},
                                       art.source.width + extension, art.source.width / 2, art.source.width / 2);
                        } else {
                            layer.image(*body, {-art.visible.x, -art.visible.y, art.source.width, art.source.height},
                                        {.fit = cybercanvas::ImageFit::Stretch,
                                         .clip = false,
                                         .sampling = cybercanvas::ImageSampling::Nearest});
                        }
                    }
                });
                const cybercanvas::Point inset{(g.pitch.x - g.diameter) / 2, (g.pitch.y - g.diameter) / 2};
                for (unsigned column = 0; column < g.columns; ++column) {
                    for (unsigned row = 0; row < g.rows; ++row) {
                        matrixDot(layer, g.grid.x + column * g.pitch.x + inset.x, g.grid.y + row * g.pitch.y + inset.y,
                                  g.diameter, 0x173343ff, spec.round);
                    }
                }
            });
        background = surface.snapshot();
        const double glow = settings.at("glow").number / 40, brightness = settings.at("brightness").number / 100;
        halo = std::max(1. / dpi, g.diameter * .15 * glow);
        spriteSize = std::ceil((g.diameter + 2 * halo) * dpi) / dpi;
        std::vector<cybercanvas::SurfaceImage> lamps;
        for (unsigned row = 0; row <= g.rows; ++row) {
            const char* field = spec.round                               ? (row == g.rows       ? "peak_color"
                                                                            : row * 2 >= g.rows ? "color_top"
                                                                                                : "color")
                                : row == g.rows || row * 3 >= g.rows * 2 ? "color_top"
                                : row * 3 >= g.rows                      ? "color_middle"
                                                                         : "color";
            const auto raw = uint32_t(settings.at(field).integer);
            uint32_t ink = raw & 255;
            for (unsigned shift : {8u, 16u, 24u}) {
                ink |= uint32_t(std::round(((raw >> shift) & 255) * brightness)) << shift;
            }
            cybercanvas::OffscreenSurface sprite;
            canvas.renderLayer(sprite,
                               {uint32_t(std::ceil(spriteSize * dpi)), uint32_t(std::ceil(spriteSize * dpi)), dpi},
                               [&](Canvas& layer) {
                                   if (glow > 0) {
                                       matrixDot(layer, 0, 0, g.diameter + 2 * halo, kInk, spec.round);
                                   }
                                   matrixDot(layer, halo, halo, g.diameter, ink, spec.round);
                               });
            lamps.push_back(sprite.snapshot());
        }
        palette.build(canvas, lamps, cybercanvas::ImageSampling::Nearest);
        owner = canvas.target;
        key = next;
        ++builds;
    }

    void draw(const cdsdk::Settings& settings, const widgetplugin::Spectrum& spectrum) {
        palette.begin(canvas);
        const auto size = background->GetSize();
        canvas.target->DrawBitmap(background.Get(), D2D1::RectF(0, 0, size.width, size.height), 1,
                                  D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
        const auto& g = geometry;
        const cybercanvas::Point inset{(g.pitch.x - g.diameter) / 2, (g.pitch.y - g.diameter) / 2};
        const auto light = [&](unsigned column, unsigned row, bool peak, float opacity) {
            const double x = g.grid.x + column * g.pitch.x + inset.x - halo,
                         y = g.grid.y + row * g.pitch.y + inset.y - halo;
            const unsigned color = peak ? g.rows : g.rows - 1 - row;
            palette.add(color, {x, y, spriteSize, spriteSize}, opacity);
        };
        for (unsigned column = 0; column < g.columns; ++column) {
            const double position = double(column) * (widgetplugin::Spectrum::count - 1) / (g.columns - 1);
            const float level = std::clamp(spectrum.sample(position), 0.f, 1.f);
            for (unsigned row = 0; row < g.rows; ++row) {
                const float opacity = level * g.rows > g.rows - 1 - row ? 1.f : 0.f;
                if (opacity > 0) {
                    light(column, row, false, opacity);
                }
            }
            const float peak = std::clamp(spectrum.sample(position, true), 0.f, 1.f);
            if (settings.at("peaks").integer && peak > level + .5f / g.rows) {
                light(column, g.rows - std::clamp(unsigned(std::ceil(peak * g.rows)), 1u, g.rows), true, 1);
            }
        }
        palette.flush(canvas);
    }
};

MatrixRenderer::MatrixRenderer(const CdCreateContext& context, const MatrixSpec& spec)
    : impl_(std::make_unique<Impl>(context, spec)) {}

MatrixRenderer::~MatrixRenderer() = default;

void MatrixRenderer::onChange(std::function<void()> callback) {
    if (impl_->continuous) {
        impl_->continuous->onChange(std::move(callback));
        return;
    }
    impl_->images.onChange(std::move(callback));
}

void MatrixRenderer::pause(bool paused) {
    if (impl_->continuous) {
        impl_->continuous->pause(paused);
        return;
    }
    impl_->images.pause(paused);
}

bool MatrixRenderer::ready() const {
    if (impl_->continuous) {
        return impl_->continuous->ready();
    }
    return impl_->body && !impl_->images.pending() && impl_->images.get("body").image == impl_->body;
}

uint64_t MatrixRenderer::cacheBuilds() const {
    if (impl_->continuous) {
        return impl_->continuous->cacheBuilds();
    }
    return impl_->builds;
}

bool MatrixRenderer::render(const cdsdk::Settings& settings, const widgetplugin::Spectrum& spectrum,
                            const CdFrame& frame, CdRect safe) {
    if (impl_->continuous) {
        return impl_->continuous->render(settings, spectrum, frame, safe);
    }
    if (!impl_->canvas.valid(frame)) {
        return false;
    }
    impl_->canvas.render(frame, safe, [&](auto&, CdRect area) {
        impl_->prepare(settings, area.width, area.height, frame.device_pixel_ratio);
        impl_->draw(settings, spectrum);
    });
    return true;
}

bool MatrixRenderer::save(const std::filesystem::path& path, const CdFrame& frame) {
    if (impl_->continuous) {
        return impl_->continuous->save(path, frame);
    }
    return impl_->canvas.save(path.wstring(), frame);
}
}  // namespace pixeldesk
