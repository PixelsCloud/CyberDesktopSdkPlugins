#include "plugins/steampunk-desk/MarbleClock.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <tuple>

#include "plugins/mechanical-instruments/PhysicsMaterials.h"
#include "plugins/mechanical-pixels/Pixels.h"
#include "plugins/steampunk-desk/SteamDigits.h"
#include "plugins/steampunk-desk/SteamParts.h"

namespace steamdesk {
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
    throw std::invalid_argument("Unknown Steamworks marble clock");
}

cdsdk::Settings marbleDefaults() {
    return {{{"theme", CD_SETTING_CHOICE, 0, 0, "dark", 0, 0, {"dark", "light", "system"}},
             {"show_background", CD_SETTING_TOGGLE, 1},
             {"running", CD_SETTING_TOGGLE, 1},
             {"reduced_motion", CD_SETTING_TOGGLE, 0},
             {"text_color", CD_SETTING_COLOR, kCream},
             {"ball_color", CD_SETTING_COLOR, 0xa8adb0ff}}};
}

MarbleView marbleView(CdRect safe, const marbleclock::Model& model) {
    // The skin surrounds the existing simulation rather than shrinking its
    // return channel under a thick bottom bezel. All physics uses one transform.
    const double width = model.shape().width + 32, height = 416;
    const double margin = std::min(safe.width, safe.height) * .025;
    const double scale = std::min((safe.width - margin * 2) / width, (safe.height - margin * 2) / height);
    return {{safe.x + (safe.width - width * scale) / 2 + 16 * scale,
             safe.y + (safe.height - height * scale) / 2 + 12 * scale},
            scale};
}

