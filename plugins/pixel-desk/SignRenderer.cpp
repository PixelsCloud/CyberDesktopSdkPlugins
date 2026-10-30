#include <algorithm>
#include <cmath>
#include <numbers>

#include "plugins/mechanical-sign/Sign.h"
#include "plugins/pixel-desk/PixelImages.h"
#include "plugins/pixel-desk/PixelParts.h"
#include "plugins/pixel-desk/SignGlyphs.h"

namespace mechanicalsign {
using cybercanvas::Canvas;
using cybercanvas::Rect;
using namespace pixeldesk;

void Renderer::bind(const CdCreateContext& context) {
    images_.bind(context);
}

Renderer::Renderer(const std::filesystem::path& assets) : assetsRoot_(assets) {
    if (!assets.empty()) {
        pixeldesk::requestPixelImage(images_, "body", assets / "body.png", 1, 768, 1);
    }
}

bool Renderer::poll() {
    bool changed = images_.poll();
    const auto next = images_.get("body").image;
    changed |= chassis_ != next;
    chassis_ = next;
    revision_ += changed;
    return changed;
}

bool Renderer::pending() const {
    return images_.pending() || images_.get("body").image != chassis_;
}

void Renderer::pause(bool paused) {
    images_.pause(paused);
}

cdsdk::ImageSlotStats Renderer::assets() const {
    return images_.stats();
}

void Renderer::stationary(Canvas& canvas, bool background, Geometry g) {
    if (background) {
        canvas.saved([&] {
            canvas.translate(31, 138);
            if (chassis_) {
                constexpr double factor = 2107. / 521;
                canvas.image(*chassis_, {-2 * factor, -2 * factor, 525 * factor, 118 * factor},
                             {.fit = cybercanvas::ImageFit::Stretch,
                              .clip = false,
                              .sampling = cybercanvas::ImageSampling::Nearest});
            } else {
                stepped(canvas, {0, 0, 2107, 459}, 25, kMint);
                stepped(canvas, {35, 35, 2037, 389}, 16, kInk);
            }
        });
    }
    for (size_t i = 0; i < kSlots; ++i) {
        const auto r = g.slot(i);
        stepped(canvas, {r.x - 6, r.y - 7, r.width + 12, r.height + 14}, 8, 0x07131bff);
        stepped(canvas, {r.x + 17, r.y - 18, r.width - 34, 12}, 4, 0x2b424fff);
    }
}

void Renderer::mechanism(Canvas& canvas, const Model& model, Geometry g) {
    for (size_t i = 0; i < kSlots; ++i) {
        const auto r = g.slot(i);
        const double y = g.mechanismY() + std::sin(model.angle() + i * .2) * model.power() * 2;
        stepped(canvas, {r.x + r.width / 2 - 13, y, 26, 14}, 3, kInk);
        stepped(canvas, {r.x + r.width / 2 - 10, y + 2, 20, 9}, 2, kAmber);
        canvas.box(r.x + r.width / 2 - 7, y + 2, 9, 3, 0, kCream);
    }
}

void Renderer::tile(Canvas& canvas, Rect r, const std::wstring& before, const std::wstring& after, double progress,
                    const cybercanvas::TextStyle& style) {
    const double hinge = r.y + r.height / 2;
    const auto face = [&](const std::wstring& text) {
        stepped(canvas, r, 6, 0x233b4aff);
        canvas.box(r.x + 3, r.y + 4, r.width - 6, 3, 0, 0x3a5361ff);
        canvas.box(r.x + 3, r.y + r.height - 7, r.width - 6, 4, 0, 0x142630ff);
        if (!signGlyph(canvas, text, r)) {
            canvas.text(text, style, {r.x + 7, r.y, r.width - 14, r.height}, kCream);
        }
    };
    const auto half = [&](const std::wstring& text, bool lower) {
        canvas.clipped({r.x, lower ? hinge : r.y, r.width, r.height / 2}, [&] {
            face(text);
        });
    };
    if (progress >= 1) {
        face(after);
    } else {
        half(after, false);
        half(before, true);
        const bool lower = progress >= .5;
        const double p = lower ? (progress - .5) * 2 : progress * 2;
        const double compression = lower ? std::min(1.02, cybercanvas::eased(p, cybercanvas::Ease::OutBack))
                                         : std::cos(p * std::numbers::pi / 2);
        if (compression > .015) {
            canvas.saved([&] {
                canvas.translate(0, hinge);
                canvas.scale(1, compression);
                canvas.translate(0, -hinge);
                half(lower ? after : before, lower);
                canvas.box(r.x, lower ? hinge : r.y, r.width, r.height / 2, 0,
                           uint32_t(std::clamp((1 - compression) * .5, 0., .5) * 255));
            });
        }
    }
    canvas.line(r.x, hinge, r.x + r.width, hinge, 0x07131bff, 3);
    for (double x : {r.x, r.x + r.width}) {
        stepped(canvas, {x - 3, hinge - 10, 6, 20}, 2, kInk);
    }
}

void Renderer::draw(Canvas& canvas, const Model& model, bool background, double scale, Format format, double aspect) {
    const auto g = geometry(format, aspect);
    const double dpi = canvas.devicePixelRatio();
    if (!assetsRoot_.empty()) {
        const auto edge = uint32_t(std::clamp(std::ceil(g.width * scale * dpi / 64) * 64, 64., 4096.));
        pixeldesk::requestPixelImage(images_, "body", assetsRoot_ / "body.png", 1, edge, dpi);
    }
    poll();
    const auto width = uint32_t(std::max(1., std::ceil(g.width * scale * dpi))),
               height = uint32_t(std::max(1., std::ceil(g.height * scale * dpi)));
    if (!backdrop_.bitmap() || owner_.Get() != canvas.target.Get() || width_ != width || height_ != height ||
        dpi_ != dpi || background_ != background || cachedRevision_ != revision_) {
        canvas.renderLayer(backdrop_, {width, height, dpi}, [&](Canvas& layer) {
            layer.scale(scale, scale);
            stationary(layer, background, g);
        });
        owner_ = canvas.target.Get();
        width_ = width;
        height_ = height;
        dpi_ = dpi;
        background_ = background;
        cachedRevision_ = revision_;
    }
    canvas.target->DrawBitmap(backdrop_.bitmap(), D2D1::RectF(0, 0, float(g.width), float(g.height)), 1,
                              D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    mechanism(canvas, model, g);
    cybercanvas::TextStyle style;
    style.family = L"Microsoft YaHei";
    style.size = 140;
    style.align = cybercanvas::TextAlign::Center;
    style.vertical = cybercanvas::VerticalAlign::Center;
    style.bold = true;
    style.ellipsis = true;
    double widest = 1;
    for (const auto& page : {model.before(), model.after()}) {
        for (const auto& glyph : page) {
            widest = std::max(widest, canvas.measureTextInk(glyph, style).width);
        }
    }
    style.size *= std::min(1., (g.slot(0).width - 14) / widest);
    for (size_t i = 0; i < kSlots; ++i) {
        tile(canvas, g.slot(i), model.before()[i], model.after()[i], model.progress(i), style);
    }
}
}  // namespace mechanicalsign
