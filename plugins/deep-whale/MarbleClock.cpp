#include "plugins/deep-whale/MarbleClock.h"

#include <sdk/WindowsFonts.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

namespace deepwhale {
using cybercanvas::Canvas;
using cybercanvas::Point;
using cybercanvas::Rect;
using Microsoft::WRL::ComPtr;

const MarbleSpec& marbleSpec(std::string_view id) {
    for (const auto& spec : kMarbleCatalog) {
        if (id == spec.id) {
            return spec;
        }
    }
    throw std::invalid_argument("Unknown Deep Whale marble clock");
}

cdsdk::Settings marbleDefaults() {
    return {{{"theme", CD_SETTING_CHOICE, 0, 0, "dark", 0, 0, {"dark", "light", "system"}},
             {"show_background", CD_SETTING_TOGGLE, 1},
             {"running", CD_SETTING_TOGGLE, 1},
             {"reduced_motion", CD_SETTING_TOGGLE, 0},
             {"text_color", CD_SETTING_COLOR, 0x3f5b89ff},
             {"ball_color", CD_SETTING_COLOR, 0xb8daeeff}}};
}

namespace {
struct Registration {
    double width, pitch;
    Rect plot, backdrop;
    cybercanvas::ImageRegion bodySource, backdropSource;
    Rect face;

    Rect world(Rect r) const {
        return {(r.x - plot.x) / pitch, (r.y - plot.y) / pitch, r.width / pitch, r.height / pitch};
    }

    Rect body() const {
        const double s = std::min((width - 14) / bodySource.width, 186. / bodySource.height);
        return {(width - bodySource.width * s) / 2, (200 - bodySource.height * s) / 2, bodySource.width * s,
                bodySource.height * s};
    }
};

Registration registration(bool compact) {
    if (compact) {
        return {320,
                .25,
                {112, 81, 180, 90},
                {102, 67, 204, 122},
                {29, 33, 1550, 940},
                {8, 14, 1586, 945},
                {665, 175, 775, 147}};
    }
    return {480,
            .33,
            {7, 58, 356.4, 118.8},
            {15, 10, 351, 164},
            {18, 13, 1920, 788},
            {23, 12, 1789, 828},
            {930, 128, 450, 121}};
}

void rail(Canvas& c, const marbleclock::Segment& segment) {
    const auto a = segment.first, b = segment.second;
    const double dx = b.x - a.x, dy = b.y - a.y, len = std::hypot(dx, dy);
    double nx = -dy / len, ny = dx / len;
    if (ny < 0) {
        nx = -nx;
        ny = -ny;
    }
    const double r = segment.radius;
    if (std::abs(ny) > .3) {
        auto shape = c.path();
        shape.moveTo({a.x - nx * r, a.y - ny * r})
            .lineTo({b.x - nx * r, b.y - ny * r})
            .lineTo({b.x + nx * 13, b.y + ny * 13})
            .lineTo({a.x + nx * 13, a.y + ny * 13})
            .close();
        const D2D1_GRADIENT_STOP stops[]{{0, cybercanvas::color(0xe8fbffff)},
                                         {.4f, cybercanvas::color(0xa6dbf1ff)},
                                         {1, cybercanvas::color(0x557da9ff)}};
        auto brush = c.linearGradient({a.x, a.y - r}, {a.x + nx * 13, a.y + ny * 13}, stops);
        c.target->FillGeometry(shape.geometry(c.factory.Get()), brush.Get());
        c.stroke(shape, 0x557ba5ff, {.width = 1.4});
        c.line(a.x - nx * r, a.y - ny * r, b.x - nx * r, b.y - ny * r, 0xecfaffff, 2.4);
        c.line(a.x + nx * 13, a.y + ny * 13, b.x + nx * 13, b.y + ny * 13, 0xcfad80ff, 1.8);
    } else {
        c.line(a.x, a.y, b.x, b.y, 0x38527dff, r * 2 + 2.3);
        c.line(a.x, a.y, b.x, b.y, 0xdbc29cff, r * 2 + .7);
        c.line(a.x, a.y, b.x, b.y, 0xa6dbf1ff, std::max(1., r * 2 - 1.3));
        c.line(a.x - 1.2, a.y, b.x - 1.2, b.y, 0xeefbffff, 1.15);
    }
}
}  // namespace

MarbleView marbleView(CdRect safe, const marbleclock::Model& model) {
    const auto r = registration(model.shape().width < 1000);
    const double scale =
        std::min(std::max(1., safe.width - 14) / (r.width - 14), std::max(1., safe.height - 14) / 186.);
    return {{safe.x + (safe.width - r.width * scale) / 2 + r.plot.x * scale,
             safe.y + (safe.height - 200 * scale) / 2 + r.plot.y * scale},
            scale * r.pitch};
}

struct MarbleRenderer::Impl {
    using Key = std::tuple<double, bool, bool, const cybercanvas::ImageData*, const cybercanvas::ImageData*>;
    cdsdk::Images images;
    Registration r;
    std::filesystem::path assets;
    std::unique_ptr<cdsdk::WindowsFonts> font;
    std::shared_ptr<const cybercanvas::ImageData> body, back;
    std::function<void(const std::string&)> report;
    std::map<std::string, std::string> errors;
    std::optional<Key> key;
    std::optional<std::pair<double, uint32_t>> sphereKey;
    ComPtr<ID2D1RenderTarget> owner;
    cybercanvas::SurfaceImage fixed;
    std::array<cybercanvas::SurfaceImage, 3> balls;
    uint64_t builds = 0;

