#include <sdk/WindowsFonts.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

#include "plugins/vampire-alice/Skin.h"
#include "plugins/vampire-alice/controllers/calendar/Calendar.h"

namespace alice_calendar {
namespace {
uint32_t nextMinute(CdEnvironmentSnapshot snapshot) {
    return uint32_t(60000 - snapshot.unix_ms % 60000);
}
}  // namespace

Instance::Instance(const CdCreateContext& context) : settings(defaults()), renderer_(context) {
    environment_.bind(context);
    timer_.bind(context);
    redraw_.bind(context);
    renderer_.onChange([this] {
        invalidate();
    });
    const auto snapshot = environment_.snapshot();
    month_ = month(localDate(snapshot), 0, true);
    visual_.previous = month_.date;
    if (schedule(nextMinute(snapshot)) != CD_OK) {
        throw std::runtime_error("Could not start calendar date watcher");
    }
}

Instance::~Instance() {
    timer_.stop();
    renderer_.onChange({});
    renderer_.pause(true);
}

CdResult Instance::schedule(uint32_t interval) {
    if (paused_) {
        return CD_OK;
    }
    return timer_.replace(interval, onTick, this);
}

void CD_CALL Instance::onTick(void* pointer, uint64_t) noexcept {
    try {
        static_cast<Instance*>(pointer)->tick();
    } catch (...) {
    }
}

void Instance::tick() {
    if (paused_) {
        return;
    }
    const auto snapshot = environment_.snapshot();
    const auto today = localDate(snapshot);
    bool changed = today != month_.today;
    if (changed) {
        month_ = month(today, offset_, settings.at("week_start").text == "monday");
    }
    if (visual_.progress < 1) {
        const double seconds = snapshot.monotonic_ms >= rollStart_ ? (snapshot.monotonic_ms - rollStart_) / 1000. : .18;
        const cybercanvas::Tween<double> track(0, 1, {.duration = .18, .easing = cybercanvas::Ease::InOutCubic});
        const auto next = track.sample(seconds);
        changed |= visual_.progress != next.value;
        visual_.progress = next.value;
    }
    schedule(visual_.progress < 1 ? 33u : nextMinute(snapshot));
    if (changed) {
        invalidate();
    }
}

CdResult Instance::render(const CdFrame& frame) {
    redraw_.beginFrame();
    return renderer_.render(month_, settings, visual_, frame, safe_) ? CD_OK : CD_ERROR_ARGUMENT;
}

CdResult Instance::setOffset(int value, bool animate) {
    if (value < -1200 || value > 1200) {
        return CD_ERROR_ARGUMENT;
    }
    const auto snapshot = environment_.snapshot();
    auto next = month(localDate(snapshot), value, settings.at("week_start").text == "monday");
    const bool rolling = animate && !paused_ && !settings.at("reduced_motion").integer && next.date != month_.date;
    const auto scheduled = schedule(rolling ? 33u : nextMinute(snapshot));
    if (scheduled != CD_OK) {
        return scheduled;
    }
    visual_.previous = month_.date;
    visual_.progress = rolling ? 0 : 1;
    visual_.direction = value >= offset_ ? 1 : -1;
    rollStart_ = snapshot.monotonic_ms;
    month_ = std::move(next);
    offset_ = value;
    cancelInput();
    invalidate();
    return CD_OK;
}

CdResult Instance::navigate(int value) {
    return setOffset(value, true);
}

CdResult Instance::applySettings(const CdSettingValue* values, uint32_t count) {
    auto next = settings;
    const auto result = next.apply(values, count);
    if (result != CD_OK) {
        return result;
    }
    try {
        cdsdk::fontWide(next.at("font_family").text);
    } catch (...) {
        return CD_ERROR_ARGUMENT;
    }
    const auto snapshot = environment_.snapshot();
    auto nextMonth = month(localDate(snapshot), offset_, next.at("week_start").text == "monday");
    if (next.at("reduced_motion").integer && visual_.progress < 1) {
        const auto scheduled = schedule(nextMinute(snapshot));
        if (scheduled != CD_OK) {
            return scheduled;
        }
        visual_.progress = 1;
    }
    settings = std::move(next);
    month_ = std::move(nextMonth);
    cancelInput();
    invalidate();
    return CD_OK;
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
    cancelInput();
}

void Instance::cancelInput() {
    visual_.pressed = visual_.hovered = 0;
    dragging_ = false;
}

void Instance::input(const CdInput& event) {
    if (paused_ || !std::isfinite(event.x) || !std::isfinite(event.y)) {
        return;
    }
    const double x = event.x - safe_.x, y = event.y - safe_.y;
    const bool inside = x >= 0 && y >= 0 && x < safe_.width && y < safe_.height;
    const unsigned hit = inside ? renderer_.hit(safe_, {x, y}) : 0;
    const auto previous = visual_;
    visual_.hovered = event.kind == CD_INPUT_LEAVE ? 0 : hit;
    if (event.kind == CD_INPUT_LEAVE || !inside) {
        visual_.pressed = 0;
    }
    if (event.kind == CD_INPUT_PRESS && event.buttons & CD_BUTTON_LEFT) {
        visual_.pressed = hit;
        dragging_ = false;
        pressX_ = x;
        pressY_ = y;
    } else if (event.kind == CD_INPUT_MOVE || event.kind == CD_INPUT_RELEASE) {
        dragging_ |= std::hypot(x - pressX_, y - pressY_) > 5;
        if (dragging_) {
            visual_.pressed = 0;
        }
        if (event.kind == CD_INPUT_RELEASE) {
            const auto pressed = visual_.pressed;
            visual_.pressed = 0;
            if (pressed && pressed == hit && !(event.buttons & CD_BUTTON_LEFT)) {
                navigate(std::clamp(offset_ + (hit == 1 ? -1 : 1), -1200, 1200));
            }
        }
    } else if (event.kind == CD_INPUT_DOUBLE_CLICK && inside && !hit) {
        navigate(0);
    }
    if (previous.pressed != visual_.pressed || previous.hovered != visual_.hovered) {
        invalidate();
    }
}

void Instance::setPaused(bool paused) {
    if (paused_ == paused) {
        return;
    }
    paused_ = paused;
    timer_.stop();
    renderer_.pause(paused);
    redraw_.pause(paused);
    visual_.progress = 1;
    cancelInput();
    if (!paused) {
        const auto snapshot = environment_.snapshot();
        month_ = month(localDate(snapshot), offset_, settings.at("week_start").text == "monday");
        schedule(nextMinute(snapshot));
        invalidate();
    }
}

void Instance::invalidate() {
    redraw_.invalidate();
}

std::vector<uint8_t> Instance::saveState() const {
    std::vector<uint8_t> bytes(8);
    bytes[0] = 'M';
    bytes[1] = 'C';
    bytes[2] = 1;
    const int32_t offset = offset_;
    std::memcpy(bytes.data() + 4, &offset, sizeof(offset));
    return bytes;
}

CdResult Instance::restoreState(std::span<const uint8_t> bytes) {
    int32_t value = 0;
    if (!bytes.empty()) {
        if (bytes.size() != 8 || bytes[0] != 'M' || bytes[1] != 'C' || bytes[2] != 1 || bytes[3] != 0) {
            return CD_ERROR_ARGUMENT;
        }
        std::memcpy(&value, bytes.data() + 4, sizeof(value));
        if (value < -1200 || value > 1200) {
            return CD_ERROR_ARGUMENT;
        }
    }
    return setOffset(value, false);
}

const Month& Instance::displayed() const {
    return month_;
}

int Instance::offset() const {
    return offset_;
}

bool Instance::rolling() const {
    return visual_.progress < 1;
}
}  // namespace alice_calendar