namespace {
void rail(Canvas& canvas, const marbleclock::Segment& segment) {
    const auto a = segment.first, b = segment.second;
    canvas.line(a.x, a.y + 2, b.x, b.y + 2, kInk, segment.radius * 2 + 3);
    canvas.line(a.x, a.y, b.x, b.y, 0x765333ff, segment.radius * 2);
    canvas.line(a.x, a.y - 1.5, b.x, b.y - 1.5, 0xbfa879ff, segment.radius * 1.2);
    canvas.line(a.x, a.y - 2, b.x, b.y - 2, 0xfff9e5ff, 1.2);
    for (Point point : {a, b}) {
        pixelDisc(canvas, point.x, point.y, segment.radius, 0x765333ff);
        pixelDisc(canvas, point.x, point.y - 1, segment.radius * .62, kCream);
    }
}

void gear(Canvas& canvas, Point center, double radius, double angle, uint32_t color) {
    canvas.saved([&] {
        canvas.translate(center.x, center.y);
        canvas.rotate(angle * 180 / std::numbers::pi);
        for (unsigned i = 0; i < 12; ++i) {
            canvas.saved([&] {
                canvas.rotate(i * 30.);
                stepped(canvas, {-radius * .15, -radius, radius * .3, radius * .42}, radius * .07, kInk);
                stepped(canvas, {-radius * .10, -radius + 2, radius * .2, radius * .36}, radius * .05, color);
            });
        }
        pixelDisc(canvas, 0, 0, radius * .82, kInk);
        pixelDisc(canvas, 0, 0, radius * .72, color);
        pixelDisc(canvas, 0, 0, radius * .53, kInk);
        for (unsigned i = 0; i < 4; ++i) {
            canvas.saved([&] {
                canvas.rotate(i * 90.);
                canvas.box(-radius * .10, -radius * .53, radius * .2, radius * .55, 0, color);
            });
        }
        pixelBolt(canvas, 0, 0, radius * .24);
    });
}

void digit(Canvas& canvas, wchar_t value, Rect area, uint32_t color) {
    pixelDigits(canvas, std::string(1, char(value)), area, color);
}

void spriteCase(Canvas& canvas, const cybercanvas::ImageData& image, const MarbleSpec& spec, Rect bounds) {
    const double scale = bounds.width / spec.visible.width, natural = spec.visible.height * scale;
    // Extend only straight sidewalls. Corner brackets and screws keep a uniform
    // scale, and no decoration is added to the marble's working or return path.
    const std::array<double, 4> from{0, natural * .22, natural * .27, natural};
    const double extra = bounds.height - natural;
    const std::array<double, 4> to{0, from[1], from[2] + extra, bounds.height};
    for (unsigned i = 0; i < 3; ++i) {
        const double stretch = (to[i + 1] - to[i]) / (from[i + 1] - from[i]);
        canvas.clipped({bounds.x, bounds.y + to[i], bounds.width, to[i + 1] - to[i]}, [&] {
            canvas.image(
                image,
                {bounds.x - spec.visible.x * scale, bounds.y + to[i] - (spec.visible.y * scale + from[i]) * stretch,
                 spec.source.width * scale, spec.source.height * scale * stretch},
                {.fit = cybercanvas::ImageFit::Stretch, .clip = false});
        });
    }
}

void stationary(Canvas& canvas, const marbleclock::Geometry& shape, const MarbleSpec& spec,
                const std::shared_ptr<const cybercanvas::ImageData>& body,
                const std::shared_ptr<const cybercanvas::ImageData>& clock, bool background, bool light) {
    const Rect bounds{-16, -12, shape.width + 32, 416};
    if (background) {
        if (body) {
            spriteCase(canvas, *body, spec, bounds);
        } else {
            stepped(canvas, bounds, 16, kCream);
            stepped(canvas, {-6, -2, shape.width + 12, 380}, 12, kMint);
            stepped(canvas, {8, 12, shape.width - 16, 350}, 10, kInk);
        }
        if (light) {
            stepped(canvas, {20, 17, shape.width - 40, 342}, 12, 0xe2e8dbed);
        }
        // Thin supports sit on the rear plate; their endpoints never close a rail.
        for (double x :
             shape.width < 1000 ? std::vector<double>{205, 408, 592} : std::vector<double>{218, 496, 780, 935}) {
            canvas.line(x, 16, x, 154, light ? 0xab967aff : 0x4b4133ff, 4);
            pixelBolt(canvas, x, 27, 4);
        }
    }
    for (const auto* segments : {&shape.track, &shape.returning}) {
        for (const auto& segment : *segments) {
            rail(canvas, segment);
        }
    }
    for (unsigned i = 0; i < 2; ++i) {
        rail(canvas, shape.enclosure[i]);
    }
    const double left = shape.width - 365;
    if (clock) {
        mechanicalart::physicsmaterials::registeredImage(
            canvas, *clock, mechanicalart::physicsmaterials::registration("clock"), {left, 23, 328, 69});
    } else {
        canvas.gradientBox({left, 23, 328, 69}, 7, 0xb68b4eff, 0x51351fff);
        canvas.gradientBox({left + 3, 26, 322, 63}, 5, 0x28231bff, 0x12110fff);
    }
    // The cosmetic hopper ends at the real feeder at (92, 122).
    auto hopper = canvas.path();
    hopper.moveTo({38, 35})
        .lineTo({110, 35})
        .lineTo({88, 66})
        .lineTo({88, 107})
        .lineTo({113, 107})
        .lineTo({113, 120})
        .lineTo({77, 120})
        .lineTo({77, 66})
        .close();
    const D2D1_GRADIENT_STOP bronze[]{{0, cybercanvas::color(0xe0b773ff)},
                                      {.3f, cybercanvas::color(0x694523ff)},
                                      {.65f, cybercanvas::color(0xba8c4aff)},
                                      {1, cybercanvas::color(0x392719ff)}};
    const auto metal = canvas.linearGradient({38, 35}, {110, 108}, bronze);
    canvas.target->FillGeometry(hopper.geometry(canvas.factory.Get()), metal.Get());
    canvas.stroke(hopper, 0x4c321bff, {.width = 2});
    canvas.oval(74, 35, 38, 5, 0xc99e5aff);
    canvas.oval(74, 34, 33, 2.5, 0x2a2017ff);
}
}  // namespace

