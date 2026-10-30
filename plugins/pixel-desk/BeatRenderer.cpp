#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>

#include "plugins/pixel-desk/BeatArtwork.h"
#include "plugins/pixel-desk/PixelBeat.h"
#include "plugins/pixel-desk/PixelImages.h"
#include "plugins/pixel-desk/PixelParts.h"

namespace beatmachine {
using cybercanvas::Canvas;
using cybercanvas::Point;
using namespace pixeldesk;

namespace {
uint64_t imageRevision(const cdsdk::Images& images) {
    const auto body = images.get("body").image, parts = images.get("parts").image;
    return uint64_t(reinterpret_cast<uintptr_t>(body.get())) ^
           std::rotl(uint64_t(reinterpret_cast<uintptr_t>(parts.get())), 1);
}

void spring(Canvas& layer, Point top, Point bottom, double half) {
    const double dx = bottom.x - top.x, dy = bottom.y - top.y, length = std::hypot(dx, dy);
    if (length < 2) {
        return;
    }
    auto path = layer.path();
    path.moveTo(top);
    for (unsigned i = 1; i < 14; ++i) {
        const double t = i / 14., side = i % 2 ? half : -half;
        path.lineTo({top.x + dx * t + dy / length * side, top.y + dy * t - dx / length * side});
    }
    path.lineTo(bottom);
    layer.stroke(path, kInk, {.width = 4});
    layer.stroke(path, kCream, {.width = 2});
}

void rail(Canvas& layer, const Segment& segment) {
    layer.line(segment.first.x, segment.first.y, segment.second.x, segment.second.y, 0x071623ff,
               segment.radius * 2 + 3);
    layer.line(segment.first.x, segment.first.y, segment.second.x, segment.second.y, kCream, segment.radius * 2);
    layer.line(segment.first.x, segment.first.y - 1.5, segment.second.x, segment.second.y - 1.5, kMint, 2);
}

void marble(Canvas& layer, Point p, double radius) {
    pixelDisc(layer, p.x + .8, p.y + 1.2, radius + .6, 0x071623e6);
    pixelDisc(layer, p.x, p.y, radius, 0x627581ff);
    pixelDisc(layer, p.x - .35, p.y - .5, radius - 1.3, 0xe7edf1ff);
    layer.box(p.x - radius * .42, p.y - radius * .57, radius * .6, radius * .48, 0, 0xfff4d9ff);
}
}  // namespace

void Renderer::bind(const CdCreateContext& context) {
    images_.bind(context);
}

Renderer::Renderer(const std::filesystem::path& assets, bool) : assetRoot_(assets) {
    if (!assets.empty()) {
        pixeldesk::requestPixelImage(images_, "body", assets / "body.png", 1, 512, 1);
        pixeldesk::requestPixelImage(images_, "parts", assets / "parts.png", 1, 512, 1);
    }
}

bool Renderer::pollAssets() {
    return images_.poll();
}

bool Renderer::pendingAssets() const {
    // A worker may finish just after this frame sampled an empty slot. Keep
    // scheduling until a frame actually paints those newly delivered images.
    return images_.pending() || imageRevision(images_) != assetRevision_;
}

cdsdk::ImageSlotStats Renderer::assetStats() const {
    return images_.stats();
}

void Renderer::pauseAssets(bool paused) {
    images_.pause(paused);
}

void Renderer::stationary(Canvas& layer, const Model& model, Appearance appearance) {
    const double width = model.width();
    const auto parts = images_.get("parts").image;
    const auto bounds = beatCase(model.compact());
    const auto& spec = kBeatCatalog[model.compact() ? 0 : 1];
    if (appearance.background) {
        if (const auto image = images_.get("body").image) {
            const double unit = bounds.width / spec.visible.width;
            layer.image(*image,
                        {bounds.x - spec.visible.x * unit, bounds.y - spec.visible.y * unit, spec.source.width * unit,
                         spec.source.height * unit},
                        {.fit = cybercanvas::ImageFit::Stretch,
                         .clip = false,
                         .sampling = cybercanvas::ImageSampling::Nearest});
        } else {
            stepped(layer, bounds, 16, kMint);
            stepped(layer, {30, 65, width - 60, 275}, 12, kInk);
        }
        if (appearance.light) {
            stepped(layer, {30, 65, width - 60, 275}, 12, 0xf1e6d0ff);
        }
    }
    for (unsigned i = 0; i < 4; ++i) {
        const double x = model.pistons()[i].rest.x, half = model.compact() ? 27 : 30;
        if (parts) {
            beatPart(layer, *parts, 2, {x - half, 326, half * 2, 29});
        }
    }
    const auto loader = model.pistons()[4].rest;
    stepped(layer, {loader.x - 3, 341, 40, 8}, 3, kInk);
    stepped(layer, {loader.x, 342, 34, 5}, 2, kAmber);
    // The case artwork supplies the enclosure. Only the three working deck
    // segments need a second visible rail inside it.
    for (unsigned i = 3; i <= 5; ++i) {
        rail(layer, model.rails()[i]);
    }
    for (unsigned i = 0; i < model.bumpers().size(); ++i) {
        const auto center = model.bumpers()[i];
        const double radius = (i == 2 ? 18 : model.compact() ? 28 : 30) + 5;
        if (parts) {
            beatPart(layer, *parts, i + 3, {center.x - radius, center.y - radius, radius * 2, radius * 2});
        }
    }
}

void Renderer::draw(Canvas& layer, const Model& model, const Signal& signal, Appearance appearance, double scale) {
    const double dpi = layer.devicePixelRatio();
    const auto bounds = beatCase(model.compact());
    const auto& spec = kBeatCatalog[model.compact() ? 0 : 1];
    const auto edge = uint32_t(std::clamp(std::ceil(spec.source.width * scale * dpi / 64) * 64, 64., 4096.));
    if (!assetRoot_.empty()) {
        pixeldesk::requestPixelImage(images_, "body", assetRoot_ / "body.png", 1, edge, dpi);
        pixeldesk::requestPixelImage(
            images_, "parts", assetRoot_ / "parts.png", 1,
            uint32_t(std::clamp(std::ceil(kBeatPartsSize.width * .25 * scale * dpi / 64) * 64, 64., 4096.)), dpi);
    }
    pollAssets();
    const auto parts = images_.get("parts").image;
    const auto paintedBody = images_.get("body").image;
    assetRevision_ = uint64_t(reinterpret_cast<uintptr_t>(paintedBody.get())) ^
                     std::rotl(uint64_t(reinterpret_cast<uintptr_t>(parts.get())), 1);
    const auto width = uint32_t(std::max(1., std::ceil(bounds.width * scale * dpi))),
               height = uint32_t(std::max(1., std::ceil(bounds.height * scale * dpi)));
    if (!backdrop_.bitmap() || owner_.Get() != layer.target.Get() || width_ != width || height_ != height ||
        dpi_ != dpi || scale_ != scale || cachedAssetRevision_ != assetRevision_ ||
        appearance_.background != appearance.background || appearance_.light != appearance.light) {
        layer.renderLayer(backdrop_, {width, height, dpi}, [&](Canvas& cache) {
            cache.scale(scale, scale);
            cache.translate(-bounds.x, -bounds.y);
            stationary(cache, model, appearance);
        });
        owner_ = layer.target.Get();
        width_ = width;
        height_ = height;
        dpi_ = dpi;
        scale_ = scale;
        cachedAssetRevision_ = assetRevision_;
        appearance_ = appearance;
    }
    layer.target->DrawBitmap(
        backdrop_.bitmap(),
        D2D1::RectF(float(bounds.x), float(bounds.y), float(bounds.x + bounds.width), float(bounds.y + bounds.height)),
        1, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    const std::array<double, 3> energies{signal.bass, signal.middle, signal.high};
    for (unsigned i = 0; i < energies.size(); ++i) {
        const double x = model.width() - 142 + i * 30.;
        pixelDisc(layer, x, 86, 5, kInk);
        pixelDisc(layer, x, 86, 3,
                  (i == 2 ? kMint : kAmber) & 0xffffff00 | uint32_t(35 + std::clamp(energies[i], 0., 1.) * 220));
    }
    for (unsigned i = 0; i < model.pistons().size(); ++i) {
        const auto& piston = model.pistons()[i];
        const auto state = model.state(piston.body);
        const Point p{state.position.x * kUnits, state.position.y * kUnits};
        if (i < 4) {
            spring(layer, {p.x, p.y + 12}, {piston.rest.x, 330}, model.compact() ? 9 : 13);
            layer.box(p.x - 3, p.y + 9, 6, std::max(1., 327 - p.y), 0, 0x5c726eff);
        } else {
            layer.clipped({model.width() - 125, 65, 95, 276}, [&] {
                spring(layer, {p.x + 3.6, p.y + 6}, {piston.rest.x + 10, 337}, 4);
            });
        }
        layer.saved([&] {
            layer.translate(p.x, p.y);
            if (i == 4) {
                layer.rotate(-.55 * 180 / std::numbers::pi);
            }
            const double half = i == 4 ? 25 : model.compact() ? 27 : 30;
            if (parts) {
                beatPart(layer, *parts, 1, {-half, -12, half * 2, 27});
            }
        });
    }
    for (const auto& paddle : model.paddles()) {
        const auto state = model.state(paddle.body);
        layer.saved([&] {
            layer.translate(state.position.x * kUnits, state.position.y * kUnits);
            layer.rotate(state.angle * 180 / std::numbers::pi);
            if (&paddle == &model.paddles()[1]) {
                layer.scale(-1, 1);
            }
            const double length = model.compact() ? 70 : 105;
            if (parts) {
                const double unit = length / 422;
                beatPart(layer, *parts, 0, {-76 * unit, -71.5 * unit, 556 * unit, 143 * unit});
            }
        });
    }
    for (unsigned i = 0; i < model.bumpers().size(); ++i) {
        const auto p = model.bumpers()[i];
        const double radius = i == 2 ? 18 : model.compact() ? 28 : 30;
        if (model.flashes()[i] > 0) {
            pixelDisc(layer, p.x, p.y, radius * .30, 0xffedb100 | uint32_t(model.flashes()[i] * 140));
        }
    }
    for (const auto& ball : model.balls()) {
        const auto p = model.state(ball.body).position;
        marble(layer, {p.x * kUnits, p.y * kUnits}, kRadius);
    }
    if (appearance.status != CD_DATA_READY) {
        stepped(layer, {50, 75, 250, 28}, 3, 0x14242ee6);
        cybercanvas::TextStyle text;
        text.size = 20;
        text.bold = true;
        layer.text(appearance.english ? L"Waiting for audio" : L"等待音频", text, {58, 77, 234, 24}, kCream);
    }
}
}  // namespace beatmachine
