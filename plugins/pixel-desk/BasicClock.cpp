#include "plugins/pixel-desk/BasicClock.h"

#include <sdk/Language.h>
#include <sdk/WindowsFonts.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <tuple>

#include "plugins/pixel-desk/BasicClockLayouts.h"
#include "plugins/pixel-desk/PixelDigits.h"
#include "plugins/pixel-desk/PixelImages.h"
#include "plugins/pixel-desk/PixelLayout.h"
#include "plugins/pixel-desk/PixelPanels.h"
#include "plugins/pixel-desk/PixelParts.h"

namespace clockplugin {
Time currentTime(const Options& options, CdEnvironmentSnapshot snapshot);
}

namespace pixeldesk {
using cybercanvas::Canvas;
using cybercanvas::Rect;

const BasicSpec& basicSpec(std::string_view id) {
    for (const auto& spec : kBasicClocks) {
        if (id == spec.id) {
            return spec;
        }
    }
    throw std::invalid_argument("Unknown Pixel clock");
}

const BasicSpec& basicFolder(std::string_view folder) {
    for (const auto& spec : kBasicClocks) {
        if (folder == spec.folder) {
            return spec;
        }
    }
    throw std::invalid_argument("Unknown Pixel clock folder");
}

cdsdk::Settings basicDefaults(const BasicSpec& spec) {
    auto settings = clockplugin::defaults(spec.clock());
    const CdSettingValue values[]{
        {sizeof(CdSettingValue), CD_SETTING_TOGGLE, "theme_colors", 0},
        {sizeof(CdSettingValue), CD_SETTING_TOGGLE, "date", spec.kind == BasicKind::Lcd && spec.columns == 4},
        {sizeof(CdSettingValue), CD_SETTING_TOGGLE, "weekday", 0},
        {sizeof(CdSettingValue), CD_SETTING_TOGGLE, "seconds", spec.kind == BasicKind::Analog || spec.columns == 4},
        {sizeof(CdSettingValue), CD_SETTING_COLOR, "text_color",
         spec.kind == BasicKind::Analog || spec.kind == BasicKind::Lcd ? kInk : kMint},
        {sizeof(CdSettingValue), CD_SETTING_COLOR, "secondary_color",
         spec.kind == BasicKind::Analog || spec.kind == BasicKind::Lcd ? kInk : kCream},
        {sizeof(CdSettingValue), CD_SETTING_COLOR, "seconds_color", kClay},
        {sizeof(CdSettingValue), CD_SETTING_FONT, "font_family", 0, 0, "Segoe UI"},
        {sizeof(CdSettingValue), CD_SETTING_CHOICE, "format", 0, 0, "24"},
    };
    if (settings.apply(values, uint32_t(std::size(values))) != CD_OK) {
        throw std::logic_error("Invalid clock defaults");
    }
    return settings;
}

namespace {
std::string pair(int value) {
    return {char('0' + value / 10), char('0' + value % 10)};
}

Rect scaled(Rect area, double fraction) {
    return {area.x + area.width * (1 - fraction) / 2, area.y + area.height * (1 - fraction) / 2, area.width * fraction,
            area.height * fraction};
}

void hand(Canvas& layer, cybercanvas::Point center, double angle, double length, double width, uint32_t color) {
    const double radians = (angle - 90) * std::numbers::pi / 180;
    const unsigned steps = std::max(1u, unsigned(length / 3));
    for (unsigned i = 0; i <= steps; ++i) {
        const double distance = length * i / steps;
        const double x = std::round((center.x + std::cos(radians) * distance) / 2) * 2;
        const double y = std::round((center.y + std::sin(radians) * distance) / 2) * 2;
        layer.box(x - width / 2, y - width / 2, width, width, 0, color);
    }
}

void flipDigit(Canvas& layer, const clockplugin::FlipValue& value, bool tens, Rect area, uint32_t color) {
    const double glyphHeight = std::min(area.height, area.width * 7 / 5);
    area.y += (area.height - glyphHeight) / 2;
    area.height = glyphHeight;
    const auto digit = [=](int number) {
        return std::string(1, char('0' + (tens ? number / 10 : number % 10)));
    };
    const auto before = digit(value.before), after = digit(value.after);
    if (value.progress >= 1 || before == after) {
        pixelDigits(layer, after, area, color);
        return;
    }
    const double middle = area.y + area.height / 2;
    layer.clipped({area.x, area.y, area.width, area.height / 2}, [&] {
        pixelDigits(layer, after, area, color);
    });
    layer.clipped({area.x, middle, area.width, area.height / 2}, [&] {
        pixelDigits(layer, before, area, color);
    });
    const double scale = std::abs(std::cos(value.progress * std::numbers::pi));
    if (scale > .001) {
        const bool first = value.progress < .5;
        layer.clipped({area.x, first ? area.y : middle, area.width, area.height / 2}, [&] {
            layer.saved([&] {
                layer.translate(0, middle);
                layer.scale(1, scale);
                layer.translate(0, -middle);
                layer.box(area.x, area.y, area.width, area.height, 0, 0x203139ff);
                pixelDigits(layer, first ? before : after, area, color);
            });
        });
    }
    layer.line(area.x, middle, area.x + area.width, middle, 0x14242ecc, 1);
}

void lcd(Canvas& layer, std::string_view text, Rect area, uint32_t color) {
    // The established seven-segment encoding; rectilinear caps match this pixel LCD.
    constexpr unsigned bits[]{0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f};
    double units = 0;
    for (char ch : text) {
        units += ch == ':' || ch == '-' ? .4 : 1.2;
    }
    units -= .2;
    const double unit = std::min(area.width / units, area.height / 2), thickness = unit * .14;
    double x = area.x + (area.width - unit * units) / 2;
    const double y = area.y + (area.height - unit * 2) / 2;
    for (char ch : text) {
        if (ch == '-') {
            layer.box(x, y + unit - thickness / 2, unit * .3, thickness, 0, color);
            x += unit * .4;
            continue;
        }
        if (ch == ':') {
            for (double fraction : {.6, 1.4}) {
                layer.box(x, y + unit * fraction - thickness / 2, thickness, thickness, 0, color);
            }
            x += unit * .4;
            continue;
        }
        if (ch < '0' || ch > '9') {
            x += unit * 1.2;
            continue;
        }
        const std::array<Rect, 7> segments{
            {{x + thickness, y, unit - thickness * 2, thickness},
             {x + unit - thickness, y + thickness, thickness, unit - thickness * 1.5},
             {x + unit - thickness, y + unit + thickness / 2, thickness, unit - thickness * 1.5},
             {x + thickness, y + unit * 2 - thickness, unit - thickness * 2, thickness},
             {x, y + unit + thickness / 2, thickness, unit - thickness * 1.5},
             {x, y + thickness, thickness, unit - thickness * 1.5},
             {x + thickness, y + unit - thickness / 2, unit - thickness * 2, thickness}}};
        for (unsigned i = 0; i < segments.size(); ++i) {
            const auto r = segments[i];
            layer.box(r.x, r.y, r.width, r.height, 0, bits[ch - '0'] & (1 << i) ? color : (color & 0xffffff00) | 14);
        }
        x += unit * 1.2;
    }
}
}  // namespace

struct BasicFace::Impl {
    const BasicSpec& spec;
    cdsdk::Canvas canvas;
    cdsdk::Images images;
    cdsdk::WindowsFonts fonts{canvas.nativeTextFactory()};
    std::filesystem::path path;
    std::shared_ptr<const cybercanvas::ImageData> body;
    std::optional<std::tuple<double, double, double, const cybercanvas::ImageData*>> key;
    cybercanvas::OffscreenSurface plate;
    Microsoft::WRL::ComPtr<ID2D1RenderTarget> owner;
    double x = 0, y = 0, scale = 1, extension = 0;
    uint64_t builds = 0;

