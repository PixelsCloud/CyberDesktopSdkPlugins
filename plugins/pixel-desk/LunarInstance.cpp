#include <algorithm>
#include <cmath>
#include <tuple>

#include "plugins/pixel-desk/LunarCalendar.h"
#include "plugins/pixel-desk/PixelParts.h"

namespace pixeldesk {
const LunarSpec& lunarSpec(std::string_view id) {
    for (const auto& spec : kLunarCatalog) {
        if (id == spec.id) {
            return spec;
        }
    }
    throw std::invalid_argument("Unknown Pixel Desk lunar calendar");
}

cdsdk::Settings lunarDefaults() {
    return {{{"week_start", CD_SETTING_CHOICE, 0, 0, "monday", 0, 0, {"monday", "sunday"}},
             {"adjacent_days", CD_SETTING_TOGGLE, 0},
             {"reduced_motion", CD_SETTING_TOGGLE, 0},
             {"language", CD_SETTING_CHOICE, 0, 0, "zh-CN", 0, 0, {"zh-CN", "en", "zh-TW"}},
             {"text_color", CD_SETTING_COLOR, kInk},
             {"accent_color", CD_SETTING_COLOR, kClay}}};
}

lunarplugin::Date lunarHostDate(CdEnvironmentSnapshot snapshot) {
    const auto local = cdsdk::Environment::localTime(snapshot);
    return {local.tm_year + 1900, local.tm_mon + 1, local.tm_mday};
}

namespace {
uint32_t nextMinute(CdEnvironmentSnapshot snapshot) {
    return std::max(16u, uint32_t(60000 - snapshot.unix_ms % 60000));
}
}  // namespace

LunarInstance::LunarInstance(const CdCreateContext& context)
    : settings(lunarDefaults()), spec_(lunarSpec(context.plugin_id)), renderer_(context, spec_) {
    environment_.bind(context);
    timer_.bind(context);
    redraw_.bind(context);
    renderer_.onChange([this] {
        invalidate();
    });
    renderer_.onAction([this](uint32_t id) {
        action(id);
    });
    const auto snapshot = environment_.snapshot();
    view_.current = view_.selected = view_.previous = lunarHostDate(snapshot);
    if (!lunarplugin::valid(view_.current) || schedule(nextMinute(snapshot)) != CD_OK) {
        throw std::runtime_error("Cannot initialize Pixel Desk lunar calendar");
    }
}

LunarInstance::~LunarInstance() {
    timer_.stop();
    renderer_.onAction({});
    renderer_.onChange({});
    renderer_.pause(true);
}

CdResult LunarInstance::schedule(uint32_t interval) {
    if (hidden_) {
        timer_.stop();
        return CD_OK;
    }
    return timer_.replace(interval, wake, this);
}

void CD_CALL LunarInstance::wake(void* pointer, uint64_t) noexcept {
    auto& self = *static_cast<LunarInstance*>(pointer);
    try {
        self.tick();
    } catch (...) {
        self.timer_.stop();
    }
}

void LunarInstance::updateDate(bool animated) {
    const auto date = lunarHostDate(environment_.snapshot());
    if (!lunarplugin::valid(date) || date == view_.current) {
        return;
    }
    const auto previous = view_.current;
    view_.current = date;
    if (followToday_ && selectDate(date, animated) != CD_OK) {
        view_.current = previous;
        return;
    }
    invalidate();
}

void LunarInstance::tick() {
    if (hidden_) {
        return;
    }
    updateDate(true);
    const auto snapshot = environment_.snapshot();
    if (view_.progress < 1) {
        const double elapsed = snapshot.monotonic_ms >= rollStart_ ? (snapshot.monotonic_ms - rollStart_) / 1000. : .18;
        const cybercanvas::Tween<double> track(0, 1, {.duration = .18, .easing = cybercanvas::Ease::InOutCubic});
        const auto next = track.sample(elapsed).value;
        if (next != view_.progress) {
            view_.progress = next;
            invalidate();
        }
    }
    if (schedule(view_.progress < 1 ? 33u : nextMinute(snapshot)) != CD_OK) {
        timer_.stop();
    }
}

CdResult LunarInstance::selectDate(lunarplugin::Date date, bool animated) {
    if (!lunarplugin::valid(date)) {
        return CD_ERROR_ARGUMENT;
    }
    const auto snapshot = environment_.snapshot();
    const bool digitsChanged = spec_.rows < 4 || date.year != view_.selected.year || date.month != view_.selected.month;
    const bool rolling =
        date != view_.selected && animated && !hidden_ && digitsChanged && !settings.at("reduced_motion").integer;
    const auto result = schedule(rolling ? 33u : nextMinute(snapshot));
    if (result != CD_OK) {
        return result;
    }
    view_.previous = view_.selected;
    view_.direction = std::tie(date.year, date.month, date.day) >=
                              std::tie(view_.selected.year, view_.selected.month, view_.selected.day)
                          ? 1
                          : -1;
    view_.selected = date;
    view_.progress = rolling ? 0 : 1;
    followToday_ = date == view_.current;
    rollStart_ = snapshot.monotonic_ms;
    renderer_.cancelInput();
    invalidate();
    return CD_OK;
}

CdResult LunarInstance::navigate(int months) {
    if (months < -1200 || months > 1200) {
        return CD_ERROR_ARGUMENT;
    }
    return selectDate(lunarplugin::shiftMonth(view_.selected, months));
}

void LunarInstance::action(uint32_t id) {
    if (hidden_) {
        return;
    }
    if (id == 1 || id == 2) {
        navigate(id == 1 ? -1 : 1);
    } else if (id == 3) {
        selectDate(view_.current);
    } else if (id >= 100 && id < 142) {
        const auto grid = lunarplugin::monthGrid(view_.selected, settings.at("week_start").text == "monday");
        selectDate(grid.dates[id - 100]);
    }
}

CdResult LunarInstance::render(const CdFrame& frame) {
    if (frame.struct_size < sizeof(CdFrame)) {
        return CD_ERROR_ARGUMENT;
    }
    redraw_.beginFrame();
    return renderer_.render(settings, view_, frame, safe_) ? CD_OK : CD_ERROR_ARGUMENT;
}

CdResult LunarInstance::applySettings(const CdSettingValue* values, uint32_t count) {
    auto next = settings;
    const auto result = next.apply(values, count);
    if (result != CD_OK) {
        return result;
    }
    if (next.at("reduced_motion").integer && view_.progress < 1) {
        if (const auto scheduled = schedule(nextMinute(environment_.snapshot())); scheduled != CD_OK) {
            return scheduled;
        }
        view_.progress = 1;
    }
    settings = std::move(next);
    renderer_.cancelInput();
    renderer_.syncInput(settings, view_, safe_);
    invalidate();
    return CD_OK;
}

void LunarInstance::layoutChanged(const CdLayout& layout) {
    if (!std::isfinite(layout.logical_width) || !std::isfinite(layout.logical_height) || layout.logical_width <= 0 ||
        layout.logical_height <= 0) {
        return;
    }
    const auto r = layout.safe_content;
    renderer_.cancelInput();
    safe_ = std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.width) && std::isfinite(r.height) && r.x >= 0 &&
                    r.y >= 0 && r.width > 0 && r.height > 0 && r.x + r.width <= layout.logical_width &&
                    r.y + r.height <= layout.logical_height
                ? r
                : CdRect{0, 0, layout.logical_width, layout.logical_height};
    renderer_.syncInput(settings, view_, safe_);
}

