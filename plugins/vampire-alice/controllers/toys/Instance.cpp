#include <ctime>
#include <numbers>

#include "plugins/vampire-alice/controllers/toys/Toys.h"

namespace alice_toys {
std::array<uint64_t, 4> Instance::physicsCounters() const {
    const auto value = model_.stats();
    return {value.steps, value.advanceNanoseconds, value.awakeBodies, value.maximumAdvanceNanoseconds};
}

Instance::Instance(const CdCreateContext& context, Kind kind, bool mechanical)
    : settings{{"speed", CD_SETTING_NUMBER, 0, .75, {}, .25, 2},
               {"wind", CD_SETTING_NUMBER, 0, .35, {}, 0, 1},
               {"show_background", CD_SETTING_TOGGLE, 1},
               {"running", CD_SETTING_TOGGLE, 1},
               {"reduced_motion", CD_SETTING_TOGGLE, 0},
               {"twelve_hour", CD_SETTING_TOGGLE, 0},
               {"palette", CD_SETTING_CHOICE, 0, 0, "pastel", 0, 0, {"pastel", "ocean"}}},
      model_(kind, mechanical && context.plugin_id && std::string_view(context.plugin_id).ends_with(".1x4"),
             mechanical) {
    compact_ = mechanical && context.plugin_id && std::string_view(context.plugin_id).ends_with(".1x4");
    canvas_.bindGpu(context);
    environment_.bind(context);
    model_.setWindSeed(uint32_t(environment_.snapshot().monotonic_ms ^ reinterpret_cast<uintptr_t>(this)));
    if (mechanical) {
        mechanical_ = std::make_unique<MechanicalRenderer>();
        mechanical_->bind(context, [this] {
            invalidate();
        });
        if (kind == Kind::Chime) {
            const CdSettingValue breeze{sizeof(CdSettingValue), CD_SETTING_NUMBER, "wind", 0, .55};
            settings.apply(&breeze, 1);
        }
    }
    interaction_.bind(context);
    redraw_.bind(context);
    updates_.bind(context);
    updates_.onInvalidate([this] {
        invalidate();
    });
    updates_.onUpdate([this](cybercanvas::AnimationTick tick) {
        const auto before = model_.digits();
        synchronizeTime();
        const double wind = model_.kind() == Kind::Chime ? settings.at("wind").number : 0;
        const bool moving = !frozen() && model_.advance(tick.delta, wind, settings.at("speed").number);
        schedule();
        return cdsdk::UpdateStep{moving || before != model_.digits(), true};
    });
    synchronizeTime();
    schedule();
}

Instance::~Instance() {
    updates_.stop();
    cancel();
}

bool Instance::frozen() const {
    return hidden_ || !settings.at("running").integer || settings.at("reduced_motion").integer;
}

void Instance::invalidate() {
    redraw_.invalidate();
}

void Instance::synchronizeTime() {
    if (model_.kind() != Kind::Clock) {
        return;
    }
    const auto environment = environment_.snapshot();
    const auto local = cdsdk::Environment::localTime(environment);
    model_.setTime(local.tm_hour, local.tm_min, local.tm_sec, settings.at("twelve_hour").integer != 0, !frozen());
}

void Instance::schedule() {
    if (hidden_) {
        updates_.stop();
        return;
    }
    const bool dynamic =
        !frozen() && (model_.active() || (model_.kind() == Kind::Chime && settings.at("wind").number > 0));
    if (dynamic) {
        updates_.start(33);
    } else if (model_.kind() == Kind::Clock) {
        updates_.start(1000);
    } else {
        updates_.stop();
    }
}

CdResult Instance::render(const CdFrame& frame) {
    if (!canvas_.valid(frame)) {
        return CD_ERROR_ARGUMENT;
    }
    updates_.beginFrame();
    redraw_.beginFrame();
    synchronizeTime();
    const auto environment = environment_.snapshot();
    const bool dark = environment.theme == CD_ENV_THEME_UNKNOWN ? dark_ : environment.theme != CD_ENV_THEME_LIGHT;
    canvas_.render(frame, safe_, [&](auto& canvas, CdRect safe) {
        const bool compact = mechanical_ ? compact_ : model_.kind() == Kind::Clock && safe.width / safe.height > 3;
        if (compact_ != compact) {
            cancel();
            compact_ = compact;
            model_.reset(compact);
            synchronizeTime();
        }
        const auto area = model_.size();
        const auto view = mechanical_->view(safe, area);
        scale_ = view.scale;
        origin_ = view.origin;
        if (!mechanical_ && settings.at("show_background").integer) {
            canvas.box(0, 0, safe.width, safe.height, 12, dark ? 0x24363be8 : 0xfaf6ede8);
        }
        canvas.saved([&] {
            canvas.translate(origin_.x - safe.x, origin_.y - safe.y);
            canvas.scale(scale_, scale_);
            const Appearance appearance{dark, false, settings.at("twelve_hour").integer != 0,
                                        settings.at("palette").text};
            if (mechanical_) {
                mechanical_->draw(canvas, model_, appearance, scale_);
            }
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
    schedule();
    return CD_OK;
}

void Instance::cancel() {
    interaction_.cancel();
    model_.cancelDrag();
}

void Instance::input(const CdInput& event) {
    if (frozen() || !laidOut_ || !std::isfinite(event.x) || !std::isfinite(event.y)) {
        return;
    }
    auto local = event;
    local.x = (event.x - origin_.x) / scale_;
    local.y = (event.y - origin_.y) / scale_;
    const auto result = interaction_.input(local);
    const Point target{local.x / 100, local.y / 100};
    if (result.drag == cybercanvas::DragPhase::Begin) {
        model_.beginDrag(result.target - 1, target);
    } else if (result.drag == cybercanvas::DragPhase::Update) {
        model_.drag(target);
    } else if (result.drag == cybercanvas::DragPhase::End || result.drag == cybercanvas::DragPhase::Cancel) {
        model_.cancelDrag();
    }
    if (event.kind == CD_INPUT_DOUBLE_CLICK) {
        model_.pluck();
    }
    schedule();
    invalidate();
}

void Instance::layoutChanged(const CdLayout& layout) {
    if (!std::isfinite(layout.logical_width) || !std::isfinite(layout.logical_height) || layout.logical_width <= 0 ||
        layout.logical_height <= 0) {
        return;
    }
    cancel();
    laidOut_ = false;
    safe_ = layout.safe_content;
    if (!std::isfinite(safe_.x) || !std::isfinite(safe_.y) || !std::isfinite(safe_.width) ||
        !std::isfinite(safe_.height) || safe_.width <= 0 || safe_.height <= 0) {
        safe_ = {0, 0, layout.logical_width, layout.logical_height};
    }
    dark_ = layout.text_color.r * .2126 + layout.text_color.g * .7152 + layout.text_color.b * .0722 > 140;
}

void Instance::setPaused(bool paused) {
    hidden_ = paused;
    if (mechanical_) {
        mechanical_->pause(paused);
    }
    cancel();
    updates_.pause(paused);
    redraw_.pause(paused);
    if (!paused) {
        synchronizeTime();
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
    cancel();
    settings = std::move(next);
    synchronizeTime();
    schedule();
    invalidate();
    return CD_OK;
}
}  // namespace alice_toys