    Impl(const CdCreateContext& context, const BasicSpec& value) : spec(value) {
        images.bind(context);
        canvas.bindGpu(context);
        path = std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(context.plugin_directory))) /
               "assets/body.png";
    }

    void prepare(double width, double height, double dpi) {
        const bool strip = spec.columns == 4 && spec.rows == 1 && spec.kind != BasicKind::Analog &&
                           spec.kind != BasicKind::Flip && std::string_view(spec.folder) != "clock-brick-4x1";
        const double worldWidth = strip
                                      ? std::max(spec.visible.width, (width - 14) / (height - 14) * spec.visible.height)
                                      : spec.visible.width;
        extension = worldWidth - spec.visible.width;
        const auto bounds = fitBody(width, height, worldWidth, spec.visible.height);
        scale = bounds.width / worldWidth;
        x = bounds.x - spec.visible.x * scale;
        y = bounds.y - spec.visible.y * scale;
        pixeldesk::requestPixelImage(
            images, "body", path, 1,
            uint32_t(std::clamp(std::ceil(std::max(spec.image.width, spec.image.height) * scale * dpi / 64) * 64, 64.,
                                4096.)),
            dpi);
        images.poll();
        body = images.get("body").image;
        const auto next = std::tuple{width, height, dpi, body.get()};
        if (key == next && owner == canvas.target) {
            return;
        }
        canvas.renderLayer(
            plate, {uint32_t(std::ceil(width * dpi)), uint32_t(std::ceil(height * dpi)), dpi}, [&](Canvas& layer) {
                layer.translate(x, y);
                layer.scale(scale, scale);
                if (body) {
                    if (extension > 0) {
                        const double cut = spec.image.width * .55;
                        panelStrip(layer, *body, {0, 0, spec.image.width, spec.image.height},
                                   spec.image.width + extension, cut, cut);
                    } else {
                        layer.image(*body, {0, 0, spec.image.width, spec.image.height},
                                    {.fit = cybercanvas::ImageFit::Stretch,
                                     .clip = false,
                                     .sampling = cybercanvas::ImageSampling::Nearest});
                    }
                }
                if (spec.kind == BasicKind::Lcd) {
                    const Rect screen = spec.columns == 2 ? Rect{51, 70, 212, 58} : Rect{115, 67, 347, 59};
                    layer.box(screen.x, screen.y, screen.width + extension, screen.height, 0, kMint);
                }
                if (std::string_view(spec.folder) == "countdown-2x1") {
                    // Replace the baked dark patches with one flat green
                    // aperture, preserving the stepped rim and paper.
                    auto screen = layer.path();
                    screen.moveTo({158, 85})
                        .lineTo({283, 85})
                        .lineTo({283, 89})
                        .lineTo({289, 89})
                        .lineTo({289, 149})
                        .lineTo({283, 149})
                        .lineTo({283, 154})
                        .lineTo({168, 154})
                        .lineTo({168, 149})
                        .lineTo({161, 149})
                        .lineTo({161, 139})
                        .lineTo({158, 139})
                        .close();
                    const auto previous = layer.target->GetAntialiasMode();
                    layer.target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
                    layer.fill(screen, 0x244d3bff);
                    layer.target->SetAntialiasMode(previous);
                }
            });
        key = next;
        owner = canvas.target;
        ++builds;
    }

    void label(Canvas& layer, const std::wstring& text, Rect area, double size, uint32_t color) {
        cybercanvas::TextStyle style;
        style.family = fonts.family();
        style.locale = cdsdk::drawingLocale();
        style.size = size;
        style.bold = true;
        style.align = cybercanvas::TextAlign::Center;
        style.vertical = cybercanvas::VerticalAlign::Center;
        const auto fit = layer.fitTextInk(text, style, area);
        layer.brush->SetColor(cybercanvas::color(color));
        layer.brush->SetOpacity(1);
        layer.target->DrawTextLayout(D2D1::Point2F(float(fit.origin.x), float(fit.origin.y)), fit.layout.Get(),
                                     layer.brush.Get());
    }
};