struct MarbleRenderer::Impl {
    using Key = std::tuple<double, bool, bool, const cybercanvas::ImageData*, const cybercanvas::ImageData*>;
    cdsdk::Images images;
    const MarbleSpec& spec;
    std::filesystem::path path;
    std::shared_ptr<const cybercanvas::ImageData> body, clock, material;
    std::optional<Key> key;
    ComPtr<ID2D1RenderTarget> owner;
    cybercanvas::SurfaceImage backdrop;
    std::array<cybercanvas::SurfaceImage, 3> gears;
    cybercanvas::SurfaceImage ball;
    std::optional<std::tuple<double, uint32_t, const cybercanvas::ImageData*>> spriteKey;
    uint64_t builds = 0;

    Impl(const CdCreateContext& context, const MarbleSpec& value) : spec(value) {
        images.bind(context);
        path = std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(context.plugin_directory))) /
               "assets" / "body.png";
    }

    void prepare(Canvas& canvas, const marbleclock::Model& model, bool background, bool light, double density) {
        const auto edge = uint32_t(std::clamp(std::ceil((model.shape().width + 32) * density / 64) * 64, 64., 4096.));
        images.set("body", path, 1, edge);
        images.set("gear", path.parent_path() / "precision-gear.png", 1,
                   uint32_t(std::clamp(std::ceil(70 * density / 32) * 32, 64., 384.)));
        images.set("clock", path.parent_path() / "clock-window.png", 1,
                   uint32_t(std::clamp(std::ceil(328 * density / 64) * 64, 128., 2048.)));
        images.poll();
        body = images.get("body").image;
        clock = images.get("clock").image;
        material = images.get("gear").image;
        const Key next{density, background, light, body.get(), clock.get()};
        if (owner == canvas.target && key == next) {
            return;
        }
        cybercanvas::OffscreenSurface surface;
        canvas.renderLayer(
            surface, {uint32_t(std::ceil((model.shape().width + 32) * density)), uint32_t(std::ceil(416 * density)), 1},
            [&](Canvas& layer) {
                layer.scale(density, density);
                layer.translate(16, 12);
                stationary(layer, model.shape(), spec, body, clock, background, light);
            });
        backdrop = surface.snapshot();
        owner = canvas.target;
        key = next;
        ++builds;
    }

    void prepareSprites(Canvas& canvas, uint32_t color, double density) {
        const auto next = std::make_tuple(density, color, material.get());
        if (ball && spriteKey == next) {
            return;
        }
        constexpr std::array<double, 3> radii{31, 28, 18};
        for (unsigned i = 0; i < gears.size(); ++i) {
            const double edge = radii[i] * 2 + 4;
            cybercanvas::OffscreenSurface sprite;
            canvas.renderLayer(sprite, {uint32_t(std::ceil(edge * density)), uint32_t(std::ceil(edge * density)), 1},
                               [&](Canvas& layer) {
                                   layer.scale(density, density);
                                   if (material) {
                                       layer.translate(edge / 2, edge / 2);
                                       mechanicalart::physicsmaterials::anchored(layer, *material, {1254, 1254},
                                                                                 {627, 621}, radii[i] / 557);
                                   } else {
                                       gear(layer, {edge / 2, edge / 2}, radii[i], 0, kAmber);
                                   }
                               });
            gears[i] = sprite.snapshot();
        }
        constexpr double radius = marbleclock::kRadius, edge = radius * 2 + 4;
        cybercanvas::OffscreenSurface sphere;
        canvas.renderLayer(
            sphere, {uint32_t(std::ceil(edge * density)), uint32_t(std::ceil(edge * density)), 1}, [&](Canvas& layer) {
                layer.scale(density, density);
                const double center = edge / 2;
                const D2D1_GRADIENT_STOP stops[]{{0, cybercanvas::color(0xf4f1eaff)},
                                                 {.2f, cybercanvas::color(color)},
                                                 {.68f, cybercanvas::color(0x5c6165ff)},
                                                 {1, cybercanvas::color(0x181b1eff)}};
                const auto metal =
                    layer.radialGradient({center, center}, {radius, radius}, stops, {-radius * .35, -radius * .4});
                layer.target->FillEllipse(
                    D2D1::Ellipse(D2D1::Point2F(float(center), float(center)), float(radius), float(radius)),
                    metal.Get());
                layer.oval(center - radius * .3, center - radius * .4, radius * .21, radius * .16, 0xfffff4cf);
            });
        ball = sphere.snapshot();
        spriteKey = next;
    }
};

