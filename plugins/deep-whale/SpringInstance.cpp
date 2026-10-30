#include <cmath>
#include <numbers>

#include "plugins/deep-whale/SpringClock.h"

namespace deepwhale {
using cybercanvas::Point;

SpringInstance::SpringInstance(const CdCreateContext& context)
    : settings(springDefaults(springSpec(context.plugin_id).compact)),
      model_(toys::Kind::Clock, springSpec(context.plugin_id).compact, true,
             springLayout(springSpec(context.plugin_id).compact)),
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
        throw std::runtime_error("Cannot schedule Deep Whale spring clock");
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
            const cybercanvas::Rect beam = model_.size().height < 150
                                               ? cybercanvas::Rect{(safe.x + 7 - view_.origin.x) / view_.scale, 0,
                                                                   (safe.width - 14) / view_.scale, 0}
                                               : cybercanvas::Rect{};
            renderer_.draw(canvas, model_, settings, view_.scale, frame.device_pixel_ratio, beam);
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

cdsdk::ImageSlotStats SpringInstance::imageStats() const {
    return renderer_.imageStats();
}

uint64_t SpringInstance::cacheBuilds() const {
    return renderer_.cacheBuilds();
}
}  // namespace deepwhale
