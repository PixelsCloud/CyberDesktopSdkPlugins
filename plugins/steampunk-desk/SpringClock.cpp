#include "plugins/steampunk-desk/SpringClock.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <tuple>

#include "plugins/mechanical-pixels/Pixels.h"
#include "plugins/steampunk-desk/SteamParts.h"
#include "plugins/steampunk-desk/SteamSprite.h"

namespace steamdesk {
using cybercanvas::Canvas;
using cybercanvas::Point;
using Microsoft::WRL::ComPtr;

const SpringSpec& springSpec(std::string_view id) {
    for (const auto& spec : kSpringCatalog) {
        if (id == spec.id) {
            return spec;
        }
    }
    throw std::invalid_argument("Unknown Steamworks spring clock");
}

cdsdk::Settings springDefaults(bool compact) {
    return {{{"speed", CD_SETTING_NUMBER, 0, .75, {}, .25, 2},
             {"running", CD_SETTING_TOGGLE, 1},
             {"reduced_motion", CD_SETTING_TOGGLE, 0},
             {"twelve_hour", CD_SETTING_TOGGLE, 0},
             {"text_color", CD_SETTING_COLOR, compact ? kInk : kCream},
             {"accent_color", CD_SETTING_COLOR, kMint}}};
}

cybercanvas::Rect springFrameBounds(const toys::Model& model) {
    // The compact gantry wraps the existing 112-unit physics world. Extra
    // decoration above and below that world used to shrink the whole 4x1 clock.
    return model.size().height < 150 ? cybercanvas::Rect{-22, 0, 404, 120}
                                     : cybercanvas::Rect{-22, -16, 404, model.size().height + 48};
}

SpringView springView(CdRect safe, const toys::Model& model) {
    const auto bounds = springFrameBounds(model);
    const bool compact = model.size().height < 150;
    // Match the matrix clocks' horizontal inset; keep one uniform transform for
    // the frame, rigid bodies, springs and pointer coordinates.
    const double margin = std::min(safe.width, safe.height) * (compact ? .025 : .045);
    const double scale =
        std::min((safe.width - margin * 2) / bounds.width, (safe.height - (compact ? 0 : margin * 2)) / bounds.height);
    return {{safe.x + (safe.width - bounds.width * scale) / 2 - bounds.x * scale,
             safe.y + (safe.height - bounds.height * scale) / 2 - bounds.y * scale},
            scale};
}

namespace {
void spring(Canvas& canvas, Point from, Point to) {
    const double dx = to.x - from.x, dy = to.y - from.y, length = std::hypot(dx, dy);
    if (length < .01) {
        return;
    }
    auto path = canvas.path();
    path.moveTo(from);
    const unsigned turns = unsigned(std::clamp(std::round(length / 4), 4., 24.));
    const unsigned segments = turns * 12;
    for (unsigned i = 1; i <= segments; ++i) {
        const double t = double(i) / segments;
        const double side = std::sin(t * turns * 2 * std::numbers::pi) * 2.8;
        path.lineTo({from.x + dx * t + dy / length * side, from.y + dy * t - dx / length * side});
    }
    canvas.stroke(path, kInk, {.width = 3.2});
    canvas.stroke(path, 0x7c4728ff, {.width = 2.3});
    canvas.stroke(path, 0xd4a66aff, {.width = .85});
}

void beam(Canvas& canvas, uint32_t accent) {
    stepped(canvas, {4, 2, 352, 24}, 4, kInk);
    stepped(canvas, {7, 4, 346, 20}, 3, 0x6b8278ff);
    canvas.box(16, 6, 328, 15, 0, accent);
    canvas.box(16, 5, 328, 3, 0, 0xfff5dfff);
    canvas.box(16, 20, 328, 2, 0, 0x527a66ff);
    for (double x : {7., 338.}) {
        stepped(canvas, {x, 3, 15, 22}, 3, kClay);
        canvas.box(x + 3, 4, 9, 3, 0, 0xe8b991ff);
        canvas.box(x + 3, 20, 9, 3, 0, 0x8c4e38ff);
        pixelBolt(canvas, x + 7.5, 13, 2.1);
        canvas.box(x + 4, 25, 9, 3, 0, 0x162129ff);
    }
}

void card(Canvas& canvas, double height, char digit, uint32_t ink) {
    // All card geometry remains registered to its actual rigid-body center.
    // The eyelet is at the same top attachment as the distance joint.
    stepped(canvas, {-4, -height / 2 - 5, 8, 8}, 2, kInk);
    stepped(canvas, {-2.5, -height / 2 - 3.5, 5, 5}, 1, 0xa89c85ff);
    canvas.box(-1, -height / 2 - 2, 2, 2, 0, kInk);
    stepped(canvas, {-22, -height / 2, 44, height + 1}, 4, kInk);
    stepped(canvas, {-20, -height / 2 + 1, 40, height - 1}, 3, 0x95765cff);
    stepped(canvas, {-18.5, -height / 2 + 2.5, 37, height - 4}, 2, kCream);
    canvas.box(-15, -height / 2 + 4, 30, 2, 0, 0xfff7e5ff);
    canvas.box(16, -height / 2 + 6, 2.5, height - 12, 0, 0xd5b994ff);
    canvas.box(-14, height / 2 - 2, 28, 3, 0, kClay);
    canvas.box(-14, height / 2 + 1, 28, 1.5, 0, 0x663f36ff);
    cybercanvas::TextStyle style;
    style.family = L"Georgia";
    style.size = height < 80 ? 49 : 72;
    style.align = cybercanvas::TextAlign::Center;
    style.vertical = cybercanvas::VerticalAlign::Center;
    const std::wstring text(1, wchar_t(digit));
    const auto fit = canvas.fitTextInk(text, style, {-15, -height / 2 + 6, 30, height - 12});
    canvas.ink(ink);
    canvas.target->DrawTextLayout(D2D1::Point2F(float(fit.origin.x), float(fit.origin.y)), fit.layout.Get(),
                                  canvas.brush.Get());
}
}  // namespace

struct SpringRenderer::Impl {
    using Key = std::tuple<bool, double, uint32_t, uint32_t, const cybercanvas::ImageData*>;
    cdsdk::Images images;
    std::filesystem::path path;
    std::shared_ptr<const cybercanvas::ImageData> artwork;

