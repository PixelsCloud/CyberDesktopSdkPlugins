#include "plugins/pixel-desk/SpringClock.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <tuple>

#include "plugins/mechanical-pixels/Pixels.h"
#include "plugins/pixel-desk/PixelDigits.h"
#include "plugins/pixel-desk/PixelImages.h"
#include "plugins/pixel-desk/PixelLayout.h"
#include "plugins/pixel-desk/PixelParts.h"
#include "plugins/pixel-desk/PixelToysModel.h"

namespace pixeldesk {
using cybercanvas::Canvas;
using cybercanvas::Point;
using Microsoft::WRL::ComPtr;

const SpringSpec& springSpec(std::string_view id) {
    for (const auto& spec : kSpringCatalog) {
        if (id == spec.id) {
            return spec;
        }
    }
    throw std::invalid_argument("Unknown Pixel Desk spring clock");
}

cdsdk::Settings springDefaults() {
    return {{{"speed", CD_SETTING_NUMBER, 0, .75, {}, .25, 2},
             {"running", CD_SETTING_TOGGLE, 1},
             {"reduced_motion", CD_SETTING_TOGGLE, 0},
             {"twelve_hour", CD_SETTING_TOGGLE, 0},
             {"text_color", CD_SETTING_COLOR, kInk},
             {"accent_color", CD_SETTING_COLOR, kMint}}};
}

SpringView springView(CdRect safe, const toys::Model& model) {
    const auto size = model.size();
    const bool compact = size.height < 150;
    const cybercanvas::Rect world = compact ? cybercanvas::Rect{-50, -10, 460, size.height + 20}
                                            : cybercanvas::Rect{-50, -10, 460, size.height + 30};
    const auto bounds = fitBody(safe.width, safe.height, world.width, world.height);
    const double scale = bounds.width / world.width;
    return {{safe.x + bounds.x - world.x * scale, safe.y + bounds.y - world.y * scale}, scale};
}

Point springDragTarget(const toys::Model& model, size_t index, Point target, Point localAnchor) {
    const auto state = model.state(index);
    const auto half = model.pieces().at(index).half;
    const auto area = model.size();
    const double c = std::cos(state.angle), s = std::sin(state.angle);
    const Point offset{c * localAnchor.x - s * localAnchor.y, s * localAnchor.x + c * localAnchor.y};
    // Each pendant stays around its own suspension. Pulling the first card to
    // the opposite corner would otherwise crush all six bodies into a sidewall.
    const auto anchor = model.anchor(model.pieces().at(index));
    const double restY = anchor.y + (area.height < 150 ? .18 : .65) + half.height;
    return {std::clamp(target.x - offset.x, std::max(.32, anchor.x - .30),
                       std::min(area.width / 100 - .32, anchor.x + .30)) +
                offset.x,
            std::clamp(target.y - offset.y, restY - .25, std::min(area.height / 100 - half.height - .08, restY + .25)) +
                offset.y};
}

namespace {
void spring(Canvas& canvas, Point from, Point to) {
    const double dx = to.x - from.x, dy = to.y - from.y, length = std::hypot(dx, dy);
    if (length < .01) {
        return;
    }
    auto path = canvas.path();
    path.moveTo(from);
    constexpr unsigned segments = 14;
    for (unsigned i = 1; i < segments; ++i) {
        const double t = double(i) / segments, side = i % 2 ? 3.1 : -3.1;
        path.lineTo({from.x + dx * t + dy / length * side, from.y + dy * t - dx / length * side});
    }
    path.lineTo(to);
    canvas.stroke(path, kInk, {.width = 3.2});
    canvas.stroke(path, 0xf5e7cdff, {.width = 1.25});
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

void card(Canvas& canvas, double height, char digit, uint32_t ink, bool casing = true, double faceCenter = 0) {
    if (casing) {
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
    }
    const double glyphHeight = height < 80 ? 43 : 58;
    const double unitX = 5.3, unitY = glyphHeight / 7;
    const std::string text(1, digit);
    canvas.saved([&] {
        canvas.translate(faceCenter - unitX * 2.5, -glyphHeight / 2);
        canvas.scale(unitX, unitY);
        pixelDigits(canvas, text, {0, 0, 5, 7}, ink);
    });
}

void part(Canvas& layer, const cybercanvas::ImageData& image, cybercanvas::Size source, cybercanvas::Rect crop,
          cybercanvas::Rect area) {
    const double scale = std::min(area.width / crop.width, area.height / crop.height);
    const double x = area.x + (area.width - crop.width * scale) / 2;
    const double y = area.y + (area.height - crop.height * scale) / 2;
    layer.clipped({x, y, crop.width * scale, crop.height * scale}, [&] {
        layer.image(
            image, {x - crop.x * scale, y - crop.y * scale, source.width * scale, source.height * scale},
            {.fit = cybercanvas::ImageFit::Stretch, .clip = false, .sampling = cybercanvas::ImageSampling::Nearest});
    });
}
}  // namespace

struct SpringRenderer::Impl {
    using Key = std::tuple<bool, double, uint32_t, uint32_t, const cybercanvas::ImageData*>;
    cdsdk::Images images;
    std::filesystem::path path;
    std::shared_ptr<const cybercanvas::ImageData> body;
    const SpringSpec* spec = nullptr;
    std::optional<Key> key;
    ComPtr<ID2D1RenderTarget> owner;
    cybercanvas::SurfaceImage beam;
    std::array<cybercanvas::SurfaceImage, 10> cards;
    uint64_t builds = 0;

