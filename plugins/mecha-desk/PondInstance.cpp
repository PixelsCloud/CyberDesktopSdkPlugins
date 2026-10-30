#include <sdk/Language.h>
#include <sdk/services/Interaction.h>

#include <cmath>
#include <random>

#include "plugins/mecha-desk/Pond.h"

namespace aeris_pond {
PondInstance::PondInstance(const CdCreateContext& context)
    : settings(pondDefaults(pondSpec(context.plugin_id))),
      spec_(pondSpec(context.plugin_id)),
      face_(context, spec_),
      startupSeed_(std::random_device{}()) {
    environment_.bind(context);
    timer_.bind(context);
    redraw_.bind(context);
    face_.onChange([this] {
        invalidate();
    });
    lastTick_ = environment_.snapshot().monotonic_ms;
    scene_.setBoundary([this](koiplugin::Vec p, double clearance) {
        return face_.contains(p, clearance);
    });
    configure();
    if (schedule(settings) != CD_OK) {
        throw std::runtime_error("Cannot schedule AERIS POLAR pond");
    }
}

PondInstance::~PondInstance() {
    timer_.stop();
    face_.onChange({});
}

void PondInstance::configure() {
    const auto water = face_.waterSize();
    scene_.configure(
        water.width, water.height, int(settings.at("fish_count").integer),
        startupSeed_ ^ (uint32_t(settings.at("seed").integer) * 0x9e3779b9u),
        koiplugin::fishLengthForGrid(water.width, water.height, int(spec_.plate.rows), int(spec_.plate.columns)));
    scene_.setMouseBehavior(settings.at("mouse_behavior").text == "follow" ? koiplugin::MouseBehavior::Follow
                                                                           : koiplugin::MouseBehavior::Flee);
}

CdResult PondInstance::schedule(const cdsdk::Settings& next) {
    if (hidden_ || !next.at("animation").integer) {
        timer_.stop();
        return CD_OK;
    }
    const unsigned fps = unsigned(std::stoi(next.at("frame_rate").text));
    return timer_.replace((1000 + fps - 1) / fps, tick, this);
}

double PondInstance::targetNight() const {
    const auto mode = settings.at("lighting").text;
    if (mode == "day") {
        return 0;
    }
    if (mode == "night") {
        return 1;
    }
    const auto theme = environment_.snapshot().theme;
    return theme == CD_ENV_THEME_UNKNOWN ? themeNight_ : theme == CD_ENV_THEME_DARK;
}

void CD_CALL PondInstance::tick(void* pointer, uint64_t) noexcept {
    auto& self = *static_cast<PondInstance*>(pointer);
    try {
        const auto now = self.environment_.snapshot().monotonic_ms;
        const double delta = now >= self.lastTick_ ? std::min(.15, (now - self.lastTick_) / 1000.) : 0;
        self.lastTick_ = now;
        self.scene_.advance(delta, self.settings.at("swim_speed").number);
        self.motion_ += delta * self.settings.at("motion_speed").number;
        self.night_ += (self.targetNight() - self.night_) * std::min(1., delta * 3);
        self.invalidate();
    } catch (...) {
        self.timer_.stop();
    }
}

void PondInstance::invalidate() {
    redraw_.invalidate();
}

bool PondInstance::ready() const {
    return face_.ready();
}

const koiplugin::Scene& PondInstance::scene() const {
    return scene_;
}

CdResult PondInstance::render(const CdFrame& frame) {
    redraw_.beginFrame();
    const cdsdk::LanguageScope language(settings.at("language").text);
    if (!settings.at("animation").integer) {
        night_ = targetNight();
    }
    const koiplugin::Appearance appearance{
        night_, settings.at("shimmer").integer / 100., 0,       true, true, uint32_t(settings.at("color").integer),
        true,   settings.at("blessings").integer != 0, motion_, true};
    return face_.render(scene_, appearance, frame, safe_) ? CD_OK : CD_ERROR_ARGUMENT;
}

void PondInstance::input(const CdInput& event) {
    if (hidden_ || !settings.at("animation").integer || !std::isfinite(event.x) || !std::isfinite(event.y)) {
        return;
    }
    if (event.kind == CD_INPUT_LEAVE || event.kind == CD_INPUT_CANCEL) {
        pressed_ = false;
        scene_.clearPointer();
        return;
    }
    const auto point = face_.hit(safe_, event.x, event.y);
    if (!point) {
        pressed_ = false;
        scene_.clearPointer();
        return;
    }
    if (std::hypot(event.x - press_.x, event.y - press_.y) > 5) {
        pressed_ = false;
    }
    if (event.kind == CD_INPUT_MOVE && !event.buttons && !pressed_) {
        scene_.pointer(*point);
    }
    if (event.kind == CD_INPUT_PRESS && (event.buttons & CD_BUTTON_LEFT)) {
        pressed_ = true;
        press_ = {event.x, event.y};
        scene_.clearPointer();
    } else if (event.kind == CD_INPUT_RELEASE) {
        const bool click = pressed_ && !(event.buttons & CD_BUTTON_LEFT);
        pressed_ = false;
        if (click) {
            scene_.click(*point);
            invalidate();
        }
    }
}

void PondInstance::layoutChanged(const CdLayout& layout) {
    const auto safe = layout.safe_content;
    if (std::isfinite(safe.width) && std::isfinite(safe.height) && safe.width > 12 && safe.height > 12 && safe.x >= 0 &&
        safe.y >= 0 && safe.x + safe.width <= layout.logical_width && safe.y + safe.height <= layout.logical_height) {
        safe_ = safe;
    } else if (layout.logical_width > 12 && layout.logical_height > 12) {
        safe_ = {0, 0, layout.logical_width, layout.logical_height};
    }
    const auto c = layout.text_color;
    themeNight_ = c.r * .2126 + c.g * .7152 + c.b * .0722 > 140;
    if (!settings.at("animation").integer || hidden_) {
        night_ = targetNight();
    }
    pressed_ = false;
    scene_.clearPointer();
}

void PondInstance::setPaused(bool paused) {
    hidden_ = paused;
    pressed_ = false;
    scene_.clearPointer();
    face_.pause(paused);
    redraw_.pause(paused);
    lastTick_ = environment_.snapshot().monotonic_ms;
    schedule(settings);
    if (!paused) {
        invalidate();
    }
}

CdResult PondInstance::applySettings(const CdSettingValue* values, uint32_t count) {
    auto next = settings;
    const auto result = next.apply(values, count);
    if (result != CD_OK) {
        return result;
    }
    const auto scheduled = schedule(next);
    if (scheduled != CD_OK) {
        return scheduled;
    }
    settings = std::move(next);
    configure();
    lastTick_ = environment_.snapshot().monotonic_ms;
    pressed_ = false;
    scene_.clearPointer();
    if (!settings.at("animation").integer) {
        night_ = targetNight();
    }
    invalidate();
    return CD_OK;
}
}  // namespace aeris_pond
