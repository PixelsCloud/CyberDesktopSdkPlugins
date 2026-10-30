#include "plugins/clock-suite/Clock.h"

#include <Windows.h>
#include <sdk/RenderOutput.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <tuple>

#include "plugins/clock-suite/FlipClock.h"
#include "sdk/Environment.h"
#include "sdk/Update.h"

namespace clockplugin {
cdsdk::Settings defaults(Spec spec) {
    return {{{"format", CD_SETTING_CHOICE, 0, 0, spec.flip ? "12" : "24", 0, 0, {"24", "12"}},
             {"timezone", CD_SETTING_CHOICE, 0, 0, "local", 0, 0, {"local", "utc", "offset"}},
             {"utc_offset", CD_SETTING_INTEGER, 480, 0, "", -720, 840},
             {"seconds", CD_SETTING_TOGGLE,
              (spec.display == Display::Lcd || spec.analog || spec.rows == 2 || spec.columns == 4 || spec.columns == 1)
                  ? 1
                  : 0},
             {"seconds_visibility", CD_SETTING_CHOICE, 0, 0, "auto", 0, 0, {"auto", "always"}},
             {"seconds_motion", CD_SETTING_CHOICE, 0, 0, "tick", 0, 0, {"tick", "smooth"}},
             {"date", CD_SETTING_TOGGLE, isPixel(spec.display) ? 0 : 1},
             {"weekday", CD_SETTING_TOGGLE, isPixel(spec.display) ? 0 : 1},
             {"date_format", CD_SETTING_CHOICE, 0, 0, "auto", 0, 0, {"auto", "short", "long"}},
             {"time_layout", CD_SETTING_CHOICE, 0, 0, "auto", 0, 0, {"auto", "horizontal", "stacked"}},
             {"theme_colors", CD_SETTING_TOGGLE, spec.display == Display::Classic ? 1 : 0},
             {"text_color", CD_SETTING_COLOR,
              isPixel(spec.display)          ? 0x05DC43FF
              : spec.display == Display::Lcd ? 0x17231BFF
                                             : 0x2B2924FF},
             {"secondary_color", CD_SETTING_COLOR, 0x766B60FF},
             {"seconds_color", CD_SETTING_COLOR,
              isPixel(spec.display)          ? 0x05DC43FF
              : spec.display == Display::Lcd ? 0x82D8ADFF
                                             : 0xC96E37FF},
             {"content_scale", CD_SETTING_NUMBER, 0, 100, "", 70, 100},
             {"relief_strength", CD_SETTING_NUMBER, 0, 65, "", 0, 100},
             {"font_family", CD_SETTING_FONT, 0, 0, spec.flip ? "@zqk-free" : "@siyuan-heavy"},
             {"language", CD_SETTING_CHOICE, 0, 0, "zh-CN", 0, 0, {"zh-CN", "en"}},
             {"flip_duration", CD_SETTING_NUMBER, 0, 500, "", 200, 900},
             {"card_color", CD_SETTING_COLOR,
              isPixel(spec.display)          ? 0x171219FF
              : spec.display == Display::Lcd ? 0xD7DDD0FF
                                             : 0xF8F8F8FF},
             {"glow_strength", CD_SETTING_NUMBER, 0, 70, "", 0, 100},
             {"background_corner_radius", CD_SETTING_INTEGER, 5, 0, "", 0, 32}}};
}

Options options(const cdsdk::Settings& values, CdColor themeText) {
    Options result;
    result.twelveHour = values.at("format").text == "12";
    result.timezone = values.at("timezone").text;
    result.utcOffset = int(values.at("utc_offset").integer);
    result.seconds = values.at("seconds").integer != 0;
    result.smooth = values.at("seconds_motion").text == "smooth";
    result.adaptiveSeconds = values.at("seconds_visibility").text == "auto";

    result.date = values.at("date").integer != 0;
    result.weekday = values.at("weekday").integer != 0;
    result.dateFormat = values.at("date_format").text;
    result.layout = values.at("time_layout").text;

    result.themeColors = values.at("theme_colors").integer != 0;
    result.sizePercent = values.at("content_scale").number;
    result.reliefPercent = values.at("relief_strength").number;

    result.fontFamily = values.at("font_family").text;
    result.language = values.at("language").text;

    result.flipDurationMs = values.at("flip_duration").number;
    result.glowPercent = values.at("glow_strength").number;
    result.cornerRadius = values.at("background_corner_radius").integer;

    const auto color = [](int64_t value) {
        const auto v = uint32_t(value);
        return CdColor{uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)};
    };
    result.primary = result.themeColors ? themeText : color(values.at("text_color").integer);
    if (result.themeColors) {
        const double brightness = .2126 * themeText.r + .7152 * themeText.g + .0722 * themeText.b;
        result.secondary = brightness > 155 ? CdColor{209, 201, 190, 255} : CdColor{118, 107, 96, 255};
    } else
        result.secondary = color(values.at("secondary_color").integer);
    result.accent = color(values.at("seconds_color").integer);
    result.cardColor = color(values.at("card_color").integer);
    return result;
}

