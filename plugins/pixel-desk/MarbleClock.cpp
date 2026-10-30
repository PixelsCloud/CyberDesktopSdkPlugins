#include "plugins/pixel-desk/MarbleClock.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <tuple>

#include "plugins/mechanical-pixels/Pixels.h"
#include "plugins/pixel-desk/PixelImages.h"
#include "plugins/pixel-desk/PixelLayout.h"
#include "plugins/pixel-desk/PixelParts.h"

namespace pixeldesk {
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
    throw std::invalid_argument("Unknown Pixel Desk marble clock");
}

cdsdk::Settings marbleDefaults() {
    return {{{"theme", CD_SETTING_CHOICE, 0, 0, "dark", 0, 0, {"dark", "light", "system"}},
             {"show_background", CD_SETTING_TOGGLE, 1},
             {"running", CD_SETTING_TOGGLE, 1},
             {"reduced_motion", CD_SETTING_TOGGLE, 0},
             {"text_color", CD_SETTING_COLOR, kInk},
             {"ball_color", CD_SETTING_COLOR, 0xe7edf1ff}}};
}

namespace {
struct MarbleComposition {
    Point origin;
    double scale;
};

MarbleComposition composition(bool compact) {
    return compact ? MarbleComposition{{40, 54}, .95} : MarbleComposition{{60, 80}, 1.2};
}

Rect caseBounds(const MarbleSpec& spec) {
    const auto c = composition(spec.compact);
    return {-c.origin.x / c.scale, -c.origin.y / c.scale, spec.visible.width / c.scale, spec.visible.height / c.scale};
}

Rect digitBounds(const MarbleSpec& spec, unsigned index) {
    const auto c = composition(spec.compact);
    const std::array<double, 6> compact{590, 657, 748.5, 817, 899, 966.5};
    const std::array<double, 6> wide{1543, 1638, 1757, 1852, 1964.5, 2060.5};
    const double center = spec.compact ? compact[index] : wide[index];
    const double width = spec.compact ? 34 : 46, height = spec.compact ? 53 : 76;
    const double y = spec.compact ? 53 : 71;
    return {(center - width / 2 - c.origin.x) / c.scale, (y - c.origin.y) / c.scale, width / c.scale, height / c.scale};
}
}  // namespace

MarbleView marbleView(CdRect safe, const marbleclock::Model& model) {
    const auto& spec = kMarbleCatalog[model.shape().width < 1500 ? 0 : 1];
    const auto c = composition(spec.compact);
    const auto bounds = fitBody(safe.width, safe.height, spec.visible.width, spec.visible.height);
    const double scale = bounds.width / spec.visible.width;
    return {{safe.x + bounds.x + c.origin.x * scale, safe.y + bounds.y + c.origin.y * scale}, scale * c.scale};
}