    Impl(const CdCreateContext& context, const MarbleSpec& spec) : r(registration(spec.compact)) {
        images.bind(context);
        assets = std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(context.plugin_directory))) /
                 "assets";
        if (context.host && context.host->log) {
            const auto log = context.host->log;
            const auto user = context.host_context;
            report = [log, user](const std::string& text) {
                log(user, 3, text.data(), uint32_t(text.size()));
            };
        }
    }

    void prepare(Canvas& c, const marbleclock::Model& model, bool background, bool light, double density) {
        const auto br = r.world(r.body()), bb = r.world(r.backdrop);
        images.set("body", assets / "body.png", 1, uint32_t(std::ceil(std::max(br.width, br.height) * density)),
                   r.bodySource);
        images.set("backdrop", assets / "backdrop.png", 1, uint32_t(std::ceil(std::max(bb.width, bb.height) * density)),
                   r.backdropSource);
        images.poll();
        body = images.get("body").image;
        back = images.get("backdrop").image;
        for (const auto* slot : {"body", "backdrop"}) {
            const auto snap = images.get(slot);
            if (snap.state == cybercanvas::ImageState::Failed && errors[slot] != snap.error) {
                errors[slot] = snap.error;
                if (report) {
                    report(std::string("Deep Whale ") + slot + ": " + snap.error);
                }
            }
        }
        if (!font) {
            font = std::make_unique<cdsdk::WindowsFonts>(c.nativeTextFactory(), assets / "BreeSerif-Regular.ttf");
        }
        c.fontCollection(font->collection());
        if (owner != c.target) {
            sphereKey.reset();
        }
        const Key next{density, background, light, body.get(), back.get()};
        if (owner == c.target && key == next) {
            return;
        }
        const auto bounds = r.world({0, 0, r.width, 200});
        cybercanvas::OffscreenSurface surface;
        c.renderLayer(surface,
                      {uint32_t(std::ceil(bounds.width * density)), uint32_t(std::ceil(bounds.height * density)), 1},
                      [&](Canvas& layer) {
                          layer.scale(density, density);
                          layer.translate(-bounds.x, -bounds.y);
                          if (background && back) {
                              layer.image(*back, bb, {.fit = cybercanvas::ImageFit::Contain, .clip = false});
                              if (light) {
                                  layer.box(bb.x + bb.width * .08, bb.y + bb.height * .13, bb.width * .84,
                                            bb.height * .68, 4, 0xf0f7ff25);
                              }
                          }
                          if (body) {
                              layer.image(*body, br, {.fit = cybercanvas::ImageFit::Contain, .clip = false});
                          }
                          const auto& shape = model.shape();
                          for (unsigned i = 2; i < shape.enclosure.size(); ++i) {
                              const auto s = shape.enclosure[i];
                              layer.line(s.first.x, s.first.y, s.second.x, s.second.y, 0x8fb9db60, 2.2);
                          }
                          for (const auto* segments : {&shape.track, &shape.returning}) {
                              for (const auto& s : *segments) {
                                  rail(layer, s);
                              }
                          }
                          for (unsigned i = 0; i < 2; ++i) {
                              rail(layer, shape.enclosure[i]);
                          }
                      });
        fixed = surface.snapshot();
        owner = c.target;
        key = next;
        ++builds;
    }

    void sprites(Canvas& c, double density, uint32_t ink) {
        const auto next = std::pair{density, ink};
        if (sphereKey == next) {
            return;
        }
        constexpr double radius = marbleclock::kRadius, edge = radius * 2 + 4;
        for (unsigned i = 0; i < 3; ++i) {
            const uint32_t color =
                ink == 0xb8daeeff ? std::array<uint32_t, 3>{0xe4edf5ff, 0x8cc5eaff, 0xc5bfdeff}[i] : ink;
            cybercanvas::OffscreenSurface surface;
            c.renderLayer(surface, {uint32_t(std::ceil(edge * density)), uint32_t(std::ceil(edge * density)), 1},
                          [&](Canvas& layer) {
                              layer.scale(density, density);
                              const double center = edge / 2;
                              const D2D1_GRADIENT_STOP stops[]{{0, cybercanvas::color(0xfffdf3ff)},
                                                               {.28f, cybercanvas::color(color)},
                                                               {1, cybercanvas::color(0x547baaff)}};
                              auto brush = layer.radialGradient({center, center}, {radius, radius}, stops,
                                                                {-radius * .35, -radius * .4});
                              layer.target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(float(center), float(center)),
                                                                      float(radius), float(radius)),
                                                        brush.Get());
                              layer.oval(center - 2.3, center - 3.1, 2.6, 1.65, 0xfffdf4df);
                          });
            balls[i] = surface.snapshot();
        }
        sphereKey = next;
    }
};