Time currentTime(const Options& options, CdEnvironmentSnapshot snapshot) {
    if (options.timezone != "local") {
        snapshot.utc_offset_minutes = options.timezone == "utc" ? 0 : options.utcOffset;
    }
    const auto time = cdsdk::Environment::localTime(snapshot);
    return {time.tm_year + 1900, time.tm_mon + 1, time.tm_mday, time.tm_wday,
            time.tm_hour,        time.tm_min,     time.tm_sec,  int(snapshot.unix_ms % 1000)};
}

Time currentTime(const Options& options) {
    return currentTime(options, cdsdk::Environment::systemSnapshot());
}

Geometry arrange(Spec spec, double width, double height, const Options& options) {
    Geometry g;
    if (width <= 0 || height <= 0)
        return g;
    const double pad = std::clamp(std::min(width, height) * .025, 1.0, 5.0);
    const double w = std::max(0.0, width - pad * 2), h = std::max(0.0, height - pad * 2);
    const double captionSize = std::clamp(std::min(w, h) * .125, 8.0, 20.0),
                 gap = std::clamp(captionSize * .38, 2.0, 7.0);
    const bool one = spec.rows == 1 && spec.columns == 1;
    if (one) {
        // 单格优先使用全部空间展示时钟，旧实例的日期设置不再挤占主体。
        if (spec.analog) {
            const double diameter = std::min(w, h);
            g.dial = {pad + (w - diameter) * .5, pad + (h - diameter) * .5, diameter, diameter};
            return g;
        }
        g.stacked = options.layout == "stacked" || (options.layout == "auto" && w / h < .90);
        const bool showSeconds = options.seconds && (g.stacked || !options.adaptiveSeconds);
        const double secondsHeight = showSeconds ? std::min(h * .23, w * .46) : 0;
        const double timeHeight = std::max(0.0, h - secondsHeight - (showSeconds ? gap : 0));
        g.time = {pad, pad, w, timeHeight};
        if (showSeconds)
            g.seconds = {pad, pad + timeHeight + gap, w, secondsHeight};
        return g;
    }
    if (spec.analog) {
        if (spec.columns == 4 && w / h > 1.28) {
            const double dialZone = w * .36;
            const double diameter = std::min(h * .94, dialZone * .96);
            g.dial = {pad + (dialZone - diameter) * .5, pad + (h - diameter) * .5, diameter, diameter};
            const double left = pad + w * .40, remaining = w * .60;
            const bool calendar = options.date || options.weekday;
            const double timeHeight = h * (calendar ? .65 : .88);
            const double dateHeight = calendar ? h * .23 : 0;
            const double total = timeHeight + dateHeight + (calendar ? gap : 0);
            double y = pad + (h - total) * .5;
            g.time = {left, y, remaining, timeHeight};
            if (calendar)
                g.date = {left, y + timeHeight + gap, remaining, dateHeight};
            g.compactDate = false;
            return g;
        }
        if (spec.columns > spec.rows && w / h > 1.28) {
            const double d = std::min(h, w * (spec.columns == 4 ? .32 : .49));
            g.dial = {pad, pad + (h - d) * .5, d, d};
            const double left = pad + d + std::max(gap, w * .035), remaining = std::max(0.0, pad + w - left);
            const double digitalHeight = std::clamp(h * .32, 14.0, 60.0);
            const double dateHeight = options.date ? captionSize * 1.4 : 0,
                         weekdayHeight =
                             options.weekday && (spec.columns != 4 || !options.date) ? captionSize * 1.25 : 0;
            const double total =
                digitalHeight + dateHeight + weekdayHeight + (dateHeight ? gap : 0) + (weekdayHeight ? gap : 0);
            double y = pad + (h - total) * .5;
            g.time = {left, y, remaining, digitalHeight};
            y += digitalHeight + gap;
            if (weekdayHeight) {
                g.weekday = {left, y, remaining, weekdayHeight};
                y += weekdayHeight + gap;
            }
            if (options.date)
                g.date = {left, y, remaining, dateHeight};
            g.compactDate = spec.columns == 2 || remaining < 130;
            return g;
        }
        const bool footer = !one || h / w > 1.20;
        const double digit = footer ? std::clamp(h * .14, 13.0, 42.0) : 0;
        const bool separateWeek = one || h / w > 1.25;
        const double date = footer && (options.date || (options.weekday && !separateWeek)) ? captionSize * 1.3 : 0;
        const double week = footer && options.weekday && separateWeek ? captionSize * 1.2 : 0;
        const double reserve = footer ? digit + date + week + (date ? gap : 0) + (week ? gap : 0) + gap : 0;
        const double diameter = std::min(w, std::max(0.0, h - reserve));
        const double total = diameter + reserve, y = pad + (h - total) * .5;
        g.dial = {pad + (w - diameter) * .5, y, diameter, diameter};
        double below = y + diameter + gap;
        if (footer) {
            g.time = {pad, below, w, digit};
            below += digit + gap;
        }
        if (week && one) {
            g.weekday = {pad, below, w, week};
            below += week + gap;
        }
        if (date) {
            g.date = {pad, below, w, date};
            below += date + gap;
        }
        if (week && !one)
            g.weekday = {pad, below, w, week};
        g.compactDate = one || w < 150;
        return g;
    }
    g.stacked = spec.columns != 4 && (options.layout == "stacked" || (options.layout == "auto" && w / h < .82));
    if (spec.columns == 4 && w / h > 1.60) {
        const double first = w * .59, rightX = pad + w * .65, rightWidth = w * .35;
        const double second = options.seconds ? first * .19 : 0;
        g.time = {pad, pad + h * .14, first - second, h * .72};
        if (options.seconds)
            g.seconds = {pad + first - second + gap, pad + h * .43, std::max(0.0, second - gap), h * .40};
        const double dateHeight = captionSize * 1.6, weekHeight = captionSize * 1.7;
        const double total = (options.weekday ? weekHeight : 0) + (options.date ? dateHeight : 0) +
                             (options.date && options.weekday ? gap : 0);
        double y = pad + (h - total) * .5;
        if (options.weekday) {
            g.weekday = {rightX, y, rightWidth, weekHeight};
            y += weekHeight + gap;
        }
        if (options.date)
            g.date = {rightX, y, rightWidth, dateHeight};
        g.compactDate = false;
        return g;
    }
    const bool showSeconds = options.seconds && (!one || g.stacked || !options.adaptiveSeconds);
    const double date = (options.date || options.weekday) ? captionSize * 1.4 : 0,
                 second = showSeconds ? captionSize * 2.1 : 0;
    const double reserve = date + second + (date ? gap : 0) + (second ? gap : 0);
    const double available = std::max(0.0, h - reserve);
    const double desired = g.stacked ? available * .89 : std::min(available * .78, w * .49);
    const double total = desired + reserve, y = pad + (h - total) * .5;
    g.time = {pad, y, w, desired};
    double next = y + desired;
    if (second) {
        next += gap;
        g.seconds = {pad, next, w, second};
        next += second;
    }
    if (date) {
        next += gap;
        g.date = {pad, next, w, date};
    }
    g.compactDate = one;
    return g;
}

