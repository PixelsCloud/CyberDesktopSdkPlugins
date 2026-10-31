#include <CyberCanvas/PanelLayout.h>

#include <cmath>
#include <string_view>

#include "plugins/mechanical-instruments/PhysicsMaterials.h"
#include "plugins/vampire-alice/controllers/marble/Clock.h"

namespace alice_marble {

std::array<uint64_t, 4> Instance::physicsCounters() const {
    const auto value = model_.stats();
    return {value.steps, value.advanceNanoseconds, value.awakeBodies, value.maximumAdvanceNanoseconds};
}

Instance::Instance(const CdCreateContext& context)
    : settings{{"theme", CD_SETTING_CHOICE, 0, 0, "dark", 0, 0, {"dark", "light", "system"}},
               {"show_background", CD_SETTING_TOGGLE, 1},
               {"running", CD_SETTING_TOGGLE, 1},
               {"reduced_motion", CD_SETTING_TOGGLE, 0}},
      model_(context.plugin_id && std::string_view(context.plugin_id).ends_with(".4x2")),
      renderer_(
          context.plugin_directory && *context.plugin_directory
              ? std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(context.plugin_directory))) /
                    "assets"
              : std::filesystem::path{},
          model_.shape().width < 1000) {
    canvas_.bindGpu(context);
    renderer_.bind(context);
    environment_.bind(context);
    interaction_.bind(context);
    redraw_.bind(context);
    updates_.bind(context);
    updates_.onInvalidate([this] {
        invalidate();
    });
    updates_.onUpdate([this](cybercanvas::AnimationTick tick) {
        bool changed = renderer_.pollAssets();
        changed |= !frozen() && model_.advance(tick.delta);
        changed |= synchronize(!frozen());
        if (!model_.dragging() && interaction_.router.captured()) {
            interaction_.cancel();
        }
        schedule();
        return cdsdk::UpdateStep{changed, true};
    });
    synchronize(false);
    schedule();
}

Instance::~Instance() {
    updates_.stop();
    cancel();
}

bool Instance::frozen() const {
    return hidden_ || !settings.at("running").integer || settings.at("reduced_motion").integer;
}

bool Instance::synchronize(bool animate) {
    const auto environment = environment_.snapshot();
    const auto second = environment.unix_ms / 1000;
    if (lastSecond_ && second != *lastSecond_ && second != *lastSecond_ + 1) {
        model_.quiet();
        animate = false;
    }
    lastSecond_ = second;
    const auto time = cdsdk::Environment::localTime(environment);
    return model_.setTime(time.tm_hour, time.tm_min, time.tm_sec, animate);
}

void Instance::schedule() {
    if (hidden_) {
        updates_.stop();
        return;
    }
    const auto now = environment_.snapshot();
    const auto interval =
        renderer_.pendingAssets() || (!frozen() && model_.active()) ? 33u : uint32_t(1000 - now.unix_ms % 1000);
    updates_.start(interval, now.monotonic_ms);
}

void Instance::arrange() {
    const auto view = renderer_.view(safe_, {model_.shape().width, kHeight});
    origin_ = view.origin;
    scale_ = view.scale;
}