MarbleRenderer::MarbleRenderer(const CdCreateContext& context, const MarbleSpec& spec)
    : impl_(std::make_unique<Impl>(context, spec)) {}

MarbleRenderer::~MarbleRenderer() = default;

void MarbleRenderer::onChange(std::function<void()> callback) {
    impl_->images.onChange(std::move(callback));
}

void MarbleRenderer::pause(bool paused) {
    impl_->images.pause(paused);
}

bool MarbleRenderer::ready() const {
    return impl_->body && impl_->clock && impl_->material && !impl_->images.pending() &&
           impl_->images.get("body").image == impl_->body && impl_->images.get("clock").image == impl_->clock &&
           impl_->images.get("gear").image == impl_->material;
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

void MarbleRenderer::draw(Canvas& canvas, const marbleclock::Model& model, const cdsdk::Settings& settings, bool light,
                          double scale, double dpi) {
    const auto& shape = model.shape();
    if (impl_->owner != canvas.target) {
        impl_->spriteKey.reset();
    }
    impl_->prepare(canvas, model, settings.at("show_background").integer != 0, light, scale * dpi);
    impl_->prepareSprites(canvas, uint32_t(settings.at("ball_color").integer), scale * dpi);
    canvas.target->DrawBitmap(impl_->backdrop.Get(), D2D1::RectF(-16, -12, float(shape.width + 16), 404), 1,
                              D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    const double angle = model.flapAngle();
    rail(canvas, {{shape.pivot + (shape.left - shape.pivot) * std::cos(angle),
                   marbleclock::kFloorY + (shape.left - shape.pivot) * std::sin(angle)},
                  {shape.pivot, marbleclock::kFloorY},
                  4.5});
    const auto drive = marbleclock::driveGeometry(shape, angle);
    canvas.line(drive.lever.x, drive.lever.y, drive.crank.x, drive.crank.y, kCream, 6);
    const auto drawGear = [&](unsigned index, Point center, double radius, double rotation) {
        canvas.saved([&] {
            canvas.translate(center.x, center.y);
            canvas.rotate(rotation * 180 / std::numbers::pi);
            const float half = float(radius + 2);
            canvas.target->DrawBitmap(impl_->gears[index].Get(), D2D1::RectF(-half, -half, half, half), 1,
                                      D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        });
    };
    drawGear(0, {65, 89}, 31, model.feederAngle());
    drawGear(1, drive.center, 28, drive.angle);
    drawGear(2, {shape.pivot + 106, 293}, 18, -drive.angle * 1.55);
    pixelBolt(canvas, shape.pivot, marbleclock::kFloorY, 8);
    const auto text = model.timeText(), previous = model.previousTimeText();
    const double progress = 1 - std::pow(1 - model.digitProgress(), 3);
    const double left = shape.width - 365;
    const auto ink = uint32_t(settings.at("text_color").integer);
    for (unsigned i = 0; i < 6; ++i) {
        const double x = left + 30.5 + (i / 2) * 99 + (i % 2) * 38;
        const unsigned index = i + i / 2;
        canvas.clipped({x, 32, 31, 51}, [&] {
            if (progress < 1 && text[index] != previous[index]) {
                digit(canvas, previous[index], {x + 4, 40 - progress * 51, 23, 35}, ink);
                digit(canvas, text[index], {x + 4, 40 + (1 - progress) * 51, 23, 35}, ink);
            } else {
                digit(canvas, text[index], {x + 4, 40, 23, 35}, ink);
            }
        });
    }
    for (const auto& marble : model.marbles()) {
        const auto body = model.state(marble);
        const double x = body.position.x * marbleclock::kPixelsPerMeter,
                     y = body.position.y * marbleclock::kPixelsPerMeter, radius = marbleclock::kRadius;
        canvas.target->DrawBitmap(
            impl_->ball.Get(),
            D2D1::RectF(float(x - radius - 2), float(y - radius - 2), float(x + radius + 2), float(y + radius + 2)), 1,
            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }
}

MarbleInstance::MarbleInstance(const CdCreateContext& context)
    : settings(marbleDefaults()),
      model_(marbleSpec(context.plugin_id).compact),
      renderer_(context, marbleSpec(context.plugin_id)) {
    canvas_.bindGpu(context);
    environment_.bind(context);
    interaction_.bind(context);
    redraw_.bind(context);
    updates_.bind(context);
    renderer_.onChange([this] {
        invalidate();
    });
    updates_.onInvalidate([this] {
        invalidate();
    });
    updates_.onUpdate([this](auto tick) {
        const bool moved = !frozen(settings) && model_.advance(tick.delta);
        const bool changed = synchronize(!frozen(settings));
        if (!model_.dragging() && interaction_.router.captured()) {
            interaction_.cancel();
        }
        if (schedule(settings) != CD_OK) {
            updates_.stop();
        }
        return cdsdk::UpdateStep{moved || changed, true};
    });
    synchronize(false);
    if (schedule(settings) != CD_OK) {
        throw std::runtime_error("Cannot schedule Steamworks marble clock");
    }
}

MarbleInstance::~MarbleInstance() {
    updates_.stop();
    renderer_.onChange({});
    cancel();
}

bool MarbleInstance::frozen(const cdsdk::Settings& values) const {
    return hidden_ || !values.at("running").integer || values.at("reduced_motion").integer;
}

bool MarbleInstance::synchronize(bool animate) {
    const auto environment = environment_.snapshot();
    const auto second = environment.unix_ms / 1000;
    if (lastSecond_ && second != *lastSecond_ && second != *lastSecond_ + 1) {
        model_.quiet();
        animate = false;
    }
    lastSecond_ = second;
    const auto local = cdsdk::Environment::localTime(environment);
    return model_.setTime(local.tm_hour, local.tm_min, local.tm_sec, animate);
}

CdResult MarbleInstance::schedule(const cdsdk::Settings& values) {
    if (hidden_) {
        updates_.stop();
        return CD_OK;
    }
    const auto now = environment_.snapshot();
    const auto boundary = std::max(16u, uint32_t(1000 - now.unix_ms % 1000));
    return updates_.start(!frozen(values) && model_.active() ? std::min(33u, boundary) : boundary, now.monotonic_ms);
}

CdResult MarbleInstance::render(const CdFrame& frame) {
    if (!canvas_.valid(frame)) {
        return CD_ERROR_ARGUMENT;
    }
    updates_.beginFrame();
    redraw_.beginFrame();
    const auto& theme = settings.at("theme").text;
    const bool light = theme == "light" || (theme == "system" && environment_.snapshot().theme == CD_ENV_THEME_LIGHT);
    canvas_.render(frame, safe_, [&](auto& canvas, CdRect safe) {
        view_ = marbleView(safe, model_);
        canvas.saved([&] {
            canvas.translate(view_.origin.x - safe.x, view_.origin.y - safe.y);
            canvas.scale(view_.scale, view_.scale);
            renderer_.draw(canvas, model_, settings, light, view_.scale, frame.device_pixel_ratio);
        });
        std::vector<cybercanvas::HitRegion> regions;
        if (!frozen(settings)) {
            regions.push_back({1, cybercanvas::HitShape::rectangle({40, 65, 86, 65})});
            regions.push_back({2, cybercanvas::HitShape::rectangle(
                                      {model_.shape().pivot - 19, marbleclock::kFloorY - 35, 150, 100})});
            if (interaction_.streaming()) {
                for (const auto& marble : model_.marbles()) {
                    const auto state = model_.state(marble);
                    regions.push_back(
                        {marble.token + 100,
                         cybercanvas::HitShape::ellipse({state.position.x * marbleclock::kPixelsPerMeter - 13,
                                                         state.position.y * marbleclock::kPixelsPerMeter - 13, 26, 26}),
                         cybercanvas::PointerBehavior::Drag, 1});
                }
            }
        }
        const auto result = interaction_.regions(std::move(regions));
        if (result.drag == cybercanvas::DragPhase::Cancel) {
            model_.cancelDrag();
        }
        laidOut_ = true;
    });
    return CD_OK;
}

void MarbleInstance::cancel() {
    interaction_.cancel();
    model_.cancelDrag();
}

void MarbleInstance::input(const CdInput& event) {
    if (event.kind == CD_INPUT_CANCEL) {
        cancel();
        return;
    }
    if (frozen(settings) || !laidOut_ || !std::isfinite(event.x) || !std::isfinite(event.y)) {
        return;
    }
    auto local = event;
    local.x = (event.x - view_.origin.x) / view_.scale;
    local.y = (event.y - view_.origin.y) / view_.scale;
    const auto result = interaction_.input(local);
    const Point target{local.x, local.y};
    if (result.drag == cybercanvas::DragPhase::Begin) {
        if (!model_.beginDrag(result.target - 100, target)) {
            interaction_.cancel();
        }
    } else if (result.drag == cybercanvas::DragPhase::Update) {
        model_.drag(target);
    } else if (result.drag == cybercanvas::DragPhase::End || result.drag == cybercanvas::DragPhase::Cancel) {
        model_.cancelDrag();
    }
    if (result.clicked == 1) {
        model_.release();
    } else if (result.clicked == 2 ||
               (event.kind == CD_INPUT_DOUBLE_CLICK &&
                std::hypot(target.x - model_.shape().pivot, target.y - marbleclock::kFloorY) < 30)) {
        cancel();
        model_.drain();
    }
    schedule(settings);
    invalidate();
}

void MarbleInstance::layoutChanged(const CdLayout& layout) {
    if (!std::isfinite(layout.logical_width) || !std::isfinite(layout.logical_height) || layout.logical_width <= 0 ||
        layout.logical_height <= 0) {
        return;
    }
    cancel();
    laidOut_ = false;
    const auto safe = layout.safe_content;
    safe_ = std::isfinite(safe.x) && std::isfinite(safe.y) && std::isfinite(safe.width) && std::isfinite(safe.height) &&
                    safe.x >= 0 && safe.y >= 0 && safe.width > 0 && safe.height > 0 &&
                    safe.x + safe.width <= layout.logical_width && safe.y + safe.height <= layout.logical_height
                ? safe
                : CdRect{0, 0, layout.logical_width, layout.logical_height};
}

void MarbleInstance::setPaused(bool paused) {
    hidden_ = paused;
    renderer_.pause(paused);
    cancel();
    updates_.pause(paused);
    redraw_.pause(paused);
    model_.quiet();
    if (!paused) {
        synchronize(false);
        invalidate();
    }
    schedule(settings);
}

CdResult MarbleInstance::applySettings(const CdSettingValue* values, uint32_t count) {
    auto next = settings;
    const auto result = next.apply(values, count);
    if (result != CD_OK) {
        return result;
    }
    const auto scheduled = schedule(next);
    if (scheduled != CD_OK) {
        return scheduled;
    }
    const bool changedMotion = next.at("running").integer != settings.at("running").integer ||
                               next.at("reduced_motion").integer != settings.at("reduced_motion").integer;
    cancel();
    settings = std::move(next);
    if (changedMotion) {
        model_.quiet();
        synchronize(false);
    }
    invalidate();
    return CD_OK;
}

void MarbleInstance::invalidate() {
    redraw_.invalidate();
}

bool MarbleInstance::save(const std::filesystem::path& path, const CdFrame& frame) {
    return canvas_.save(path.wstring(), frame);
}

bool MarbleInstance::ready() const {
    return renderer_.ready();
}

const marbleclock::Model& MarbleInstance::model() const {
    return model_;
}

uint64_t MarbleInstance::cacheBuilds() const {
    return renderer_.cacheBuilds();
}
}  // namespace steamdesk