namespace {
struct Instance {
    Spec spec;
    cdsdk::Settings settings;
    CdHostApi host{};
    void* hostContext = nullptr;
    cdsdk::Timer token;
    cdsdk::Environment environment;
    uint32_t interval = 0;
    bool paused = false;
    CdColor theme{43, 41, 36, 255};
    Rect safe{};
    Renderer renderer;
    std::unique_ptr<FlipRenderer> flipRenderer;
    FlipTimeline flipTimeline;
    uint64_t pausedAt = 0;
    bool flipAnimating = false;
    cdsdk::FrameCache output;
    uint64_t contentStamp = 1;
    uint64_t revision = 1, renderedRevision = 0;
    Time lastTime{};
    bool hasTime = false;
    std::tuple<int, int, int, int, int, int, int> previous{-1, -1, -1, -1, -1, -1, -1};

    explicit Instance(Spec value) : spec(value), settings(defaults(value)) {
        if (spec.flip)
            flipRenderer = std::make_unique<FlipRenderer>();
    }

    ~Instance() {
        token.stop();
    }

    Options option() const {
        return options(settings, theme);
    }

    uint32_t desiredInterval() const {
        const auto o = option();
        return spec.flip ? 16 : spec.analog && o.seconds && o.smooth ? 33 : 1000;
    }