CdResult Instance::render(const CdFrame& frame) {
    if (!canvas_.valid(frame)) {
        return CD_ERROR_ARGUMENT;
    }
    updates_.beginFrame();
    redraw_.beginFrame();
    const auto environment = environment_.snapshot();
    const auto& theme = settings.at("theme").text;
    const bool light = theme == "light" || (theme == "system" && environment.theme == CD_ENV_THEME_LIGHT);
    canvas_.render(frame, safe_, [&](auto& canvas, CdRect safe) {
        safe_ = safe;
        arrange();
        canvas.saved([&] {
            canvas.translate(origin_.x - safe.x, origin_.y - safe.y);
            canvas.scale(scale_, scale_);
            renderer_.draw(canvas, model_,
                           {light, settings.at("show_background").integer != 0,
                            std::string_view(environment.locale).starts_with("en")},
                           scale_);
        });
        std::vector<cybercanvas::HitRegion> regions;
        if (!frozen()) {
            regions.push_back(
                {1, cybercanvas::HitShape::rectangle(renderer_.control("feeder", {model_.shape().width, kHeight}))});
            regions.push_back(
                {2, cybercanvas::HitShape::rectangle(renderer_.control("drain", {model_.shape().width, kHeight}))});
            if (interaction_.streaming()) {
                for (const auto& marble : model_.marbles()) {
                    const auto state = model_.state(marble);
                    regions.push_back(
                        {marble.token + 100,
                         cybercanvas::HitShape::ellipse({state.position.x * kPixelsPerMeter - 13,
                                                         state.position.y * kPixelsPerMeter - 13, 26, 26}),
                         cybercanvas::PointerBehavior::Drag, 1});
                }
            }
        }
        const auto result = interaction_.regions(std::move(regions));
        if (result.drag == cybercanvas::DragPhase::Cancel) {
            model_.cancelDrag();
        }
        arranged_ = true;
    });
    if (renderer_.pendingAssets()) {
        schedule();
    }
    return CD_OK;
}

void Instance::input(const CdInput& input) {
    if (input.kind == CD_INPUT_CANCEL) {
        cancel();
        return;
    }
    if (frozen() || !arranged_ || !std::isfinite(input.x) || !std::isfinite(input.y)) {
        return;
    }
    auto local = input;
    local.x = (input.x - origin_.x) / scale_;
    local.y = (input.y - origin_.y) / scale_;
    const auto result = interaction_.input(local);
    const Point point{local.x, local.y};
    if (result.drag == cybercanvas::DragPhase::Begin) {
        if (!model_.beginDrag(result.target - 100, point)) {
            interaction_.cancel();
        }
    } else if (result.drag == cybercanvas::DragPhase::Update) {
        model_.drag(point);
    } else if (result.drag == cybercanvas::DragPhase::End || result.drag == cybercanvas::DragPhase::Cancel) {
        model_.cancelDrag();
    }
    if (result.clicked == 1) {
        model_.release();
    } else if (result.clicked == 2 || (input.kind == CD_INPUT_DOUBLE_CLICK &&
                                       std::hypot(point.x - model_.shape().pivot, point.y - kFloorY) < 30)) {
        cancel();
        model_.drain();
    }
    schedule();
    invalidate();
}

void Instance::cancel() {
    interaction_.cancel();
    model_.cancelDrag();
}

void Instance::layoutChanged(const CdLayout& layout) {
    if (!std::isfinite(layout.logical_width) || !std::isfinite(layout.logical_height) || layout.logical_width <= 0 ||
        layout.logical_height <= 0) {
        return;
    }
    cancel();
    arranged_ = false;
    const auto safe = layout.safe_content;
    safe_ = std::isfinite(safe.x) && std::isfinite(safe.y) && std::isfinite(safe.width) && std::isfinite(safe.height) &&
                    safe.width > 0 && safe.height > 0
                ? safe
                : CdRect{0, 0, layout.logical_width, layout.logical_height};
    arrange();
    invalidate();
}

void Instance::setPaused(bool paused) {
    hidden_ = paused;
    renderer_.pauseAssets(paused);
    cancel();
    updates_.pause(paused);
    redraw_.pause(paused);
    model_.quiet();
    if (!paused) {
        synchronize(false);
        invalidate();
    }
    schedule();
}

CdResult Instance::applySettings(const CdSettingValue* values, uint32_t count) {
    auto next = settings;
    const auto result = next.apply(values, count);
    if (result != CD_OK) {
        return result;
    }
    const bool motionChanged = next.at("running").integer != settings.at("running").integer ||
                               next.at("reduced_motion").integer != settings.at("reduced_motion").integer;
    cancel();
    settings = std::move(next);
    if (motionChanged) {
        model_.quiet();
        synchronize(false);
    }
    schedule();
    invalidate();
    return CD_OK;
}

void Instance::invalidate() {
    redraw_.invalidate();
}
}  // namespace alice_marble
