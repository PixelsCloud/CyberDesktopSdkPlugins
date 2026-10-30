#include <sdk/Icons.h>

#include <optional>

#include "plugins/pixel-desk/PixelDigits.h"
#include "plugins/pixel-desk/PixelImages.h"
#include "plugins/pixel-desk/PixelInput.h"
#include "plugins/pixel-desk/PixelLayout.h"
#include "plugins/pixel-desk/PixelParts.h"

namespace pixeldesk {
using cybercanvas::Canvas;
using Microsoft::WRL::ComPtr;

InputGeometry inputGeometry() {
    InputGeometry geometry;
    constexpr double centerY = 87.5, panelWidth = 360, iconSize = 64;
    constexpr double cardWidth = 48, cardHeight = 72, cardGap = 3;
    constexpr double digitsWidth = 5 * cardWidth + 4 * cardGap;
    const double gutter = (panelWidth - iconSize - digitsWidth) / 3;
    for (unsigned channel = 0; channel < 2; ++channel) {
        const double left = 40 + channel * panelWidth;
        geometry.channels[channel] = {left, 35, panelWidth, 105};
        geometry.icons[channel] = {left + gutter, centerY - iconSize / 2, iconSize, iconSize};
        for (unsigned digit = 0; digit < 5; ++digit) {
            const unsigned index = channel * 5 + digit;
            const double x = left + gutter * 2 + iconSize + digit * (cardWidth + cardGap);
            geometry.cards[index] = {x, centerY - cardHeight / 2, cardWidth, cardHeight};
            geometry.faces[index] = {x + 4, centerY - (cardHeight - 10) / 2, cardWidth - 8, cardHeight - 10};
            geometry.glyphs[index] = {x + (cardWidth - 24) / 2, centerY - 33.6 / 2, 24, 33.6};
        }
    }
    return geometry;
}

struct InputRenderer::Impl {
    struct Key {
        double width, height, dpi;
        const cybercanvas::ImageData* body;
        bool operator==(const Key&) const = default;
    };

    cdsdk::Canvas canvas;
    cdsdk::Images images;
    cdsdk::Icons icons;
    std::filesystem::path path;
    std::shared_ptr<const cybercanvas::ImageData> body;
    std::optional<Key> key;
    ComPtr<ID2D1RenderTarget> owner;
    cybercanvas::SurfaceImage plate;
    double scale = 1, x = 0, y = 0;
    uint64_t builds = 0;

    explicit Impl(const CdCreateContext& context) {
        images.bind(context);
        canvas.bindGpu(context);
        icons.bind(context);
        path = std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(context.plugin_directory))) /
               "assets/body.png";
    }

    void prepare(double width, double height, double dpi) {
        const auto bounds = fitBody(width, height, 800, 800. * 116 / 531);
        scale = bounds.width / 800;
        x = bounds.x;
        y = bounds.y;
        const auto edge = uint32_t(std::clamp(std::ceil(806 * scale * dpi / 64) * 64, 64., 4096.));
        pixeldesk::requestPixelImage(images, "body", path, 1, edge, dpi);
        images.poll();
        if (const auto next = images.get("body").image) {
            body = next;
        }
        const Key next{width, height, dpi, body.get()};
        if (key == next && owner == canvas.target) {
            return;
        }
        key = next;
        cybercanvas::OffscreenSurface surface;
        canvas.renderLayer(
            surface, {uint32_t(std::ceil(width * dpi)), uint32_t(std::ceil(height * dpi)), dpi}, [&](Canvas& layer) {
                layer.translate(x, y);
                layer.scale(scale, scale);
                if (body) {
                    constexpr double assetScale = 800. / 531;
                    layer.image(*body, {-2 * assetScale, -2 * assetScale, 535 * assetScale, 120 * assetScale},
                                {.fit = cybercanvas::ImageFit::Stretch,
                                 .clip = false,
                                 .sampling = cybercanvas::ImageSampling::Nearest});
                }
                // Retain equal five-digit panels even if the illustrative
                // source has unequal numbers of blank windows.
                stepped(layer, {40, 35, 720, 105}, 8, 0x102735ff);
                layer.box(398, 37, 4, 101, 0, 0x071723ff);
                layer.box(400, 39, 1, 97, 0, 0x9bcdb440);
                const auto geometry = inputGeometry();
                for (unsigned i = 0; i < geometry.cards.size(); ++i) {
                    const auto& card = geometry.cards[i];
                    stepped(layer, card, 3, 0xb9b09fff);
                    stepped(layer, geometry.faces[i], 2, 0x1d242aff);
                    layer.line(card.x + 7, card.y + 8, card.x + card.width - 7, card.y + 8, 0xf5e7cd50, 1.5);
                }
            });
        plate = surface.snapshot();
        owner = canvas.target;
        ++builds;
    }
};

InputRenderer::InputRenderer(const CdCreateContext& context) : impl_(std::make_unique<Impl>(context)) {}

InputRenderer::~InputRenderer() = default;

void InputRenderer::onChange(std::function<void()> callback) {
    impl_->images.onChange(std::move(callback));
}

void InputRenderer::pause(bool paused) {
    impl_->images.pause(paused);
}

bool InputRenderer::ready() const {
    return impl_->body && !impl_->images.pending() && impl_->images.get("body").image == impl_->body;
}

bool InputRenderer::render(const CdInputStats& stats, const widgetplugin::CounterDrums::Positions& positions,
                           const CdFrame& frame, CdRect safe) {
    if (!impl_->canvas.valid(frame)) {
        return false;
    }
    impl_->canvas.render(frame, safe, [&](Canvas& layer, CdRect area) {
        impl_->prepare(area.width, area.height, frame.device_pixel_ratio);
        layer.target->DrawBitmap(impl_->plate.Get(), D2D1::RectF(0, 0, float(area.width), float(area.height)), 1,
                                 D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
        layer.saved([&] {
            layer.translate(impl_->x, impl_->y);
            layer.scale(impl_->scale, impl_->scale);
            const auto geometry = inputGeometry();
            for (unsigned i = 0; i < 10; ++i) {
                const auto face = geometry.faces[i], glyph = geometry.glyphs[i];
                layer.clipped(face, [&] {
                    if (!(stats.status & CD_INPUT_STATS_ACTIVE)) {
                        pixelDigits(layer, "-", glyph, kCream);
                        return;
                    }
                    const auto digit = uint64_t(std::floor(positions[i]));
                    const double shift = (positions[i] - digit) * face.height;
                    pixelDigits(layer, std::to_string(digit % 10),
                                {glyph.x, glyph.y - shift, glyph.width, glyph.height}, kCream);
                    if (shift > .001) {
                        pixelDigits(layer, std::to_string((digit + 1) % 10),
                                    {glyph.x, glyph.y + face.height - shift, glyph.width, glyph.height}, kCream);
                    }
                });
            }
        });
        for (unsigned channel = 0; channel < 2; ++channel) {
            const auto icon = inputGeometry().icons[channel];
            impl_->icons.paint(layer,
                               {impl_->x + icon.x * impl_->scale, impl_->y + icon.y * impl_->scale,
                                icon.width * impl_->scale, icon.height * impl_->scale},
                               channel ? "mouse" : "keyboard", channel ? kClay : kMint);
        }
    });
    return true;
}

bool InputRenderer::save(const std::filesystem::path& path, const CdFrame& frame) {
    return impl_->canvas.save(path.wstring(), frame);
}

uint64_t InputRenderer::cacheBuilds() const {
    return impl_->builds;
}
}  // namespace pixeldesk