void LunarInstance::input(const CdInput& event) {
    if (hidden_ || !std::isfinite(event.x) || !std::isfinite(event.y)) {
        return;
    }
    const auto viewport = lunarViewport(safe_, spec_);
    const auto body = viewport.map(spec_.visible);
    const bool inside =
        event.x >= body.x && event.y >= body.y && event.x < body.x + body.width && event.y < body.y + body.height;
    if (event.kind == CD_INPUT_DOUBLE_CLICK && inside) {
        selectDate(view_.current);
        return;
    }
    renderer_.syncInput(settings, view_, safe_);
    renderer_.input(event);
}

void LunarInstance::setPaused(bool paused) {
    if (hidden_ == paused) {
        return;
    }
    hidden_ = paused;
    renderer_.pause(paused);
    redraw_.pause(paused);
    timer_.stop();
    view_.progress = 1;
    if (!paused) {
        updateDate(false);
        schedule(nextMinute(environment_.snapshot()));
        invalidate();
    }
}

void LunarInstance::invalidate() {
    redraw_.invalidate();
}

std::vector<uint8_t> LunarInstance::saveState() const {
    const auto date = view_.selected;
    return {'P',
            'L',
            1,
            uint8_t(date.year),
            uint8_t(date.year >> 8),
            uint8_t(date.month),
            uint8_t(date.day),
            uint8_t(followToday_)};
}

CdResult LunarInstance::restoreState(std::span<const uint8_t> bytes) {
    if (bytes.empty()) {
        return selectDate(view_.current, false);
    }
    if (bytes.size() != 8 || bytes[0] != 'P' || bytes[1] != 'L' || bytes[2] != 1 || bytes[7] > 1) {
        return CD_ERROR_ARGUMENT;
    }
    const lunarplugin::Date date{bytes[3] | int(bytes[4]) << 8, bytes[5], bytes[6]};
    if (!lunarplugin::valid(date)) {
        return CD_ERROR_ARGUMENT;
    }
    const auto result = selectDate(bytes[7] ? view_.current : date, false);
    if (result == CD_OK) {
        followToday_ = bytes[7] != 0;
    }
    return result;
}

const lunarplugin::View& LunarInstance::view() const {
    return view_;
}

bool LunarInstance::followsToday() const {
    return followToday_;
}
}  // namespace pixeldesk