BasicFace::BasicFace(const CdCreateContext& context, const BasicSpec& spec)
    : impl_(std::make_unique<Impl>(context, spec)) {}

BasicFace::~BasicFace() = default;

void BasicFace::onChange(std::function<void()> callback) {
    impl_->images.onChange(std::move(callback));
}

void BasicFace::pause(bool paused) {
    impl_->images.pause(paused);
}

bool BasicFace::ready() const {
    return impl_->body && !impl_->images.pending() && impl_->body == impl_->images.get("body").image;
}

uint64_t BasicFace::cacheBuilds() const {
    return impl_->builds;
}

double BasicFace::panelExtension() const {
    return impl_->extension;
}

bool BasicFace::save(const std::filesystem::path& path, const CdFrame& frame) {
    return impl_->canvas.save(path.wstring(), frame);
}

bool BasicFace::render(const cdsdk::Settings& settings, clockplugin::Time time, const clockplugin::FlipPose& pose,
                       const CdFrame& frame, CdRect safe) {
    if (!impl_->canvas.valid(frame)) {
        return false;
    }
    const cdsdk::LanguageScope language(settings.at("language").text);
    impl_->fonts.select(impl_->canvas.nativeTextFactory(), settings.at("font_family").text);
    impl_->canvas.fontCollection(impl_->fonts.collection());
    const auto options = clockplugin::options(settings, {20, 36, 46, 255});
    const auto& spec = impl_->spec;
    const uint32_t primary = uint32_t(settings.at("text_color").integer),
                   secondary = uint32_t(settings.at("secondary_color").integer),
                   accent = uint32_t(settings.at("seconds_color").integer);
    const int hour = options.twelveHour ? (time.hour % 12 ? time.hour % 12 : 12) : time.hour;
    impl_->canvas.render(frame, safe, [&](Canvas& layer, CdRect area) {
        impl_->prepare(area.width, area.height, frame.device_pixel_ratio);
        const auto size = impl_->plate.bitmap()->GetSize();
        layer.target->DrawBitmap(impl_->plate.bitmap(), D2D1::RectF(0, 0, size.width, size.height), 1,
                                 D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
        layer.saved([&] {
            layer.translate(impl_->x, impl_->y);
            layer.scale(impl_->scale, impl_->scale);
            const double fraction = options.sizePercent / 100;
            const bool lcdDate = spec.kind == BasicKind::Lcd && spec.columns == 4 && options.date;
            std::wstring caption;
            if (options.date && !lcdDate) {
                caption = (options.dateFormat == "long" ? std::to_wstring(time.year) + L"-" : L"") +
                          std::to_wstring(time.month) + L"/" + std::to_wstring(time.day);
            }
            if (options.weekday) {
                static constexpr const wchar_t* en[]{L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat"};
                static constexpr const wchar_t* zh[]{L"周日", L"周一", L"周二", L"周三", L"周四", L"周五", L"周六"};
                static constexpr const wchar_t* zhTW[]{L"週日", L"週一", L"週二", L"週三", L"週四", L"週五", L"週六"};
                caption += (caption.empty() ? L"" : L" ") +
                           std::wstring((options.language == "en"      ? en
                                         : options.language == "zh-TW" ? zhTW
                                                                       : zh)[std::clamp(time.weekday, 0, 6)]);
            }
            if (spec.kind == BasicKind::Analog) {
                const double second = time.second + (options.smooth ? time.millisecond / 1000. : 0);
                hand(layer, spec.center, (time.hour % 12 + time.minute / 60.) * 30, spec.radius * .58 * fraction, 7,
                     primary);
                hand(layer, spec.center, (time.minute + second / 60.) * 6, spec.radius * .83 * fraction, 6, primary);
                if (options.seconds) {
                    hand(layer, spec.center, second * 6, spec.radius * .88 * fraction, 2.5, accent);
                }
                pixelDisc(layer, spec.center.x, spec.center.y, 7, kInk);
                pixelDisc(layer, spec.center.x, spec.center.y, 3.5, kAmber);
                if (options.date || options.weekday) {
                    impl_->label(layer, caption,
                                 {spec.center.x - spec.radius * .55, spec.center.y + spec.radius * .35,
                                  spec.radius * 1.1, spec.radius * .20},
                                 spec.radius * .14, secondary);
                }
            } else if (spec.kind == BasicKind::Flip) {
                for (unsigned i = 0; i < spec.readoutCount; ++i) {
                    const unsigned channel = i / 2;
                    if (channel == 2 && !options.seconds) {
                        continue;
                    }
                    flipDigit(layer, pose.values[channel], i % 2 == 0, scaled(spec.readouts[i], fraction), primary);
                }
                if (spec.readoutCount == 4 && options.seconds) {
                    pixelDigits(layer, ":" + pair(time.second), {104, 386, 84, 18}, accent, true);
                }
                if (!caption.empty()) {
                    const Rect captionArea = spec.readoutCount == 4 ? Rect{58, 365, 170, 12}
                                             : spec.rows == 2       ? Rect{125, 302, 390, 12}
                                                                    : Rect{100, 154, 360, 12};
                    impl_->label(layer, caption, captionArea, captionArea.height * .88, secondary);
                }
            } else {
                auto readout = spec.readouts[0];
                readout.width += impl_->extension;
                auto display = scaled(readout, fraction);
                if (!caption.empty()) {
                    const double captionHeight = std::min(22., display.height * .25);
                    impl_->label(layer, caption,
                                 {display.x, display.y + display.height - captionHeight, display.width, captionHeight},
                                 captionHeight * .8, secondary);
                    display.height -= captionHeight + 4;
                }
                if (lcdDate) {
                    const std::string date = std::to_string(time.year) + "-" + pair(time.month) + "-" + pair(time.day);
                    lcd(layer, date, scaled({display.x, display.y, display.width * .44, display.height}, .88),
                        secondary);
                    display.x += display.width * .50;
                    display.width *= .50;
                }
                const bool stacked = options.layout == "stacked" || (options.layout == "auto" && spec.columns == 1);
                const auto digits = [&](std::string text, Rect bounds) {
                    if (spec.kind == BasicKind::Lcd) {
                        lcd(layer, text, bounds, primary);
                    } else {
                        pixelDigits(layer, text, bounds, primary, true);
                    }
                };
                if (stacked) {
                    const double row = display.height / (options.seconds ? 3 : 2);
                    digits(pair(hour), {display.x, display.y, display.width, row * .88});
                    digits(pair(time.minute), {display.x, display.y + row, display.width, row * .88});
                    if (options.seconds) {
                        digits(pair(time.second), {display.x, display.y + row * 2, display.width, row * .88});
                    }
                } else {
                    digits(pair(hour) + ":" + pair(time.minute) + (options.seconds ? ":" + pair(time.second) : ""),
                           display);
                }
            }
        });
    });
    return true;
}

bool BasicFace::renderContent(const CdFrame& frame, CdRect safe, const std::function<void(Canvas&)>& draw,
                              const std::function<void(Canvas&)>& overlay) {
    if (!impl_->canvas.valid(frame)) {
        return false;
    }
    impl_->canvas.render(frame, safe, [&](Canvas& layer, CdRect area) {
        impl_->prepare(area.width, area.height, frame.device_pixel_ratio);
        const auto size = impl_->plate.bitmap()->GetSize();
        layer.target->DrawBitmap(impl_->plate.bitmap(), D2D1::RectF(0, 0, size.width, size.height), 1,
                                 D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
        layer.saved([&] {
            layer.translate(impl_->x, impl_->y);
            layer.scale(impl_->scale, impl_->scale);
            draw(layer);
        });
        if (overlay) {
            overlay(layer);
        }
    });
    return true;
}

BasicClockInstance::BasicClockInstance(const CdCreateContext& context)
    : settings(basicDefaults(basicSpec(context.plugin_id))),
      spec_(basicSpec(context.plugin_id)),
      renderer_(context, spec_) {
    environment_.bind(context);
    timer_.bind(context);
    redraw_.bind(context);
    renderer_.onChange([this] {
        invalidate();
    });
    synchronize();
    timeline_.reset(time_, clockplugin::options(settings, {20, 36, 46, 255}), environment_.snapshot().monotonic_ms);
    if (schedule(settings) != CD_OK) {
        throw std::runtime_error("Cannot schedule Pixel clock");
    }
}

BasicClockInstance::~BasicClockInstance() {
    timer_.stop();
    renderer_.onChange({});
}

void BasicClockInstance::invalidate() {
    redraw_.invalidate();
}

void BasicClockInstance::synchronize() {
    const auto now = environment_.snapshot();
    const auto options = clockplugin::options(settings, {20, 36, 46, 255});
    time_ = clockplugin::currentTime(options, now);
    pose_ = timeline_.sample(time_, options, now.monotonic_ms);
}

CdResult BasicClockInstance::schedule(const cdsdk::Settings& values) {
    if (paused_) {
        timer_.stop();
        return CD_OK;
    }
    const auto now = environment_.snapshot();
    const bool animated =
        (spec_.kind == BasicKind::Flip && timeline_.active(now.monotonic_ms, values.at("flip_duration").number)) ||
        (spec_.kind == BasicKind::Analog && values.at("seconds").integer &&
         values.at("seconds_motion").text == "smooth");
    const uint64_t period = animated                                                          ? 33
                            : values.at("seconds").integer || spec_.kind == BasicKind::Analog ? 1000
                                                                                              : 60000;
    return timer_.replace(animated ? 33 : uint32_t(std::max(uint64_t(16), period - now.unix_ms % period)), tick, this);
}

void CD_CALL BasicClockInstance::tick(void* pointer, uint64_t) noexcept {
    auto& self = *static_cast<BasicClockInstance*>(pointer);
    try {
        self.synchronize();
        self.invalidate();
        if (self.schedule(self.settings) != CD_OK) {
            self.timer_.stop();
        }
    } catch (...) {
        self.timer_.stop();
    }
}

CdResult BasicClockInstance::render(const CdFrame& frame) {
    redraw_.beginFrame();
    if (!paused_) {
        synchronize();
    }
    const bool okay = renderer_.render(settings, time_, pose_, frame, safe_);
    if (okay && !paused_) {
        schedule(settings);
    }
    return okay ? CD_OK : CD_ERROR_ARGUMENT;
}

CdResult BasicClockInstance::applySettings(const CdSettingValue* values, uint32_t count) {
    auto next = settings;
    const auto result = next.apply(values, count);
    if (result != CD_OK) {
        return result;
    }
    const auto scheduled = schedule(next);
    if (scheduled != CD_OK) {
        return scheduled;
    }
    const bool reset = next.at("format").text != settings.at("format").text ||
                       next.at("timezone").text != settings.at("timezone").text ||
                       next.at("utc_offset").integer != settings.at("utc_offset").integer;
    settings = std::move(next);
    if (!paused_) {
        synchronize();
    }
    if (reset) {
        timeline_.reset(time_, clockplugin::options(settings, {20, 36, 46, 255}),
                        paused_ ? pausedAt_ : environment_.snapshot().monotonic_ms);
    }
    invalidate();
    return CD_OK;
}

void BasicClockInstance::layoutChanged(const CdLayout& layout) {
    const auto area = layout.safe_content;
    if (std::isfinite(area.width) && std::isfinite(area.height) && area.width > 12 && area.height > 12 && area.x >= 0 &&
        area.y >= 0 && area.x + area.width <= layout.logical_width && area.y + area.height <= layout.logical_height) {
        safe_ = area;
    } else if (layout.logical_width > 12 && layout.logical_height > 12) {
        safe_ = {0, 0, layout.logical_width, layout.logical_height};
    }
}

void BasicClockInstance::setPaused(bool paused) {
    if (paused_ == paused) {
        return;
    }
    paused_ = paused;
    pausedAt_ = environment_.snapshot().monotonic_ms;
    renderer_.pause(paused);
    redraw_.pause(paused);
    if (!paused) {
        synchronize();
        timeline_.reset(time_, clockplugin::options(settings, {20, 36, 46, 255}), pausedAt_);
        pose_ = timeline_.sample(time_, clockplugin::options(settings, {20, 36, 46, 255}), pausedAt_);
        invalidate();
    }
    schedule(settings);
}
}  // namespace pixeldesk

namespace pixeldesk {
auto BasicSpec::clock() const -> clockplugin::Spec {
    return {id,
            kind == BasicKind::Analog,
            int(rows),
            int(columns),
            kind == BasicKind::Flip,
            kind == BasicKind::Lcd ? clockplugin::Display::Lcd : clockplugin::Display::Classic};
}
}  // namespace pixeldesk