    explicit Impl(const CdCreateContext& context) {
        images.bind(context);
        path = std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(context.plugin_directory))) /
               "assets" / "body.png";
    }

    std::optional<Key> key;
    ComPtr<ID2D1RenderTarget> owner;
    cybercanvas::SurfaceImage beam;
    std::array<cybercanvas::SurfaceImage, 10> cards;
    uint64_t builds = 0;

    void prepare(Canvas& canvas, const toys::Model& model, const cdsdk::Settings& settings, double density) {
        const bool compact = model.size().height < 150;
        const auto ink = uint32_t(settings.at("text_color").integer),
                   accent = uint32_t(settings.at("accent_color").integer);
        const auto edge = uint32_t(std::clamp(std::ceil(1200 * density / 64) * 64, 256., 2048.));
        images.set("parts", path, 1, edge);
        images.poll();
        artwork = images.get("parts").image;
        const Key next{compact, density, ink, accent, artwork.get()};
        if (owner == canvas.target && key == next) {
            return;
        }
        cybercanvas::OffscreenSurface rail;
        const auto bounds = springFrameBounds(model);
        const double worldHeight = bounds.height;
        canvas.renderLayer(
            rail, {uint32_t(std::ceil(404 * density)), uint32_t(std::ceil(worldHeight * density)), 1},
            [&](Canvas& layer) {
                layer.scale(density, density);
                if (!artwork) {
                    layer.translate(-bounds.x, -bounds.y);
                    steamdesk::beam(layer, accent);
                    return;
                }
                const std::array<double, 4> sx{48, 225, 1310, 1490}, sy{25, 157, 457, 540};
                const std::array<double, 4> dx =
                    compact ? std::array<double, 4>{0, 19, 385, 404} : std::array<double, 4>{0, 34, 370, 404};
                const std::array<double, 4> dy{0, compact ? 14. : 34., worldHeight - (compact ? 9 : 18), worldHeight};
                for (unsigned y = 0; y < 3; ++y) {
                    for (unsigned x = 0; x < 3; ++x) {
                        const cybercanvas::Rect source =
                            compact && y == 0 && x == 1
                                ? cybercanvas::Rect{225, 25, 360, 132}
                                : cybercanvas::Rect{sx[x], sy[y], sx[x + 1] - sx[x], sy[y + 1] - sy[y]};
                        sprite(layer, *artwork, {1536, 1024}, source,
                               {dx[x], dy[y], dx[x + 1] - dx[x], dy[y + 1] - dy[y]});
                    }
                }
                if (compact) {
                    // Stretch the plain crossbar, then place the maker's
                    // plate at its original aspect ratio independently.
                    sprite(layer, *artwork, {1536, 1024}, {618, 50, 310, 70}, {177.2, 1.4, 49.6, 11.2});
                }
                // Fixed mounting stems connect the cosmetic beam to
                // the unchanged six Box2D anchors at y = 27.
                for (const auto& piece : model.pieces()) {
                    const auto anchor = model.anchor(piece);
                    const double x = anchor.x * 100 - bounds.x, y = anchor.y * 100 - bounds.y;
                    const double start = compact ? 11 : 25;
                    layer.line(x, start, x, y, 0x50351eff, 3.8);
                    layer.line(x - .5, start, x - .5, y, 0xcfaa70ff, 1.4);
                    layer.oval(x, y, 2.6, 2.6, 0x9d703dff, 1.4);
                }
            });
        beam = rail.snapshot();
        const double height = compact ? 62 : 110;
        for (unsigned i = 0; i < cards.size(); ++i) {
            cybercanvas::OffscreenSurface tile;
            canvas.renderLayer(
                tile, {uint32_t(std::ceil(48 * density)), uint32_t(std::ceil((height + 24) * density)), 1},
                [&](Canvas& layer) {
                    layer.scale(density, density);
                    layer.translate(24, height / 2 + 16);
                    if (artwork) {
                        const cybercanvas::Rect source =
                            compact ? cybercanvas::Rect{190, 592, 193, 385} : cybercanvas::Rect{1178, 554, 164, 428};
                        const double eyelet = compact ? 10 : 14;
                        sprite(layer, *artwork, {1536, 1024}, source, {-22, -height / 2 - eyelet, 44, height + eyelet});
                        cybercanvas::TextStyle style;
                        style.family = L"Georgia";
                        style.size = compact ? 50 : 75;
                        style.align = cybercanvas::TextAlign::Center;
                        const std::wstring text(1, wchar_t(L'0' + i));
                        const auto fit = layer.fitTextInk(text, style, {-15, -height / 2 + 7, 30, height - 14});
                        layer.ink(ink);
                        layer.target->DrawTextLayout(D2D1::Point2F(float(fit.origin.x), float(fit.origin.y)),
                                                     fit.layout.Get(), layer.brush.Get());
                    } else {
                        card(layer, height, char('0' + i), ink);
                    }
                });
            cards[i] = tile.snapshot();
        }
        owner = canvas.target;
        key = next;
        ++builds;
    }
};

