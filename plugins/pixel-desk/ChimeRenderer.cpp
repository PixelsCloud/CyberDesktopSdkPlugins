#include <sdk/Images.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <numbers>
#include <optional>
#include <tuple>

#include "plugins/pixel-desk/PixelChime.h"
#include "plugins/pixel-desk/PixelImages.h"
#include "plugins/pixel-desk/PixelParts.h"

namespace toys {
using cybercanvas::Canvas;
using Microsoft::WRL::ComPtr;
using namespace pixeldesk;

namespace {
constexpr std::array<const char*, 7> kImageNames{"body", "moon", "mint-left", "bell", "clay", "star", "mint-right"};
using Images = std::array<std::shared_ptr<const cybercanvas::ImageData>, 7>;
using Pointers = std::array<const cybercanvas::ImageData*, 7>;

Images currentImages(const cdsdk::Images& images) {
    Images result;
    for (unsigned i = 0; i < result.size(); ++i) {
        result[i] = images.get(kImageNames[i]).image;
    }
    return result;
}

Pointers pointers(const Images& images) {
    Pointers result{};
    for (unsigned i = 0; i < result.size(); ++i) {
        result[i] = images[i].get();
    }
    return result;
}
}  // namespace

struct MechanicalRenderer::Impl {
    using Key = std::tuple<double, double, bool, std::string, Pointers>;
    cdsdk::Images images;
    std::filesystem::path directory;
    std::optional<Key> key;
    Pointers painted{};
    ComPtr<ID2D1RenderTarget> owner;
    cybercanvas::OffscreenSurface plate;
    std::array<cybercanvas::SurfaceImage, 6> sprites;

    void prepare(Canvas& canvas, const Model& model, const Appearance& appearance, double scale) {
        const double density = scale * canvas.devicePixelRatio(), worldHeight = model.size().height;
        for (unsigned i = 0; i < kImageNames.size(); ++i) {
            const auto edge = uint32_t(std::clamp(std::ceil((i == 0 ? 312 : 140) * density / 32) * 32, 64., 4096.));
            pixeldesk::requestPixelImage(images, kImageNames[i], directory / (std::string(kImageNames[i]) + ".png"), 1,
                                         edge, canvas.devicePixelRatio());
        }
        images.poll();
        const auto artwork = currentImages(images);
        painted = pointers(artwork);
        const Key next{density, worldHeight, appearance.dark, appearance.palette, painted};
        if (key == next && owner == canvas.target) {
            return;
        }
        canvas.renderLayer(plate,
                           {uint32_t(std::ceil(410 * density)), uint32_t(std::ceil((worldHeight + 70) * density)), 1},
                           [&](Canvas& layer) {
                               layer.scale(density, density);
                               layer.translate(46, 32);
                               if (artwork[0]) {
                                   layer.image(*artwork[0],
                                               worldHeight > 500 ? cybercanvas::Rect{0, 0, 313, 144}
                                                                 : cybercanvas::Rect{-26, 11, 312, 90},
                                               {.fit = cybercanvas::ImageFit::Contain,
                                                .clip = false,
                                                .sampling = cybercanvas::ImageSampling::Nearest});
                               }
                               if (worldHeight > 500) {
                                   // This clean bird rail has two baked eyelets; register the left
                                   // suspension to its lower edge before drawing its physical cord.
                                   layer.line(63, 111, 63, 134, kInk, 6);
                                   layer.line(63, 111, 63, 134, kAmber, 3.5);
                                   pixelDisc(layer, 63, 134, 5, kInk);
                                   pixelDisc(layer, 63, 134, 3.5, kAmber);
                               }
                               // Register each rope to its real distance-joint anchor.
                               for (unsigned i = 0; i < 3; ++i) {
                                   const auto anchor = model.anchor(model.pieces()[i * 2]);
                                   pixelDisc(layer, anchor.x * 100, anchor.y * 100, 3.5, kInk);
                                   pixelDisc(layer, anchor.x * 100, anchor.y * 100, 2, kAmber);
                               }
                           });
        for (unsigned i = 0; i < sprites.size(); ++i) {
            const auto extent = chimePieceSize(i, worldHeight);
            const double width = extent.width, height = extent.height;
            cybercanvas::OffscreenSurface sprite;
            canvas.renderLayer(
                sprite, {uint32_t(std::ceil(width * density)), uint32_t(std::ceil(height * density)), 1},
                [&](Canvas& layer) {
                    layer.scale(density, density);
                    if (artwork[i + 1]) {
                        layer.image(*artwork[i + 1], {0, 0, width, height},
                                    {.fit = cybercanvas::ImageFit::Contain,
                                     .clip = false,
                                     .sampling = cybercanvas::ImageSampling::Nearest});
                        if (worldHeight < 500 && i % 2 && appearance.palette == "ocean") {
                            stepped(layer, {width / 2 - 7.2, height / 2 - 17.8, 14.4, 36}, 1.3, 0x6aa6c2ff);
                            layer.box(width / 2 - 6, height / 2 - 16.5, 1.4, 32, 0, 0xb8deecff);
                        }
                    }
                });
            sprites[i] = sprite.snapshot();
        }
        owner = canvas.target;
        key = next;
    }
};

MechanicalRenderer::MechanicalRenderer() : impl_(std::make_unique<Impl>()) {}

MechanicalRenderer::~MechanicalRenderer() = default;

void MechanicalRenderer::bind(const CdCreateContext& context, std::function<void()> changed) {
    impl_->directory =
        std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(context.plugin_directory))) /
        "assets";
    impl_->images.bind(context);
    impl_->images.onChange(std::move(changed));
}

