#include <CyberCanvas/PanelDrawing.h>

#include <algorithm>
#include <cmath>
#include <numbers>

#include "plugins/mechanical-sign/Sign.h"

namespace mechanicalsign {
namespace {
using cybercanvas::Canvas;
using cybercanvas::Point;
using cybercanvas::Rect;

void metal(Canvas& canvas, Rect bounds, bool brass = false, double corner = 4) {
    canvas.gradientBox(bounds, corner, brass ? 0xd6b86eff : 0x9ea8aaff, brass ? 0x7c5a2bff : 0x434c51ff);
}
}  // namespace

void Renderer::bind(const CdCreateContext& context) {
    images_.bind(context);
}

Renderer::Renderer(const std::filesystem::path& assets) : assetsRoot_(assets) {
    if (!assets.empty()) {
        images_.set("chassis", assets / "approved-body.png", 1, 1400);
    }
}

bool Renderer::poll() {
    const bool changed = images_.poll();
    if (const auto next = images_.get("chassis").image) {
        chassis_ = next;
    }
    revision_ += changed;
    return changed;
}

bool Renderer::pending() const {
    return images_.pending();
}

void Renderer::pause(bool paused) {
    images_.pause(paused);
}

cdsdk::ImageSlotStats Renderer::assets() const {
    return images_.stats();
}

void Renderer::stationary(Canvas& canvas, bool background, Geometry geometry) {
    if (background && chassis_) {
        canvas.image(*chassis_, {0, 0, geometry.width, geometry.height},
                     {.fit = cybercanvas::ImageFit::Stretch, .clip = false});
    }
}

void Renderer::mechanism(Canvas& canvas, const Model& model, Geometry geometry) {
    (void)geometry;
    if (!model.active() || !chassis_) {
        return;
    }
    // The original photographed gear assembly flexes only along its axle.
    // Keeping its image pixels preserves the approved machined appearance.
    canvas.clipped({76, 267, 144, 209}, [&] {
        canvas.saved([&] {
            canvas.translate(148, 365);
            canvas.scale(1, .985 + .015 * std::cos(model.angle() * 4));
            canvas.translate(-148, -365);
            canvas.image(*chassis_, {0, 0, 2172, 724}, {.fit = cybercanvas::ImageFit::Stretch, .clip = false});
        });
    });
}

void Renderer::tile(Canvas& c, Rect r, const std::wstring& before, const std::wstring& after, double progress,
                    const cybercanvas::TextStyle& style) {
    const double hinge = r.y + r.height / 2;
    auto face = [&](const std::wstring& text) {
        if (chassis_) {
            c.clipped(r, [&] {
                c.image(*chassis_, {0, 0, 2172, 724}, {.fit = cybercanvas::ImageFit::Stretch, .clip = false});
            });
        } else {
            c.gradientBox(r, 5, 0x2b2d2eff, 0x15191bff);
        }
        if (!text.empty() && text != L" ") {
            c.text(text, style, {r.x + 4, r.y, r.width - 8, r.height}, 0xe6e1d5ff);
        }
    };
    auto half = [&](const std::wstring& text, bool lower) {
        c.clipped({r.x, lower ? hinge : r.y, r.width, r.height / 2}, [&] {
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
            c.saved([&] {
                c.translate(0, hinge);
                c.scale(1, compression);
                c.translate(0, -hinge);
                half(lower ? after : before, lower);
                const double shade = std::clamp((1 - compression) * .5, 0., .5);
                c.box(r.x, lower ? hinge : r.y, r.width, r.height / 2, 0, uint32_t(shade * 255));
            });
        }
        c.line(r.x + 1, hinge + (lower ? 1 : -1) * (r.height / 2 * compression), r.x + r.width - 1,
               hinge + (lower ? 1 : -1) * (r.height / 2 * compression), 0x96968a90, 1.3);
    }
    c.line(r.x, hinge + 1, r.x + r.width, hinge + 1, 0x010305ff, 2);
    c.line(r.x + 3, hinge + 2.8, r.x + r.width - 3, hinge + 2.8, 0x5b625e55, .8);
    for (double x : {r.x - 1, r.x + r.width + 1}) {
        metal(c, {x - 2, hinge - 7, 4, 14}, true, 1);
    }
}

void Renderer::draw(Canvas& canvas, const Model& model, bool background, double scale, Format format, double aspect) {
    const auto g = geometry(format, aspect);
    const double dpi = canvas.devicePixelRatio();
    if (!assetsRoot_.empty()) {
        const auto edge = uint32_t(std::clamp(std::ceil(g.width * scale * dpi), 64., 2172.));
        images_.set("chassis", assetsRoot_ / "approved-body.png", 1, edge);
    }
    poll();
    const auto width = uint32_t(std::clamp(std::ceil(g.width * scale * dpi), 1., 8192.));
    const auto height = uint32_t(std::clamp(std::ceil(g.height * scale * dpi), 1., 8192.));
    const bool cache = uint64_t(width) * height <= 2 * 1024 * 1024;
    if (cache && (!backdrop_.bitmap() || owner_.Get() != canvas.target.Get() || width != width_ || height != height_ ||
                  dpi != dpi_ || background != background_ || revision_ != cachedRevision_)) {
        canvas.renderLayer(backdrop_, {width, height, dpi}, [&](auto& layer) {
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
    if (cache) {
        canvas.target->DrawBitmap(backdrop_.bitmap(), D2D1::RectF(0, 0, float(g.width), float(g.height)), 1,
                                  D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    } else {
        stationary(canvas, background, g);
    }
    mechanism(canvas, model, g);
    std::vector<std::wstring> references;
    for (const auto& page : {model.before(), model.after()}) {
        for (const auto& glyph : page) {
            if (!glyph.empty() && std::find(references.begin(), references.end(), glyph) == references.end()) {
                references.push_back(glyph);
            }
        }
    }
    cybercanvas::TextStyle style;
    style.family = L"Bahnschrift";
    const auto first = g.slot(0);
    style.size = first.height * .72;
    style.bold = true;
    style.align = cybercanvas::TextAlign::Center;
    style.vertical = cybercanvas::VerticalAlign::Center;
    double widest = 1;
    for (const auto& glyph : references) {
        widest = std::max(widest, canvas.measureTextInk(glyph, style).width);
    }
    style.size *= std::min({1., (first.width - 12) / widest,
                            (first.height - 18) / std::max(1., canvas.measureText(L"CyDgp", style).height)});
    for (size_t i = 0; i < kSlots; ++i) {
        tile(canvas, g.slot(i), model.before()[i], model.after()[i], model.progress(i), style);
    }
}
}  // namespace mechanicalsign
