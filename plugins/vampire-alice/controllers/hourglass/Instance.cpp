#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "plugins/vampire-alice/Skin.h"
#include "plugins/vampire-alice/controllers/hourglass/MechanicalHourglass.h"

namespace alice_hourglass {
std::array<uint64_t, 4> Instance::physicsCounters() const {
    const auto value = sand_.stats();
    return {value.steps, value.advanceNanoseconds, value.awakeBodies, value.maximumAdvanceNanoseconds};
}

Instance::Instance(const CdCreateContext& context)
    : settings(defaults(specification(context.plugin_id).columns == 2)),
      spec_(specification(context.plugin_id)),
      host_(*context.host),
      context_(context.host_context),
      renderer_(context) {
    const CdSettingValue sand{sizeof(CdSettingValue), CD_SETTING_COLOR, "sand_color", alice::kRuby};
    settings.apply(&sand, 1);
    environment_.bind(context);
    redraw_.bind(context);
    renderer_.onChange([this] {
        invalidate();
    });
    lastTick_ = environment_.snapshot().monotonic_ms;
    if (subscribe(settings, true, timer_) != CD_OK) {
        throw std::runtime_error("Could not start hourglass timer");
    }
}

Instance::~Instance() {
    timer_.stop();
    renderer_.onChange({});
    renderer_.pause(true);
}

CdResult Instance::subscribe(const cdsdk::Settings& next, bool restarting, cdsdk::Timer& token) {
    token.bind(host_, context_);
    if (paused_ || !next.at("running").integer || (!restarting && model_.phase() == Phase::Finished)) {
        return CD_OK;
    }
    const auto fps = uint32_t(std::stoi(next.at("frame_rate").text));
    return token.replace((1000 + fps - 1) / fps,
                         [](void* pointer, uint64_t now) noexcept {
                             try {
                                 static_cast<Instance*>(pointer)->tick(now);
                             } catch (...) {
                             }
                         },
                         this);
}

void Instance::tick(uint64_t now) {
    if (paused_ || !settings.at("running").integer) {
        return;
    }
    double seconds = now >= lastTick_ ? std::min(.25, (now - lastTick_) / 1000.) : 0;
    lastTick_ = now;
    if (seconds <= 0) {
        return;
    }
    while (seconds > .0000001) {
        const double step = std::min(.05, seconds);
        model_.advance(step, settings.at("repeat").integer != 0);
        if (settings.at("physics").integer) {
            sand_.advance(step, model_);
        }
        seconds -= step;
    }
    if (model_.phase() == Phase::Finished) {
        timer_.stop();
    }
    invalidate();
}

CdResult Instance::render(const CdFrame& frame) {
    redraw_.beginFrame();
    return renderer_.render(spec_.columns == 2, model_, settings.at("physics").integer ? &sand_ : nullptr, settings,
                            frame, safe_)
               ? CD_OK
               : CD_ERROR_ARGUMENT;
}

CdResult Instance::applySettings(const CdSettingValue* values, uint32_t count) {
    auto next = settings;
    const auto result = next.apply(values, count);
    if (result != CD_OK) {
        return result;
    }
    const bool restarting =
        next.at("duration").integer != settings.at("duration").integer ||
        next.at("physics").integer != settings.at("physics").integer ||
        (next.at("repeat").integer && !settings.at("repeat").integer && model_.phase() == Phase::Finished);
    const bool finishing = !restarting && !next.at("repeat").integer && model_.phase() == Phase::Turning;
    const bool resubscribe = restarting || finishing || next.at("running").integer != settings.at("running").integer ||
                             next.at("frame_rate").text != settings.at("frame_rate").text;
    if (resubscribe) {
        cdsdk::Timer replacement;
        const auto subscribed = finishing ? CD_OK : subscribe(next, restarting, replacement);
        if (subscribed != CD_OK) {
            return subscribed;
        }
        timer_ = std::move(replacement);
        lastTick_ = environment_.snapshot().monotonic_ms;
    }
    settings = std::move(next);
    if (restarting) {
        model_.reset(double(settings.at("duration").integer));
        sand_.reset();
    } else if (finishing) {
        model_.advance(0, false);
    }
    pressed_ = false;
    invalidate();
    return CD_OK;
}

void Instance::restart() {
    auto next = settings;
    const CdSettingValue running{sizeof(running), CD_SETTING_TOGGLE, "running", 1};
    if (next.apply(&running, 1) != CD_OK) {
        return;
    }
    cdsdk::Timer replacement;
    if (subscribe(next, true, replacement) != CD_OK) {
        return;
    }
    timer_ = std::move(replacement);
    lastTick_ = environment_.snapshot().monotonic_ms;
    settings = std::move(next);
    model_.reset(double(settings.at("duration").integer));
    sand_.reset();
    invalidate();
}

void Instance::layoutChanged(const CdLayout& layout) {
    if (!std::isfinite(layout.logical_width) || !std::isfinite(layout.logical_height) || layout.logical_width <= 0 ||
        layout.logical_height <= 0) {
        return;
    }
    const auto r = layout.safe_content;
    safe_ = std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.width) && std::isfinite(r.height) && r.x >= 0 &&
                    r.y >= 0 && r.width > 0 && r.height > 0 && r.x + r.width <= layout.logical_width &&
                    r.y + r.height <= layout.logical_height
                ? r
                : CdRect{0, 0, layout.logical_width, layout.logical_height};
    pressed_ = false;
}

void Instance::input(const CdInput& event) {
    if (paused_ || !std::isfinite(event.x) || !std::isfinite(event.y)) {
        return;
    }
    const double x = event.x - safe_.x, y = event.y - safe_.y;
    const bool inside = x >= 0 && y >= 0 && x < safe_.width && y < safe_.height;
    if (event.kind == CD_INPUT_LEAVE || !inside || std::hypot(x - pressX_, y - pressY_) > 5) {
        pressed_ = false;
    }
    if (event.kind == CD_INPUT_PRESS && event.buttons & CD_BUTTON_LEFT && inside) {
        pressed_ = true;
        pressX_ = x;
        pressY_ = y;
    } else if (event.kind == CD_INPUT_RELEASE) {
        const bool click = pressed_ && !(event.buttons & CD_BUTTON_LEFT);
        pressed_ = false;
        if (click) {
            const bool reset = spec_.columns == 2 ? x < safe_.width * .45 : y < safe_.height / 2;
            if (reset || model_.phase() == Phase::Finished) {
                restart();
            } else {
                const CdSettingValue running{sizeof(running), CD_SETTING_TOGGLE, "running",
                                             settings.at("running").integer ? 0 : 1};
                applySettings(&running, 1);
            }
        }
    }
}

void Instance::setPaused(bool paused) {
    if (paused_ == paused) {
        return;
    }
    paused_ = paused;
    pressed_ = false;
    timer_.stop();
    renderer_.pause(paused);
    redraw_.pause(paused);
    lastTick_ = environment_.snapshot().monotonic_ms;
    if (!paused) {
        subscribe(settings, false, timer_);
        invalidate();
    }
}

void Instance::invalidate() {
    redraw_.invalidate();
}

const Model& Instance::model() const {
    return model_;
}
}  // namespace alice_hourglass