void MechanicalRenderer::pause(bool paused) {
    impl_->images.pause(paused);
    if (paused) {
        impl_->sprites = {};
        impl_->plate.reset();
        impl_->owner.Reset();
        impl_->key.reset();
        impl_->painted = {};
    }
}

bool MechanicalRenderer::pending() const {
    const auto current = currentImages(impl_->images);
    return impl_->images.pending() || pointers(current) != impl_->painted ||
           std::any_of(current.begin(), current.end(), [](const auto& image) {
               return !image;
           });
}

void MechanicalRenderer::draw(Canvas& layer, const Model& model, const Appearance& appearance, double scale) {
    impl_->prepare(layer, model, appearance, scale);
    for (const auto& piece : model.pieces()) {
        const auto a = model.anchor(piece), b = model.attachment(piece);
        const uint32_t chain = appearance.palette == "ocean" ? 0x7bb4c9ff : kAmber;
        layer.line(a.x * 100, a.y * 100, b.x * 100, b.y * 100, kInk, 4.5);
        layer.line(a.x * 100, a.y * 100, b.x * 100, b.y * 100, chain, 2.6);
        const double length = std::hypot(b.x - a.x, b.y - a.y);
        const double dx = length > .001 ? (b.y - a.y) / length * .6 : .6;
        const double dy = length > .001 ? -(b.x - a.x) / length * .6 : 0;
        layer.line(a.x * 100 + dx, a.y * 100 + dy, b.x * 100 + dx, b.y * 100 + dy, kCream, .65);
        if (piece.art >= 3) {
            const double x = std::lerp(a.x, b.x, .55) * 100, y = std::lerp(a.y, b.y, .55) * 100;
            pixelDisc(layer, x, y, 5.5, kInk);
            pixelDisc(layer, x, y, 4.1, appearance.palette == "ocean" ? 0x6aa6c2ff : kMint);
            layer.box(x - 2.2, y - 2.7, 2, 2.2, 0, kCream);
        }
    }
    layer.target->DrawBitmap(impl_->plate.bitmap(), D2D1::RectF(-46, -32, 364, float(model.size().height + 38)), 1,
                             D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    for (size_t i = 0; i < model.pieces().size(); ++i) {
        const auto state = model.state(i);
        const auto extent = chimePieceSize(unsigned(i), model.size().height);
        const float halfWidth = float(extent.width / 2), halfHeight = float(extent.height / 2);
        layer.saved([&] {
            layer.translate(state.position.x * 100, state.position.y * 100);
            layer.rotate(state.angle * 180 / std::numbers::pi);
            layer.target->DrawBitmap(impl_->sprites[i].Get(),
                                     D2D1::RectF(-halfWidth, -halfHeight, halfWidth, halfHeight), 1,
                                     D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
        });
    }
}
}  // namespace toys
