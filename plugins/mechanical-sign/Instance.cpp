#include <algorithm>
#include <cmath>

#include "plugins/mechanical-sign/Sign.h"

namespace mechanicalsign {
Instance::Instance(const CdCreateContext& context)
    : settings{{"text", CD_SETTING_TEXT, 0, 0, "CyberDesktop", 0, 512},
               {"interval", CD_SETTING_INTEGER, 5, 0, {}, 0, 60},
               {"speed", CD_SETTING_NUMBER, 0, 1, {}, .35, 2},
               {"animation", CD_SETTING_TOGGLE, 1},
               {"reduced_motion", CD_SETTING_TOGGLE, 0},
               {"background", CD_SETTING_TOGGLE, 1}},
      renderer_(std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(context.plugin_directory))) /
                "assets") {
    canvas_.bindGpu(context);
    environment_.bind(context);
    interaction_.bind(context);
    redraw_.bind(context);
    updates_.bind(context);
    wake_.bind(context);
    updates_.onInvalidate([this] {
        invalidate();
    });
    updates_.onUpdate([this](cybercanvas::AnimationTick tick) {
        bool changed = renderer_.poll();
        if (enabled(settings)) {
            changed |= model_.advance(tick.delta * settings.at("speed").number);
        }
        schedule(model_, settings);
        return cdsdk::UpdateStep{changed, renderer_.pending() || (enabled(settings) && model_.active())};
    });
    if (schedule(model_, settings) != CD_OK) {
        throw std::runtime_error("Cannot schedule mechanical sign");
    }
}

Instance::~Instance() {
    wake_.stop();
    updates_.stop();
    interaction_.cancel();
}

bool Instance::enabled(const cdsdk::Settings& value) const {
    return !hidden_ && value.at("animation").integer && !value.at("reduced_motion").integer;
}

CdResult Instance::schedule(const Model& model, const cdsdk::Settings& value) {
    if (hidden_) {
        wake_.stop();
        updates_.stop();
        return CD_OK;
    }
    if (renderer_.pending() || (enabled(value) && model.active())) {
        const auto result = updates_.start(33, environment_.snapshot().monotonic_ms);
        if (result == CD_OK) {
            wake_.stop();
        }
        return result;
    }
    const auto interval = enabled(value) ? uint32_t(value.at("interval").integer * 1000) : 0;
    const auto result = wake_.replace(interval, wake, this);
    if (result == CD_OK) {
        updates_.stop();
    }
    return result;
}

void CD_CALL Instance::wake(void* user, uint64_t) noexcept {
    auto& self = *static_cast<Instance*>(user);
    self.wake_.stop();
    try {
        self.trigger();
    } catch (...) {
        self.updates_.stop();
    }
}

void Instance::trigger() {
    if (hidden_) {
        return;
    }
    model_.request();
    if (!enabled(settings)) {
        model_.finish();
    }
    schedule(model_, settings);
    invalidate();
}

void Instance::arrange() {
    const double margin = std::min(safe_.width, safe_.height) * .045;
    scale_ = std::min((safe_.width - 2 * margin) / 2107, (safe_.height - 2 * margin) / 459);
    origin_ = {safe_.x + (safe_.width - 2107 * scale_) / 2 - 31 * scale_,
               safe_.y + (safe_.height - 459 * scale_) / 2 - 138 * scale_};
}

CdResult Instance::render(const CdFrame& frame) {
    if (!canvas_.valid(frame)) {
        return CD_ERROR_ARGUMENT;
    }
    redraw_.beginFrame();
    updates_.beginFrame();
    canvas_.render(frame, safe_, [&](auto& canvas, CdRect safe) {
        safe_ = safe;
        if (!layoutKnown_) {
            CdLayout fallback{};
            fallback.logical_width = safe.width;
            fallback.logical_height = safe.height;
            format_ = formatFor(fallback);
        }
        arrange();
        canvas.saved([&] {
            canvas.translate(origin_.x - safe.x, origin_.y - safe.y);
            canvas.scale(scale_, scale_);
            renderer_.draw(canvas, model_, settings.at("background").integer != 0, scale_, format_,
                           safe.width / safe.height);
        });
        if (interaction_
                .regions(
                    !hidden_
                        ? std::vector<
                              cybercanvas::HitRegion>{{1, cybercanvas::HitShape::rectangle(
                                                              {0, 0, geometry(format_, safe.width / safe.height).width,
                                                               geometry(format_, safe.width / safe.height).height})}}
                        : std::vector<cybercanvas::HitRegion>{})
                .drag == cybercanvas::DragPhase::Cancel) {
            interaction_.cancel();
        }
        arranged_ = true;
    });
    if (renderer_.pending()) {
        schedule(model_, settings);
    }
    return CD_OK;
}

CdResult Instance::applySettings(const CdSettingValue* values, uint32_t count) {
    auto next = settings;
    const auto result = next.apply(values, count);
    if (result != CD_OK) {
        return result;
    }
    auto model = model_;
    try {
        if (next.at("text").text != settings.at("text").text) {
            model.text(pages(next.at("text").text), enabled(next));
        }
    } catch (const std::invalid_argument&) {
        return CD_ERROR_ARGUMENT;
    }
    if (!next.at("animation").integer || next.at("reduced_motion").integer) {
        model.finish();
    }
    const auto scheduled = schedule(model, next);
    if (scheduled != CD_OK) {
        return scheduled;
    }
    settings = std::move(next);
    model_ = std::move(model);
    interaction_.cancel();
    invalidate();
    return CD_OK;
}

void Instance::input(const CdInput& event) {
    if (event.kind == CD_INPUT_CANCEL) {
        interaction_.cancel();
        return;
    }
    if (!arranged_ || hidden_ || !std::isfinite(event.x) || !std::isfinite(event.y)) {
        return;
    }
    auto local = event;
    local.x = (event.x - origin_.x) / scale_;
    local.y = (event.y - origin_.y) / scale_;
    if (interaction_.input(local).clicked == 1) {
        trigger();
    }
}

void Instance::layoutChanged(const CdLayout& layout) {
    if (!std::isfinite(layout.logical_width) || !std::isfinite(layout.logical_height) || layout.logical_width <= 0 ||
        layout.logical_height <= 0) {
        return;
    }
    const auto area = layout.safe_content;
    format_ = formatFor(layout);
    layoutKnown_ = true;
    safe_ = std::isfinite(area.x) && std::isfinite(area.y) && std::isfinite(area.width) && std::isfinite(area.height) &&
                    area.width > 0 && area.height > 0
                ? area
                : CdRect{0, 0, layout.logical_width, layout.logical_height};
    interaction_.cancel();
    arranged_ = false;
    arrange();
    invalidate();
}

void Instance::setPaused(bool paused) {
    hidden_ = paused;
    wake_.stop();
    interaction_.cancel();
    renderer_.pause(paused);
    updates_.pause(paused);
    redraw_.pause(paused);
    if (!paused) {
        schedule(model_, settings);
        invalidate();
    }
}

void Instance::invalidate() {
    redraw_.invalidate();
}
}  // namespace mechanicalsign