    void prepare(Canvas& canvas, const toys::Model& model, const cdsdk::Settings& settings, double density) {
        const bool compact = model.size().height < 150;
        const auto ink = uint32_t(settings.at("text_color").integer),
                   accent = uint32_t(settings.at("accent_color").integer);
        const double beamWidth = compact ? 460 : 360;
        const double imageScale = std::max(beamWidth / spec->beamPart.width, 44 / spec->cardPart.width);
        pixeldesk::requestPixelImage(
            images, "body", path, 1,
            uint32_t(std::clamp(std::ceil(spec->source.width * imageScale * density / 64) * 64, 64., 4096.)),
            canvas.devicePixelRatio());
        images.poll();
        body = images.get("body").image;
        const Key next{compact, density, ink, accent, body.get()};
        if (owner == canvas.target && key == next) {
            return;
        }
        cybercanvas::OffscreenSurface rail;
        canvas.renderLayer(rail, {uint32_t(std::ceil(beamWidth * density)), uint32_t(std::ceil(30 * density)), 1},
                           [&](Canvas& layer) {
                               layer.scale(density, density);
                               if (body) {
                                   part(layer, *body, spec->source, spec->beamPart, {0, 0, beamWidth, 30});
                                   layer.box(24, 6, beamWidth - 48, 10, 0, accent);
                               } else {
                                   pixeldesk::beam(layer, accent);
                               }
                           });
        beam = rail.snapshot();
        const double height = compact ? 62 : 110;
        for (unsigned i = 0; i < cards.size(); ++i) {
            cybercanvas::OffscreenSurface tile;
            canvas.renderLayer(
                tile, {uint32_t(std::ceil(48 * density)), uint32_t(std::ceil((height + 14) * density)), 1},
                [&](Canvas& layer) {
                    layer.scale(density, density);
                    layer.translate(24, height / 2 + 8);
                    if (body) {
                        part(layer, *body, spec->source, spec->cardPart, {-22, -height / 2 - 8, 44, height + 14});
                    }
                    // The right bevel belongs to the casing, not the
                    // cream paper face. Register to the face aperture.
                    const double partScale = std::min(44 / spec->cardPart.width, (height + 14) / spec->cardPart.height);
                    const double center =
                        (spec->cardFaceCenterX - spec->cardPart.x - spec->cardPart.width / 2) * partScale;
                    card(layer, height, char('0' + i), ink, !body, body ? center : 0);
                });
            cards[i] = tile.snapshot();
        }
        owner = canvas.target;
        key = next;
        ++builds;
    }
};

SpringRenderer::SpringRenderer() : impl_(std::make_unique<Impl>()) {}

SpringRenderer::~SpringRenderer() = default;

void SpringRenderer::bind(const CdCreateContext& context, std::function<void()> changed) {
    impl_->images.bind(context);
    impl_->images.onChange(std::move(changed));
    impl_->spec = &springSpec(context.plugin_id);
    impl_->path =
        std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(context.plugin_directory))) /
        "assets/body.png";
}