namespace {
void rail(Canvas& canvas, const marbleclock::Segment& segment) {
    const auto a = segment.first, b = segment.second;
    canvas.line(a.x, a.y + 2, b.x, b.y + 2, kInk, segment.radius * 2 + 3);
    canvas.line(a.x, a.y, b.x, b.y, 0x9b927aff, segment.radius * 2);
    canvas.line(a.x, a.y - 1.5, b.x, b.y - 1.5, kCream, segment.radius * 1.2);
    canvas.line(a.x, a.y - 2, b.x, b.y - 2, 0xfff9e5ff, 1.2);
    for (Point point : {a, b}) {
        pixelDisc(canvas, point.x, point.y, segment.radius, 0x9b927aff);
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
    const std::string text(1, char(value));
    const double ux = area.width / 5, uy = area.height / 7;
    for (unsigned row = 0; row < 7; ++row) {
        for (unsigned column = 0; column < 5; ++column) {
            if (mechanicalpixels::clockCell(text, column + 1, row + 1)) {
                canvas.box(area.x + column * ux, area.y + row * uy, ux + .02, uy + .02, 0, color);
            }
        }
    }
}

void stationary(Canvas& canvas, const marbleclock::Geometry& shape, const MarbleSpec& spec,
                const std::shared_ptr<const cybercanvas::ImageData>& body, bool background, bool light) {
    const auto bounds = caseBounds(spec);
    if (background && body) {
        const double scale = bounds.width / spec.visible.width;
        canvas.image(
            *body,
            {bounds.x - spec.visible.x * scale, bounds.y - spec.visible.y * scale, spec.source.width * scale,
             spec.source.height * scale},
            {.fit = cybercanvas::ImageFit::Stretch, .clip = false, .sampling = cybercanvas::ImageSampling::Nearest});
    }
    if (background && light) {
        stepped(canvas, {35, 100, shape.width - 70, 238}, 9, 0xe2e8dbed);
    }
    for (const auto* segments : {&shape.track, &shape.returning}) {
        for (const auto& segment : *segments) {
            rail(canvas, segment);
        }
    }
    for (unsigned i = 0; i < 2; ++i) {
        rail(canvas, shape.enclosure[i]);
    }
    if (!background) {
        for (unsigned i = 0; i < 6; ++i) {
            const auto d = digitBounds(spec, i);
            stepped(canvas, {d.x - 6, d.y - 7, d.width + 12, d.height + 14}, 4, kCream);
        }
    }
    stepped(canvas, {40, 65, 70, 59}, 8, kInk);
    stepped(canvas, {43, 68, 64, 53}, 6, 0x839d8aff);
    stepped(canvas, {91, 104, 28, 17}, 4, kCream);
    canvas.box(97, 108, 16, 9, 0, kInk);
}

}  // namespace

struct MarbleRenderer::Impl {
    using Key = std::tuple<double, bool, bool, const cybercanvas::ImageData*>;
    cdsdk::Images images;
    const MarbleSpec& spec;
    std::filesystem::path path;
    std::shared_ptr<const cybercanvas::ImageData> body;
    std::optional<Key> key;
    ComPtr<ID2D1RenderTarget> owner;
    cybercanvas::SurfaceImage backdrop;
    std::array<cybercanvas::SurfaceImage, 3> gears;
    cybercanvas::SurfaceImage ball;
    std::optional<std::tuple<double, uint32_t>> spriteKey;
    uint64_t builds = 0;