    void redraw() {
        host.request_redraw(hostContext);
    }
};

void CD_CALL tick(void* pointer, uint64_t) noexcept try {
    auto* self = static_cast<Instance*>(pointer);
    if (self->paused)
        return;
    if (self->hasTime) {
        const auto o = self->option();
        const auto time = currentTime(o, self->environment.snapshot());
        const bool smooth = self->spec.analog && o.seconds && o.smooth;
        if ((!self->spec.flip || !self->flipAnimating) && time.year == self->lastTime.year &&
            time.month == self->lastTime.month && time.day == self->lastTime.day && time.hour == self->lastTime.hour &&
            time.minute == self->lastTime.minute && (!o.seconds || time.second == self->lastTime.second) &&
            (!smooth || time.millisecond / 33 == self->lastTime.millisecond / 33))
            return;
    }
    self->redraw();
} catch (...) {
}

CdResult subscribe(Instance& self, uint32_t interval, cdsdk::Timer* token) {
    token->bind(self.host, self.hostContext);
    return token->replace(interval, tick, &self);
}

void CD_CALL destroy(void* pointer) noexcept {
    const auto instance = std::unique_ptr<Instance>(static_cast<Instance*>(pointer));
}

void CD_CALL layout(void* pointer, const CdLayout* layout) noexcept {
    if (!pointer || !layout || layout->struct_size < sizeof(CdLayout))
        return;
    auto& self = *static_cast<Instance*>(pointer);
    self.theme = layout->text_color;
    self.safe = {layout->safe_content.x, layout->safe_content.y, layout->safe_content.width,
                 layout->safe_content.height};
    ++self.revision;
    self.redraw();
}

void CD_CALL input(void*, const CdInput*) noexcept {}  // A clock's content never intercepts desktop dragging.

void CD_CALL pause(void* pointer, uint32_t value) noexcept {
    if (!pointer)
        return;
    auto& self = *static_cast<Instance*>(pointer);
    if (self.paused == (value != 0))
        return;
    if (value && !self.hasTime) {
        self.lastTime = currentTime(self.option(), self.environment.snapshot());
        self.hasTime = true;
    }
    self.paused = value != 0;
    if (self.spec.flip) {
        if (self.paused)
            self.pausedAt = self.environment.snapshot().monotonic_ms;
        else {
            self.flipTimeline.reset(currentTime(self.option(), self.environment.snapshot()), self.option(),
                                    self.environment.snapshot().monotonic_ms);
            self.flipAnimating = false;
        }
    }
    if (self.paused) {
        self.token.stop();
    } else {
        cdsdk::Timer token;
        if (subscribe(self, self.desiredInterval(), &token) == CD_OK) {
            self.token = std::move(token);
            self.interval = self.desiredInterval();
        }
        ++self.revision;
        self.redraw();
    }
}

CdResult CD_CALL get(void* pointer, const char* id, CdSettingValue* output, uint32_t size) noexcept {
    return pointer ? static_cast<Instance*>(pointer)->settings.get(id, output, size) : CD_ERROR_ARGUMENT;
}

CdResult CD_CALL apply(void* pointer, const CdSettingValue* values, uint32_t count) noexcept {
    try {
        if (!pointer)
            return CD_ERROR_ARGUMENT;
        auto& self = *static_cast<Instance*>(pointer);
        auto previous = self.settings;
        const auto applied = self.settings.apply(values, count);
        if (applied != CD_OK)
            return applied;
        const auto desired = self.desiredInterval();
        if (!self.paused && (!self.token || desired != self.interval)) {
            cdsdk::Timer token;
            const auto result = subscribe(self, desired, &token);
            if (result != CD_OK) {
                self.settings = std::move(previous);
                return result;
            }
            self.token = std::move(token);
            self.interval = desired;
        }
        if (self.spec.flip && (self.settings.at("format").text != previous.at("format").text ||
                               self.settings.at("timezone").text != previous.at("timezone").text ||
                               self.settings.at("utc_offset").integer != previous.at("utc_offset").integer))
            self.flipTimeline.reset(
                self.paused ? self.lastTime : currentTime(self.option(), self.environment.snapshot()), self.option(),
                self.paused ? self.pausedAt : self.environment.snapshot().monotonic_ms);
        ++self.revision;
        self.redraw();
        return CD_OK;
    } catch (...) {
        return CD_ERROR_INTERNAL;
    }
}

constexpr std::array<uint8_t, 8> state{'C', 'L', 'O', 'C', 'K', 1, 0, 0};

CdResult CD_CALL save(void* pointer, uint8_t* bytes, uint64_t capacity, uint64_t* size) noexcept {
    if (!pointer || !size)
        return CD_ERROR_ARGUMENT;
    *size = state.size();
    if (!bytes && !capacity)
        return CD_OK;
    if (!bytes || capacity < state.size())
        return CD_ERROR_BUFFER;
    std::memcpy(bytes, state.data(), state.size());
    return CD_OK;
}

CdResult CD_CALL restore(void* pointer, const uint8_t* bytes, uint64_t size) noexcept {
    if (!pointer || (size && !bytes))
        return CD_ERROR_ARGUMENT;
    if (size && (size != state.size() || std::memcmp(bytes, state.data(), state.size()) != 0))
        return CD_ERROR_ARGUMENT;
    auto& self = *static_cast<Instance*>(pointer);
    ++self.revision;
    self.redraw();
    return CD_OK;
}

CdResult CD_CALL render(void* pointer, const CdFrame* frame) noexcept {
    try {
        if (!pointer || !frame || frame->struct_size < sizeof(CdFrame) || !frame->rgba || !frame->width ||
            !frame->height || frame->width > 4096 || frame->height > 4096 || frame->stride < frame->width * 4 ||
            frame->capacity < uint64_t(frame->stride) * frame->height || !std::isfinite(frame->device_pixel_ratio) ||
            frame->device_pixel_ratio <= 0 || frame->device_pixel_ratio > 8)
            return CD_ERROR_ARGUMENT;
        auto& self = *static_cast<Instance*>(pointer);
        const auto o = self.option();
        const auto now = self.paused && self.hasTime ? self.lastTime : currentTime(o, self.environment.snapshot());
        const uint64_t flipNow = self.paused ? self.pausedAt : self.environment.snapshot().monotonic_ms;
        const auto pose = self.spec.flip ? self.flipTimeline.sample(now, o, flipNow) : FlipPose{};
        const bool animating = self.spec.flip && self.flipTimeline.active(flipNow, o.flipDurationMs);
        const int second = o.seconds ? now.second : 0,
                  milli = self.spec.flip ? (animating ? int((flipNow % 1000000000) / 16) + 1 : 0)
                          : self.spec.analog && o.seconds && o.smooth ? now.millisecond / 33
                                                                      : 0;
        const auto key = std::make_tuple(now.year, now.month, now.day, now.hour, now.minute, second, milli);
        if (self.renderedRevision != self.revision || (!self.paused && key != self.previous)) {
            ++self.contentStamp;
            self.previous = key;
            self.renderedRevision = self.revision;
        }
        self.flipAnimating = animating;
        self.lastTime = now;
        self.hasTime = true;
        return self.output.render(*frame, self.contentStamp, [&](const CdFrame& target) {
            const bool painted = self.spec.flip ? self.flipRenderer->render(self.spec, o, now, pose, target, self.safe)
                                                : self.renderer.render(self.spec, o, now, target, self.safe);
            return painted ? CD_OK : CD_ERROR_INTERNAL;
        });
    } catch (...) {
        return CD_ERROR_INTERNAL;
    }
}
}  // namespace