void SpringRenderer::pause(bool paused) {
    impl_->images.pause(paused);
}

bool SpringRenderer::ready() const {
    return impl_->body && !impl_->images.pending() && impl_->images.get("body").image == impl_->body;
}

void SpringRenderer::draw(Canvas& canvas, const toys::Model& model, const cdsdk::Settings& settings, double scale,
                          double dpi) {
    impl_->prepare(canvas, model, settings, scale * dpi);
    const bool compact = model.size().height < 150;
    canvas.target->DrawBitmap(impl_->beam.Get(), D2D1::RectF(compact ? -50.f : 0.f, 0, compact ? 410.f : 360.f, 30), 1,
                              D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    const auto digits = model.digits();
    const double height = model.size().height < 150 ? 62 : 110;
    for (size_t i = 0; i < model.pieces().size(); ++i) {
        const auto& piece = model.pieces()[i];
        const auto from = model.anchor(piece), to = model.attachment(piece);
        stepped(canvas, {from.x * 100 - 5, 23, 10, 9}, 2, kInk);
        stepped(canvas, {from.x * 100 - 4, 24, 8, 7}, 2, kMint);
        pixelBolt(canvas, from.x * 100, 27, 2);
        spring(canvas, {from.x * 100, from.y * 100}, {to.x * 100, to.y * 100});
        const auto body = model.state(i);
        canvas.saved([&] {
            canvas.translate(body.position.x * 100, body.position.y * 100);
            canvas.rotate(body.angle * 180 / std::numbers::pi);
            const unsigned digit =
                i < digits.size() && digits[i] >= '0' && digits[i] <= '9' ? unsigned(digits[i] - '0') : 0;
            canvas.target->DrawBitmap(impl_->cards[digit].Get(),
                                      D2D1::RectF(-24, float(-height / 2 - 8), 24, float(height / 2 + 6)), 1,
                                      D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
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
    : settings(springDefaults()),
      model_(toys::Kind::Clock, springSpec(context.plugin_id).compact, true, pixelToysOptions()) {
    canvas_.bindGpu(context);
    environment_.bind(context);
    interaction_.bind(context);
    redraw_.bind(context);
    renderer_.bind(context, [this] {
        invalidate();
    });
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
        throw std::runtime_error("Cannot schedule Pixel Desk spring clock");
    }
}

SpringInstance::~SpringInstance() {
    updates_.stop();
    renderer_.pause(true);
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
    dragIndex_.reset();
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
        const auto index = size_t(result.target - 1);
        const auto body = model_.state(index);
        const double c = std::cos(body.angle), s = std::sin(body.angle);
        const Point offset{target.x - body.position.x, target.y - body.position.y};
        dragAnchor_ = {c * offset.x + s * offset.y, -s * offset.x + c * offset.y};
        dragIndex_ = index;
        model_.beginDrag(index, target);
    } else if (result.drag == cybercanvas::DragPhase::Update && dragIndex_) {
        model_.drag(springDragTarget(model_, *dragIndex_, target, dragAnchor_));
    } else if (result.drag == cybercanvas::DragPhase::End || result.drag == cybercanvas::DragPhase::Cancel) {
        model_.cancelDrag();
        dragIndex_.reset();
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
    hidden_ = paused;
    cancel();
    updates_.pause(paused);
    redraw_.pause(paused);
    renderer_.pause(paused);
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

uint64_t SpringInstance::cacheBuilds() const {
    return renderer_.cacheBuilds();
}

bool SpringInstance::ready() const {
    return renderer_.ready();
}

}  // namespace pixeldesk