MarbleRenderer::MarbleRenderer(const CdCreateContext& context, const MarbleSpec& spec)
    : impl_(std::make_unique<Impl>(context, spec)) {}

MarbleRenderer::~MarbleRenderer() = default;

void MarbleRenderer::onChange(std::function<void()> callback) {
    impl_->images.onChange(std::move(callback));
}

void MarbleRenderer::pause(bool paused) {
    if (!paused && impl_->images.stats().failed) {
        impl_->images.clear();
        impl_->errors.clear();
        impl_->key.reset();
    }
    impl_->images.pause(paused);
}

bool MarbleRenderer::ready() const {
    return impl_->body && impl_->back && !impl_->images.pending() && impl_->images.get("body").image == impl_->body &&
           impl_->images.get("backdrop").image == impl_->back;
}

bool MarbleRenderer::pending() const {
    return impl_->images.pending();
}

uint64_t MarbleRenderer::cacheBuilds() const {
    return impl_->builds;
}

cdsdk::ImageSlotStats MarbleRenderer::imageStats() const {
    return impl_->images.stats();
}

void MarbleRenderer::draw(Canvas& c, const marbleclock::Model& model, const cdsdk::Settings& settings, bool light,
                          double scale, double dpi) {
    impl_->prepare(c, model, settings.at("show_background").integer != 0, light, scale * dpi);
    impl_->sprites(c, scale * dpi, uint32_t(settings.at("ball_color").integer));
    const auto& r = impl_->r;
    const auto bounds = r.world({0, 0, r.width, 200});
    c.target->DrawBitmap(
        impl_->fixed.Get(),
        D2D1::RectF(float(bounds.x), float(bounds.y), float(bounds.x + bounds.width), float(bounds.y + bounds.height)),
        1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    const auto& shape = model.shape();
    const double angle = model.flapAngle();
    rail(c, {{shape.pivot + (shape.left - shape.pivot) * std::cos(angle),
              marbleclock::kFloorY + (shape.left - shape.pivot) * std::sin(angle)},
             {shape.pivot, marbleclock::kFloorY},
             4.5});
    c.oval(shape.pivot, marbleclock::kFloorY, 10, 10, 0x435c86ff);
    c.oval(shape.pivot, marbleclock::kFloorY, 7.5, 7.5, 0xd7bb93ff);
    c.oval(shape.pivot - 1, marbleclock::kFloorY - 1, 4, 4, 0xfffaf0ff);
    const auto body = r.body();
    const double s = body.width / r.bodySource.width;
    const auto face = r.world({body.x + (r.face.x - r.bodySource.x) * s, body.y + (r.face.y - r.bodySource.y) * s,
                               r.face.width * s, r.face.height * s});
    cybercanvas::TextStyle style;
    style.family = impl_->font->family();
    style.size = face.height * .88;
    style.align = cybercanvas::TextAlign::Center;
    const auto fit =
        c.fitTextInk(model.timeText(), style,
                     {face.x + face.width * .04, face.y + face.height * .12, face.width * .92, face.height * .76});
    c.ink(uint32_t(settings.at("text_color").integer));
    c.target->DrawTextLayout(D2D1::Point2F(float(fit.origin.x), float(fit.origin.y)), fit.layout.Get(), c.brush.Get());
    unsigned i = 0;
    for (const auto& marble : model.marbles()) {
        const auto state = model.state(marble);
        const double x = state.position.x * marbleclock::kPixelsPerMeter,
                     y = state.position.y * marbleclock::kPixelsPerMeter, h = marbleclock::kRadius + 2;
        c.target->DrawBitmap(impl_->balls[i++ % 3].Get(),
                             D2D1::RectF(float(x - h), float(y - h), float(x + h), float(y + h)), 1,
                             D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }
}
}  // namespace deepwhale
