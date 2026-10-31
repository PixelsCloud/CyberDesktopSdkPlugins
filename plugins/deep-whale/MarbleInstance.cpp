#include <cmath>

#include "plugins/deep-whale/MarbleClock.h"

namespace deepwhale {
std::array<uint64_t, 4> MarbleInstance::physicsCounters() const {
    const auto value = model_.stats();
    return {value.steps, value.advanceNanoseconds, value.awakeBodies, value.maximumAdvanceNanoseconds};
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
        throw std::runtime_error("Cannot schedule Deep Whale marble clock");
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
    const cybercanvas::Point target{local.x, local.y};
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

cdsdk::ImageSlotStats MarbleInstance::imageStats() const {
    return renderer_.imageStats();
}

uint64_t MarbleInstance::cacheBuilds() const {
    return renderer_.cacheBuilds();
}
}  // namespace deepwhale