    Impl(const CdCreateContext& context, const MarbleSpec& value) : spec(value) {
        images.bind(context);
        path = std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(context.plugin_directory))) /
               "assets" / "body.png";
    }

    void prepare(Canvas& canvas, const marbleclock::Model& model, bool background, bool light, double density) {
        const auto bounds = caseBounds(spec);
        const auto edge = uint32_t(std::clamp(std::ceil(bounds.width * density / 64) * 64, 64., 4096.));
        pixeldesk::requestPixelImage(images, "body", path, 1, edge, canvas.devicePixelRatio());
        images.poll();
        body = images.get("body").image;
        const Key next{density, background, light, body.get()};
        if (owner == canvas.target && key == next) {
            return;
        }
        cybercanvas::OffscreenSurface surface;
        canvas.renderLayer(
            surface, {uint32_t(std::ceil(bounds.width * density)), uint32_t(std::ceil(bounds.height * density)), 1},
            [&](Canvas& layer) {
                layer.scale(density, density);
                layer.translate(-bounds.x, -bounds.y);
                stationary(layer, model.shape(), spec, body, background, light);
            });
        backdrop = surface.snapshot();
        owner = canvas.target;
        key = next;
        ++builds;
    }

    void prepareSprites(Canvas& canvas, uint32_t color, double density) {
        const auto next = std::make_tuple(density, color);
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
                                   gear(layer, {edge / 2, edge / 2}, radii[i], 0, i == 1 ? kClay : kAmber);
                               });
            gears[i] = sprite.snapshot();
        }
        constexpr double radius = kMarbleRadius, edge = radius * 2 + 4;
        cybercanvas::OffscreenSurface sphere;
        canvas.renderLayer(sphere, {uint32_t(std::ceil(edge * density)), uint32_t(std::ceil(edge * density)), 1},
                           [&](Canvas& layer) {
                               layer.scale(density, density);
                               const double center = edge / 2;
                               pixelDisc(layer, center, center, radius, 0x627581ff);
                               pixelDisc(layer, center, center - .3, radius - 1.2, color);
                               layer.box(center - 4.5, center - 5.5, 5.5, 4, 0, 0xfff4d5bb);
                               layer.box(center - 6, center - 3, 2, 4, 0, 0xfff4d580);
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
    return impl_->body && !impl_->images.pending() && impl_->images.get("body").image == impl_->body;
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
    const auto bounds = caseBounds(impl_->spec);
    canvas.target->DrawBitmap(
        impl_->backdrop.Get(),
        D2D1::RectF(float(bounds.x), float(bounds.y), float(bounds.x + bounds.width), float(bounds.y + bounds.height)),
        1, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
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
                                      D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
        });
    };
    drawGear(0, {65, 89}, 31, model.feederAngle());
    drawGear(1, drive.center, 28, drive.angle);
    drawGear(2, {shape.pivot + 106, 293}, 18, -drive.angle * 1.55);
    pixelBolt(canvas, shape.pivot, marbleclock::kFloorY, 8);
    const auto text = model.timeText(), previous = model.previousTimeText();
    const double progress = 1 - std::pow(1 - model.digitProgress(), 3);
    const auto ink = uint32_t(settings.at("text_color").integer);
    for (unsigned i = 0; i < 6; ++i) {
        const auto area = digitBounds(impl_->spec, i);
        const unsigned index = i + i / 2;
        canvas.clipped(area, [&] {
            if (progress < 1 && text[index] != previous[index]) {
                auto old = area, next = area;
                old.y -= progress * area.height;
                next.y += (1 - progress) * area.height;
                digit(canvas, previous[index], old, ink);
                digit(canvas, text[index], next, ink);
            } else {
                digit(canvas, text[index], area, ink);
            }
        });
    }
    const auto c = composition(impl_->spec.compact);
    for (unsigned pair = 0; pair < 2; ++pair) {
        const auto left = digitBounds(impl_->spec, pair * 2 + 1), right = digitBounds(impl_->spec, pair * 2 + 2);
        const double x = (left.x + left.width + right.x) / 2, center = left.y + left.height / 2;
        for (double offset : {-.18, .18}) {
            pixelDisc(canvas, x, center + offset * left.height, (impl_->spec.compact ? 3.5 : 5.) / c.scale, kAmber);
        }
    }
    for (const auto& marble : model.marbles()) {
        const auto body = model.state(marble);
        const double x = body.position.x * marbleclock::kPixelsPerMeter,
                     y = body.position.y * marbleclock::kPixelsPerMeter, radius = kMarbleRadius;
        canvas.target->DrawBitmap(
            impl_->ball.Get(),
            D2D1::RectF(float(x - radius - 2), float(y - radius - 2), float(x + radius + 2), float(y + radius + 2)), 1,
            D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
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
        throw std::runtime_error("Cannot schedule Pixel Desk marble clock");
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
                constexpr double hitRadius = kMarbleRadius + 2;
                for (const auto& marble : model_.marbles()) {
                    const auto state = model_.state(marble);
                    regions.push_back(
                        {marble.token + 100,
                         cybercanvas::HitShape::ellipse({state.position.x * marbleclock::kPixelsPerMeter - hitRadius,
                                                         state.position.y * marbleclock::kPixelsPerMeter - hitRadius,
                                                         hitRadius * 2, hitRadius * 2}),
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
}  // namespace pixeldesk
