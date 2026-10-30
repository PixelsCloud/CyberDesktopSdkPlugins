#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <tuple>

#include "plugins/pixel-desk/PixelDigits.h"
#include "plugins/pixel-desk/PixelHourglass.h"
#include "plugins/pixel-desk/PixelImages.h"
#include "plugins/pixel-desk/PixelParts.h"

namespace mechanicalhourglass {
using cybercanvas::Canvas;
using cybercanvas::Rect;
using Microsoft::WRL::ComPtr;
using namespace pixeldesk;

namespace {
cybercanvas::Path vessel(Canvas& canvas, Rect jar) {
    const double cx = jar.x + jar.width / 2, cy = jar.y + jar.height / 2, unit = jar.height / 224 * 2;
    auto path = canvas.path();
    const auto point = [&](double v, double side) {
        const double half = hourglassHalfWidth(v);
        return cybercanvas::Point{std::round((cx + side * half * jar.width) / unit) * unit,
                                  std::round((cy + v * .345 * jar.height) / unit) * unit};
    };
    path.moveTo(point(-1.16, -1));
    for (unsigned i = 1; i <= 58; ++i) {
        path.lineTo(point(-1.16 + i * .04, -1));
    }
    for (int i = 58; i >= 0; --i) {
        path.lineTo(point(-1.16 + i * .04, 1));
    }
    path.close();
    return path;
}
}  // namespace

struct Renderer::Impl {
    using Key =
        std::tuple<bool, double, double, double, uint32_t, uint32_t, int64_t, bool, const cybercanvas::ImageData*>;
    cdsdk::Canvas canvas;
    cdsdk::Images images;
    std::filesystem::path directory;
    std::shared_ptr<const cybercanvas::ImageData> body;
    bool running = true;
    std::function<void()> changed;
    std::optional<Key> key;
    Geometry geometry;
    ComPtr<ID2D1RenderTarget> owner;
    cybercanvas::SurfaceImage plate, glass;
    std::array<cybercanvas::SurfaceImage, 3> grains;
    double side = 0, halo = 0, spriteSize = 0;
    uint64_t builds = 0;

    void prepare(bool timer, double width, double height, double dpi, const cdsdk::Settings& settings) {
        geometry = arrange(timer, width, height);
        const uint32_t sand = uint32_t(settings.at("sand_color").integer),
                       text = timer ? uint32_t(settings.at("text_color").integer) : 0;
        running = settings.at("running").integer != 0;
        const auto edge = uint32_t(
            std::clamp(std::ceil(std::max(geometry.body.width, geometry.body.height) * dpi / 64) * 64, 64., 4096.));
        pixeldesk::requestPixelImage(images, "active", directory / "body.png", 1, edge, dpi);
        pixeldesk::requestPixelImage(images, "paused", directory / "paused.png", 1, edge, dpi);
        images.poll();
        body = images.get(running ? "active" : "paused").image;
        const Key next{timer, width, height, dpi, sand, text, settings.at("glow").integer, running, body.get()};
        if (key == next && owner == canvas.target) {
            return;
        }
        const auto& g = geometry;
        const double scale = g.jar.height / 224;
        cybercanvas::OffscreenSurface background;
        canvas.renderLayer(background, {uint32_t(std::ceil(width * dpi)), uint32_t(std::ceil(height * dpi)), dpi},
                           [&](Canvas& layer) {
                               if (body) {
                                   layer.image(*body, g.body,
                                               {.fit = cybercanvas::ImageFit::Contain,
                                                .clip = false,
                                                .sampling = cybercanvas::ImageSampling::Nearest});
                               }
                           });
        plate = background.snapshot();
        cybercanvas::OffscreenSurface transparent;
        canvas.renderLayer(
            transparent, {uint32_t(std::ceil(width * dpi)), uint32_t(std::ceil(height * dpi)), dpi},
            [&](Canvas& layer) {
                auto shape = vessel(layer, g.jar);
                layer.fill(shape, 0x34567440);
                layer.stroke(shape, 0x091b29e6, {.width = 7.5 * scale});
                layer.stroke(shape, 0xbfd4e3ff, {.width = 5.5 * scale});
                layer.stroke(shape, 0xf0f8ffff, {.width = 2.6 * scale});
                const double cx = g.jar.x + g.jar.width / 2, cy = g.jar.y + g.jar.height / 2;
                for (double sign : {-1., 1.}) {
                    stepped(layer,
                            {cx - g.jar.width * .275, cy + sign * g.jar.height * .405 - 3 * scale, g.jar.width * .55,
                             6 * scale},
                            2 * scale, kInk);
                    layer.box(cx - g.jar.width * .26, cy + sign * g.jar.height * .405 - 2 * scale, g.jar.width * .52,
                              3 * scale, 0, kCream);
                }
                for (double sign : {-1., 1.}) {
                    auto highlight = layer.path();
                    constexpr std::array<double, 5> positions{.95, .85, .75, .65, .55};
                    const auto point = [&](double value, double inset) {
                        const double unit = scale * 2;
                        return cybercanvas::Point{
                            std::round((cx - hourglassHalfWidth(value) * g.jar.width + inset * scale) / unit) * unit,
                            std::round((cy + sign * value * .345 * g.jar.height) / unit) * unit};
                    };
                    highlight.moveTo(point(positions.front(), 6));
                    for (unsigned i = 1; i < positions.size(); ++i) {
                        highlight.lineTo(point(positions[i], 6));
                    }
                    for (auto i = positions.rbegin(); i != positions.rend(); ++i) {
                        highlight.lineTo(point(*i, 11));
                    }
                    highlight.close();
                    layer.fill(highlight, 0xf5fbffd0);
                }
            });
        glass = transparent.snapshot();
        side = std::max(1. / dpi, g.jar.height * .026);
        halo = g.jar.height * .0025 * settings.at("glow").integer / 40;
        spriteSize = std::ceil((side + halo * 2) * dpi) / dpi;
        for (unsigned i = 0; i < grains.size(); ++i) {
            cybercanvas::OffscreenSurface grain;
            canvas.renderLayer(
                grain, {uint32_t(std::ceil(spriteSize * dpi)), uint32_t(std::ceil(spriteSize * dpi)), dpi},
                [&](Canvas& layer) {
                    uint32_t ink = sand & 255;
                    for (unsigned shift : {8u, 16u, 24u}) {
                        ink |= uint32_t(
                                   std::clamp(((sand >> shift) & 255) * (.95 + i * .08) + (i == 2 ? 22 : 0), 0., 255.))
                               << shift;
                    }
                    if (halo > 0) {
                        layer.box(0, 0, spriteSize, spriteSize, 0, (ink & 0xffffff00) | uint32_t((ink & 255) * .15));
                    }
                    layer.box(halo, halo, side, side, 0, ink);
                    layer.box(halo, halo, side * .4, side * .4, 0, 0xfff4d700 | uint32_t((sand & 255) * .6));
                });
            grains[i] = grain.snapshot();
        }
        owner = canvas.target;
        key = next;
        ++builds;
    }