SpringRenderer::SpringRenderer(const CdCreateContext& context) : impl_(std::make_unique<Impl>(context)) {}

void SpringRenderer::onChange(std::function<void()> callback) {
    impl_->images.onChange(std::move(callback));
}

void SpringRenderer::pause(bool paused) {
    impl_->images.pause(paused);
}

bool SpringRenderer::ready() const {
    return impl_->artwork && !impl_->images.pending() && impl_->images.get("parts").image == impl_->artwork;
}

SpringRenderer::~SpringRenderer() = default;

void SpringRenderer::draw(Canvas& canvas, const toys::Model& model, const cdsdk::Settings& settings, double scale,
                          double dpi) {
    impl_->prepare(canvas, model, settings, scale * dpi);
    const auto bounds = springFrameBounds(model);
    canvas.target->DrawBitmap(
        impl_->beam.Get(),
        D2D1::RectF(float(bounds.x), float(bounds.y), float(bounds.x + bounds.width), float(bounds.y + bounds.height)),
        1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    const auto digits = model.digits();
    const double height = model.size().height < 150 ? 62 : 110;
    for (size_t i = 0; i < model.pieces().size(); ++i) {
        const auto& piece = model.pieces()[i];
        const auto from = model.anchor(piece), to = model.attachment(piece);
        spring(canvas, {from.x * 100, from.y * 100}, {to.x * 100, to.y * 100});
        const auto body = model.state(i);
        canvas.saved([&] {
            canvas.translate(body.position.x * 100, body.position.y * 100);
            canvas.rotate(body.angle * 180 / std::numbers::pi);
            const unsigned digit =
                i < digits.size() && digits[i] >= '0' && digits[i] <= '9' ? unsigned(digits[i] - '0') : 0;
            canvas.target->DrawBitmap(impl_->cards[digit].Get(),
                                      D2D1::RectF(-24, float(-height / 2 - 16), 24, float(height / 2 + 8)), 1,
                                      D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        });
    }
    const double y = height < 80 ? 79 : 152;
    for (double x : {120.5, 232.5}) {
        for (double offset : {-6., 6.}) {
            stepped(canvas, {x - 2.5, y + offset - 2.5, 5, 5}, 1.25, kAmber);
            canvas.box(x - 1, y + offset - 1.75, 1.5, 1.5, 0, 0xfff6dfff);
        }
    }
    if (settings.at("twelve_hour").integer) {
        cybercanvas::TextStyle style;
        style.size = 7;
        style.align = cybercanvas::TextAlign::Center;
        canvas.text(model.afternoon() ? L"PM" : L"AM", style, {165, model.size().height - 10, 30, 9}, kCream);
    }
}

uint64_t SpringRenderer::cacheBuilds() const {
    return impl_->builds;
}

SpringInstance::SpringInstance(const CdCreateContext& context)
    : settings(springDefaults(springSpec(context.plugin_id).compact)),
      model_(toys::Kind::Clock, springSpec(context.plugin_id).compact, true),
      renderer_(context) {
    renderer_.onChange([this] {
        invalidate();
    });
    canvas_.bindGpu(context);
    environment_.bind(context);
    interaction_.bind(context);
    redraw_.bind(context);
    updates_.bind(context);
    updates_.onInvalidate([this] {
        invalidate();
    });
    updates_.onUpdate([this](auto tick) {
        const bool changed = synchronize(!frozen(settings));
        const bool moving = !frozen(settings) && model_.advance(tick.delta, 0, settings.at("speed").number);
        if (schedule(settings) != CD_OK) {
            updates_.stop();
        }
        return cdsdk::UpdateStep{changed || moving, true};
    });
    synchronize(false);
    if (schedule(settings) != CD_OK) {
        throw std::runtime_error("Cannot schedule Steamworks spring clock");
    }
}

SpringInstance::~SpringInstance() {
    updates_.stop();
    cancel();
}

bool SpringInstance::frozen(const cdsdk::Settings& values) const {
    return hidden_ || !values.at("running").integer || values.at("reduced_motion").integer;
}

bool SpringInstance::synchronize(bool animate) {
    const auto local = cdsdk::Environment::localTime(environment_.snapshot());
    return model_.setTime(local.tm_hour, local.tm_min, local.tm_sec, settings.at("twelve_hour").integer != 0, animate);
}

CdResult SpringInstance::schedule(const cdsdk::Settings& values) {
    if (hidden_) {
        updates_.stop();
        return CD_OK;
    }
    return updates_.start(!frozen(values) && model_.active() ? 33u : 1000u, environment_.snapshot().monotonic_ms);
}

CdResult SpringInstance::render(const CdFrame& frame) {
    if (!canvas_.valid(frame)) {
        return CD_ERROR_ARGUMENT;
    }
    updates_.beginFrame();
    redraw_.beginFrame();
    canvas_.render(frame, safe_, [&](auto& canvas, CdRect safe) {
        view_ = springView(safe, model_);
        canvas.saved([&] {
            canvas.translate(view_.origin.x - safe.x, view_.origin.y - safe.y);
            canvas.scale(view_.scale, view_.scale);
            renderer_.draw(canvas, model_, settings, view_.scale, frame.device_pixel_ratio);
        });
        std::vector<cybercanvas::HitRegion> regions;
        for (size_t i = 0; i < model_.pieces().size(); ++i) {
            const auto& piece = model_.pieces()[i];
            const auto state = model_.state(i);
            auto shape = cybercanvas::HitShape::rectangle(
                {-piece.half.width * 100, -piece.half.height * 100, piece.half.width * 200, piece.half.height * 200});
            const auto matrix =
                D2D1::Matrix3x2F::Rotation(float(state.angle * 180 / std::numbers::pi)) *
                D2D1::Matrix3x2F::Translation(float(state.position.x * 100), float(state.position.y * 100));
            regions.push_back({uint32_t(i + 1), shape.transformed(matrix), cybercanvas::PointerBehavior::Drag});
        }
        interaction_.regions(std::move(regions));
        laidOut_ = true;
    });
    return CD_OK;
}

void SpringInstance::cancel() {
    interaction_.cancel();
    model_.cancelDrag();
}

void SpringInstance::input(const CdInput& event) {
    if (frozen(settings) || !laidOut_ || !std::isfinite(event.x) || !std::isfinite(event.y)) {
        return;
    }
    auto local = event;
    local.x = (event.x - view_.origin.x) / view_.scale;
    local.y = (event.y - view_.origin.y) / view_.scale;
    const auto result = interaction_.input(local);
    const Point target{local.x / 100, local.y / 100};
    if (result.drag == cybercanvas::DragPhase::Begin) {
        model_.beginDrag(result.target - 1, target);
    } else if (result.drag == cybercanvas::DragPhase::Update) {
        model_.drag(target);
    } else if (result.drag == cybercanvas::DragPhase::End || result.drag == cybercanvas::DragPhase::Cancel) {
        model_.cancelDrag();
    }
    schedule(settings);
}

void SpringInstance::layoutChanged(const CdLayout& layout) {
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

void SpringInstance::setPaused(bool paused) {
    renderer_.pause(paused);
    hidden_ = paused;
    cancel();
    updates_.pause(paused);
    redraw_.pause(paused);
    if (!paused) {
        synchronize(false);
        invalidate();
    }
    schedule(settings);
}

CdResult SpringInstance::applySettings(const CdSettingValue* values, uint32_t count) {
    auto next = settings;
    const auto result = next.apply(values, count);
    if (result != CD_OK) {
        return result;
    }
    const auto scheduled = schedule(next);
    if (scheduled != CD_OK) {
        return scheduled;
    }
    cancel();
    settings = std::move(next);
    synchronize(!frozen(settings));
    invalidate();
    return CD_OK;
}

void SpringInstance::invalidate() {
    redraw_.invalidate();
}

bool SpringInstance::save(const std::filesystem::path& path, const CdFrame& frame) {
    return canvas_.save(path.wstring(), frame);
}

const toys::Model& SpringInstance::model() const {
    return model_;
}

bool SpringInstance::ready() const {
    return renderer_.ready();
}

uint64_t SpringInstance::cacheBuilds() const {
    return renderer_.cacheBuilds();
}
}  // namespace steamdesk