CdResult create(Spec spec, const CdCreateContext* context, CdInstanceApi* output, uint32_t size) {
    try {
        if (!context || context->struct_size < sizeof(CdCreateContext) ||
            context->abi_version != CD_PLUGIN_ABI_VERSION || !output || size < sizeof(CdInstanceApi) ||
            !context->host || context->host->struct_size < sizeof(CdHostApi) ||
            context->host->abi_version != CD_PLUGIN_ABI_VERSION || !context->host->request_redraw ||
            !context->host->subscribe_timer || !context->host->unsubscribe)
            return CD_ERROR_ARGUMENT;
        auto self = std::make_unique<Instance>(spec);
        self->host = *context->host;
        self->hostContext = context->host_context;
        self->environment.bind(*context);
        self->output.bind(*context);
        self->renderer.bindOutput(*context);
        if (self->flipRenderer) {
            self->flipRenderer->bindOutput(*context);
        }
        self->interval = self->desiredInterval();
        if (const auto result = subscribe(*self, self->interval, &self->token); result != CD_OK)
            return result;
        *output = {sizeof(CdInstanceApi),
                   CD_PLUGIN_ABI_VERSION,
                   self.get(),
                   destroy,
                   render,
                   input,
                   layout,
                   pause,
                   save,
                   restore,
                   get,
                   apply};
        self.release();
        return CD_OK;
    } catch (...) {
        return CD_ERROR_INTERNAL;
    }
}
}  // namespace clockplugin