    void draw(bool timer, const Model& model, const PhysicalSand* physical, const cdsdk::Settings& settings) {
        const auto& g = geometry;
        const auto size = plate->GetSize();
        canvas.target->DrawBitmap(plate.Get(), D2D1::RectF(0, 0, size.width, size.height), 1,
                                  D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
        const cybercanvas::Point center{g.jar.x + g.jar.width / 2, g.jar.y + g.jar.height / 2};
        const double radians = model.angle() * std::numbers::pi / 180, projection = std::cos(radians);
        canvas.saved([&] {
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
                canvas.target->DrawBitmap(
                    grains[(index++ * 7) % grains.size()].Get(),
                    D2D1::RectF(float(p.x - side / 2 - halo), float(p.y - side / 2 - halo),
                                float(p.x - side / 2 - halo + spriteSize), float(p.y - side / 2 - halo + spriteSize)),
                    1, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
            }
            canvas.target->DrawBitmap(glass.Get(), D2D1::RectF(0, 0, size.width, size.height), 1,
                                      D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
        });
        const double face = std::abs(std::sin(radians));
        if (model.phase() == Phase::Turning && face > .12) {
            for (double sign : {1., -1.}) {
                const double y = center.y + sign * g.jar.height * .405 * projection, radius = g.jar.width * .3;
                canvas.saved([&] {
                    canvas.translate(center.x, y);
                    canvas.scale(1, std::max(.05, face * .32));
                    pixelDisc(canvas, 0, 0, radius, kInk);
                    pixelDisc(canvas, 0, 0, radius * .94, kCream);
                    pixelDisc(canvas, 0, 0, radius * .65, running ? kMint : 0x6f8ea5ff);
                });
            }
        }
        if (timer) {
            const auto selected = uint32_t(settings.at("text_color").integer);
            const auto text = !running && selected == kMint ? 0x6f8ea5ff : selected;
            pixelDigits(canvas, remainingText(model.remaining()), g.display, text, true);
            // These are the measured native-pixel slots of the approved case.
            // Their slight width changes are part of the illustration, not a uniform grid.
            constexpr std::array<std::pair<double, double>, 16> slots{{
                {299, 316},
                {320, 338},
                {342, 360},
                {364, 382},
                {386, 404},
                {408, 427},
                {431, 449},
                {453, 472},
                {477, 496},
                {500, 520},
                {525, 544},
                {548, 568},
                {572, 592},
                {596, 617},
                {621, 641},
                {646, 667},
            }};
            const double unit = g.body.width / 739;
            const double offset = g.body.y + (g.body.height - 330 * unit) / 2;
            for (unsigned i = 0; i < slots.size(); ++i) {
                if (running && double(i + 1) / slots.size() <= model.progress() + .000001) {
                    const auto [left, right] = slots[i];
                    canvas.box(g.body.x + left * unit, offset + 220 * unit, (right - left) * unit, 34 * unit, 0, text);
                    canvas.line(g.body.x + (left + 1) * unit, offset + 221 * unit, g.body.x + (right - 1) * unit,
                                offset + 221 * unit, 0xf5e7cd66, unit);
                }
            }
        }
    }
};

Renderer::Renderer(const CdCreateContext& context) : impl_(std::make_unique<Impl>()) {
    impl_->directory =
        std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(context.plugin_directory))) /
        "assets";
    impl_->images.bind(context);
    impl_->canvas.bindGpu(context);
    impl_->images.onChange([this] {
        if (impl_->changed) {
            impl_->changed();
        }
    });
}

Renderer::~Renderer() = default;

void Renderer::onChange(std::function<void()> callback) {
    impl_->changed = std::move(callback);
}

void Renderer::pause(bool paused) {
    impl_->images.pause(paused);
    if (paused) {
        impl_->grains = {};
        impl_->glass.Reset();
        impl_->plate.Reset();
        impl_->owner.Reset();
        impl_->key.reset();
    }
}

bool Renderer::ready() const {
    return impl_->body && !impl_->images.pending() && impl_->images.get("active").image &&
           impl_->images.get("paused").image &&
           impl_->images.get(impl_->running ? "active" : "paused").image == impl_->body;
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
