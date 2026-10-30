#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <sdk/Language.h>
#include <sdk/Typography.h>
#include <sdk/WindowsFonts.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <sstream>
#include <stdexcept>

#include "plugins/clock-suite/Clock.h"
#include "sdk/Canvas.h"

namespace clockplugin {
using Microsoft::WRL::ComPtr;

namespace {
void require(HRESULT result) {
    if (FAILED(result))
        throw std::runtime_error("Clock graphics error: " + std::to_string(uint32_t(result)));
}

std::wstring two(int value) {
    wchar_t text[8]{};
    swprintf_s(text, L"%02d", value);
    return text;
}

std::wstring timeText(Time time, const Options& options) {
    const int hour = options.twelveHour ? (time.hour % 12 ? time.hour % 12 : 12) : time.hour;
    return two(hour) + L":" + two(time.minute);
}

const wchar_t* weekdayText(int day, bool english = false) {
    static const wchar_t* names[]{L"周日", L"周一", L"周二", L"周三", L"周四", L"周五", L"周六"};
    static const wchar_t* en[]{L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat"};
    return (english ? en : names)[std::clamp(day, 0, 6)];
}

std::wstring dateText(Time time, const Options& options, bool compact, bool separateWeekday, bool fullYear) {
    if (!options.date)
        return options.weekday && !separateWeekday ? weekdayText(time.weekday, options.language == "en") : L"";
    std::wstring text;
    const bool shortDate = options.dateFormat == "short" || (options.dateFormat == "auto" && compact);
    if (options.language == "en") {
        text = shortDate ? two(time.month) + L"/" + two(time.day)
                         : std::to_wstring(time.year) + L"-" + two(time.month) + L"-" + two(time.day);
        if (options.weekday && !separateWeekday)
            text += L"  " + std::wstring(weekdayText(time.weekday, true));
        return text;
    }
    if (shortDate)
        text = two(time.month) + L"/" + two(time.day);
    else {
        if (options.dateFormat == "long" || fullYear)
            text = std::to_wstring(time.year) + L"年";
        text += std::to_wstring(time.month) + L"月" + std::to_wstring(time.day) + L"日";
    }
    if (options.weekday && !separateWeekday)
        text += L"  " + std::wstring(weekdayText(time.weekday));
    return text;
}

Rect inset(Rect value, double amount) {
    return {value.x + amount, value.y + amount, std::max(0.0, value.width - amount * 2),
            std::max(0.0, value.height - amount * 2)};
}

D2D1_COLOR_F paint(CdColor color, double opacity = 1) {
    return D2D1::ColorF(color.r / 255.f, color.g / 255.f, color.b / 255.f, float(color.a / 255.0 * opacity));
}
}  // namespace

std::wstring dateCaption(Time time, const Options& options, bool compact) {
    return dateText(time, options, compact, false, !compact);
}

struct Renderer::Impl {
    cdsdk::Canvas canvas;
    ComPtr<ID2D1Factory>& factory = canvas.factory;
    ComPtr<IDWriteFactory> write{canvas.nativeTextFactory()};
    std::unique_ptr<cdsdk::WindowsFonts> fonts, privateLatin, privateCjk;
    std::string privateFamily;
    bool privateActive = false;
    std::wstring locale = cdsdk::drawingLocale();
    ComPtr<ID2D1RenderTarget>& target = canvas.target;
    ComPtr<ID2D1SolidColorBrush>& brush = canvas.brush;
    ComPtr<ID2D1StrokeStyle> round;
    ComPtr<ID2D1Factory> strokeFactory;

    uint32_t width = 0, height = 0;
    double dpr = 1;
    std::wstring digitalFamily = L"Segoe UI";
    double relief = .65;

    Impl() {
        fonts = std::make_unique<cdsdk::WindowsFonts>(write.Get());
        auto stroke = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                  D2D1_LINE_JOIN_ROUND);
        require(factory->CreateStrokeStyle(stroke, nullptr, 0, round.GetAddressOf()));
        strokeFactory = factory;
        ComPtr<IDWriteFontCollection> fonts;
        require(write->GetSystemFontCollection(fonts.GetAddressOf()));
        UINT32 index = 0;
        BOOL exists = FALSE;
        require(fonts->FindFamilyName(L"Segoe UI Variable Display", &index, &exists));
        if (exists)
            digitalFamily = L"Segoe UI Variable Display";
    }

    void surface(uint32_t w, uint32_t h, double scale) {
        if (width == w && height == h && dpr == scale) {
            return;
        }

        width = w;
        height = h;
        dpr = scale;
    }

    void ensureStroke() {
        if (strokeFactory == factory) {
            return;
        }
        round.Reset();
        const auto stroke = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                        D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND);
        require(factory->CreateStrokeStyle(stroke, nullptr, 0, round.GetAddressOf()));
        strokeFactory = factory;
    }

    ComPtr<IDWriteTextLayout> text(const std::wstring& value, const std::wstring& family, double font,
                                   DWRITE_FONT_WEIGHT weight) {
        auto* selected = fonts.get();
        if (privateActive) {
            const bool chinese = std::any_of(value.begin(), value.end(), [](wchar_t c) {
                return c > 0x2e7f;
            });
            selected = chinese ? privateCjk.get() : privateLatin.get();
            canvas.fontCollection(selected->collection());
        }
        auto style = cdsdk::textStyle(*selected, std::max(.5, std::round(font * 64) / 64), false);
        style.weight = cybercanvas::FontWeight(selected->collection() ? selected->weight() : weight);
        style.locale = locale;
        style.vertical = cybercanvas::VerticalAlign::Top;
        style.ellipsis = false;
        style.tabularFigures = true;
        return canvas.nativeTextLayout(value, style, {10000, 10000});
    }

    void label(const std::wstring& source, Rect box, CdColor color, double requested, const std::wstring& family,
               DWRITE_FONT_WEIGHT weight, bool centered = true, bool rightAligned = false) {
        const auto value = cdsdk::displayText(source);
        if (value.empty() || box.width < 1 || box.height < 1)
            return;
        auto layout = text(value, family, requested, weight);
        DWRITE_TEXT_METRICS metrics{};
        require(layout->GetMetrics(&metrics));
        const double factor = std::min({1.0, box.width / std::max(.01, double(metrics.width)),
                                        box.height / std::max(.01, double(metrics.height))});
        if (factor < .995) {
            layout = text(value, family, std::max(.5, requested * factor * .995), weight);
            require(layout->GetMetrics(&metrics));
        }
        const double x = box.x + (rightAligned ? box.width - metrics.width
                                  : centered   ? (box.width - metrics.width) * .5
                                               : 0),
                     y = box.y + (box.height - metrics.height) * .5;
        if (relief > 0) {
            const double radius = std::clamp(double(metrics.height) * .018, .35, 1.05);
            const double drop = std::clamp(double(metrics.height) * .025, .6, 1.4);
            constexpr std::array<std::pair<double, double>, 8> taps{
                {{1, 0}, {.707, .707}, {0, 1}, {-.707, .707}, {-1, 0}, {-.707, -.707}, {0, -1}, {.707, -.707}}};
            brush->SetColor(paint({20, 20, 20, color.a}, relief * .030));
            for (const auto [dx, dy] : taps)
                target->DrawTextLayout(D2D1::Point2F(float(x + .35 + dx * radius), float(y + drop + dy * radius)),
                                       layout.Get(), brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            brush->SetColor(paint({18, 18, 18, color.a}, relief * .22));
            target->DrawTextLayout(D2D1::Point2F(float(x + .25), float(y + drop * .6)), layout.Get(), brush.Get(),
                                   D2D1_DRAW_TEXT_OPTIONS_CLIP);
            brush->SetColor(paint({255, 255, 255, color.a}, relief * .14));
            target->DrawTextLayout(D2D1::Point2F(float(x - .18), float(y - .22)), layout.Get(), brush.Get(),
                                   D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
        brush->SetColor(paint(color));
        target->DrawTextLayout(D2D1::Point2F(float(x), float(y)), layout.Get(), brush.Get(),
                               D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }

    void line(double x1, double y1, double x2, double y2, CdColor color, double width, double opacity = 1) {
        brush->SetColor(paint(color, opacity));
        target->DrawLine(D2D1::Point2F(float(x1), float(y1)), D2D1::Point2F(float(x2), float(y2)), brush.Get(),
                         float(width), round.Get());
    }

    void raisedLine(double x1, double y1, double x2, double y2, CdColor color, double width, double opacity = 1) {
        if (relief > 0) {
            const double drop = std::clamp(width * .30, .50, 1.25);
            line(x1 + .35, y1 + drop, x2 + .35, y2 + drop, {18, 18, 18, color.a}, width + 1.25, opacity * relief * .10);
            line(x1 + .30, y1 + drop * .65, x2 + .30, y2 + drop * .65, {18, 18, 18, color.a}, width + .35,
                 opacity * relief * .27);
        }
        line(x1, y1, x2, y2, color, width, opacity);
        if (relief > 0 && width >= 1.1)
            line(x1 - .16, y1 - .20, x2 - .16, y2 - .20, {255, 255, 255, color.a}, std::max(.35, width * .23),
                 opacity * relief * .25);
    }

    void dial(Rect area, Time time, const Options& options) {
        const double d = std::min(area.width, area.height) * std::min(1.0, options.sizePercent / 100.0);
        if (d < 4)
            return;
        const double cx = area.x + area.width * .5, cy = area.y + area.height * .5, r = d * (relief > 0 ? .442 : .47);
        constexpr double pi = 3.14159265358979323846;
        if (relief > 0) {
            const float bezel = float(d * .479), stroke = float(std::max(.55, d * .008));
            brush->SetColor(paint({18, 18, 18, options.primary.a}, relief * .17));
            target->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(float(cx + .35), float(cy + .85)), bezel, bezel),
                                brush.Get(), stroke + 1.1f);
            for (int i = 0; i < 96; ++i) {
                const double a = i * pi / 48, b = (i + 1) * pi / 48;
                const double light = -std::sin((a + b) * .5) * .6 + std::cos((a + b) * .5) * .8;
                const CdColor shade =
                    light >= 0 ? CdColor{255, 255, 255, options.primary.a} : CdColor{18, 18, 18, options.primary.a};
                line(cx + std::sin(a) * bezel, cy - std::cos(a) * bezel, cx + std::sin(b) * bezel,
                     cy - std::cos(b) * bezel, shade, stroke, std::abs(light) * relief * (light >= 0 ? .32 : .24));
            }
        }
        for (int i = 0; i < 60; ++i) {
            const bool hour = i % 5 == 0;
            if (d < 58 && !hour)
                continue;
            const double angle = i * pi / 30, length = d * (hour ? .053 : .029);
            raisedLine(cx + std::sin(angle) * (r - length), cy - std::cos(angle) * (r - length),
                       cx + std::sin(angle) * r, cy - std::cos(angle) * r, options.primary,
                       std::max(.55, d * (hour ? .0075 : .0035)), hour ? .96 : .55);
        }
        for (int n : {12, 3, 6, 9}) {
            const double angle = n * pi / 6, rad = d * (relief > 0 ? .315 : .337),
                         font = std::clamp(d * .100, 6.5, 25.0);
            const double x = cx + std::sin(angle) * rad, y = cy - std::cos(angle) * rad;
            label(std::to_wstring(n), {x - font * 1.6, y - font * .7, font * 3.2, font * 1.4}, options.primary, font,
                  L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL);
        }
        const double seconds = time.second + (options.smooth && options.seconds ? time.millisecond / 1000.0 : 0);
        const double minutes = time.minute + seconds / 60.0, hours = time.hour % 12 + minutes / 60.0;
        const auto hand = [&](double angle, double length, double thickness, CdColor color, double tail) {
            raisedLine(cx - std::sin(angle) * d * tail, cy + std::cos(angle) * d * tail,
                       cx + std::sin(angle) * d * length, cy - std::cos(angle) * d * length, color, thickness);
        };
        hand(hours * pi / 6, .239, std::max(1.65, d * .021), options.primary, .035);
        hand(minutes * pi / 30, .379, std::max(1.10, d * .0135), options.primary, .042);
        if (options.seconds)
            hand(seconds * pi / 30, .279, std::max(.65, d * .0047), options.accent, .062);
        const float pin = float(std::max(1.10, d * .013));
        if (relief > 0) {
            brush->SetColor(paint({18, 18, 18, options.primary.a}, relief * .35));
            target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(float(cx + .45), float(cy + .85)), pin + .55f, pin + .55f),
                                brush.Get());
        }
        brush->SetColor(paint(options.accent));
        target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(float(cx), float(cy)), pin, pin), brush.Get());
        brush->SetColor(paint(options.primary));
        target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(float(cx), float(cy)), pin * .42f, pin * .42f), brush.Get());
    }

#include "plugins/clock-suite/ElectronicViews.inl"
};

Renderer::Renderer() : m_impl(std::make_unique<Impl>()) {}

Renderer::~Renderer() = default;

void Renderer::bindOutput(const CdCreateContext& context) {
    m_impl->canvas.bindGpu(context);
}

void Renderer::drawElectronic(cybercanvas::Canvas& canvas, Spec spec, const Options& options, Time time,
                              cybercanvas::Rect area) {
    const cdsdk::LanguageScope languageScope(options.language);
    auto& p = *m_impl;
    p.relief = std::clamp(options.reliefPercent / 100.0, 0.0, 1.0);
    p.privateActive = p.privateLatin && options.fontFamily == p.privateFamily;
    p.fonts->select(p.write.Get(), options.fontFamily);
    p.canvas.fontCollection(p.fonts->collection());
    p.locale = options.language == "en" ? L"en-US" : cdsdk::drawingLocale();
    p.canvas.drawIn(canvas, area, [&](auto&) {
        p.ensureStroke();
        p.electronic(spec, options, time, area.width, area.height);
    });
}

void Renderer::privateFonts(const std::filesystem::path& latin, const std::filesystem::path& cjk, std::string family) {
    auto& p = *m_impl;
    p.privateLatin = std::make_unique<cdsdk::WindowsFonts>(p.write.Get(), latin);
    p.privateCjk = std::make_unique<cdsdk::WindowsFonts>(p.write.Get(), cjk);
    p.privateFamily = std::move(family);
}

bool Renderer::render(Spec spec, const Options& options, Time time, const CdFrame& frame, Rect safe,
                      std::string* error) {
    const cdsdk::LanguageScope languageScope(options.language);
    try {
        auto& p = *m_impl;
        p.surface(frame.width, frame.height, frame.device_pixel_ratio);
        p.relief = std::clamp(options.reliefPercent / 100.0, 0.0, 1.0);
        p.privateActive = p.privateLatin && options.fontFamily == p.privateFamily;
        p.fonts->select(p.write.Get(), options.fontFamily);
        p.canvas.fontCollection(p.fonts->collection());
        p.locale = options.language == "en" ? L"en-US" : cdsdk::drawingLocale();
        const double width = frame.width / frame.device_pixel_ratio, height = frame.height / frame.device_pixel_ratio;
        if (safe.width <= 0 || safe.height <= 0)
            safe = {0, 0, width, height};
        safe.x = std::clamp(safe.x, 0.0, width);
        safe.y = std::clamp(safe.y, 0.0, height);
        safe.width = std::clamp(safe.width, 0.0, width - safe.x);
        safe.height = std::clamp(safe.height, 0.0, height - safe.y);
        auto painting = p.canvas.frame(frame, {safe.x, safe.y, safe.width, safe.height});
        p.ensureStroke();
        if (spec.display != Display::Classic)
            p.electronic(spec, options, time, safe.width, safe.height);
        else {
            const auto g = arrange(spec, safe.width, safe.height, options);
            const double scale = options.sizePercent / 100.0;
            if (spec.analog)
                p.dial(g.dial, time, options);
            const auto hm = timeText(time, options);
            const bool horizontal = spec.columns > spec.rows;
            const auto family = spec.analog ? std::wstring(L"Georgia") : p.digitalFamily;
            const auto weight = spec.analog
                                    ? (spec.columns == 4 ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL)
                                    : DWRITE_FONT_WEIGHT_SEMI_BOLD;
            const bool centeredAnalog = spec.analog && spec.columns == 4;
            if (g.stacked) {
                const auto split = hm.find(L':');
                Rect top = g.time, bottom = g.time;
                top.height = g.time.height * .49;
                bottom.y += g.time.height * .51;
                bottom.height = top.height;
                p.label(hm.substr(0, split), top, options.primary, top.height * .99 * scale, family, weight);
                p.label(hm.substr(split + 1), bottom, options.primary, bottom.height * .99 * scale, family, weight);
            } else
                p.label(hm, g.time, options.primary, g.time.height * .99 * scale, family, weight,
                        centeredAnalog || !horizontal || !spec.analog,
                        !spec.analog && spec.columns == 4 && options.seconds);
            if (g.seconds.height > 0)
                p.label(two(time.second), g.seconds, options.accent, g.seconds.height * .96 * scale, p.digitalFamily,
                        DWRITE_FONT_WEIGHT_NORMAL, spec.columns != 4);
            if ((options.date || options.weekday) && g.date.height > 0) {
                const bool separate = g.weekday.height > 0;
                p.label(dateText(time, options,
                                 !spec.analog && g.compactDate ||
                                     spec.analog && spec.rows == 1 && spec.columns == 1 && safe.width < 60,
                                 separate, spec.columns == 4 || spec.rows == 2 && safe.height / safe.width > 1.25),
                        g.date, options.secondary, g.date.height * .90 * scale, L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL,
                        centeredAnalog || (!(horizontal && spec.analog) && spec.columns != 4));
            }
            if (options.weekday && g.weekday.height > 0)
                p.label(weekdayText(time.weekday, options.language == "en"), g.weekday, options.secondary,
                        g.weekday.height * .95 * scale, L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL,
                        !(horizontal && spec.analog) && spec.columns != 4);
            if (spec.analog && horizontal && spec.columns != 4 && g.dial.width > 0 &&
                g.time.x > g.dial.x + g.dial.width) {
                const double x = (g.dial.x + g.dial.width + g.time.x) * .5;
                p.line(x, g.dial.y + g.dial.height * .16, x, g.dial.y + g.dial.height * .84, options.secondary, .55,
                       .30);
            }
            if (!spec.analog && spec.columns == 4 && (options.date || options.weekday)) {
                const double x = safe.width * .626;
                p.line(x, safe.height * .27, x, safe.height * .73, options.secondary, .55, .33);
            }
        }
        painting.finish();
        return true;
    } catch (const std::exception& failure) {
        if (error)
            *error = failure.what();
        return false;
    }
}

bool Renderer::savePng(const std::wstring& path, const CdFrame& frame, CdColor background) {
    return m_impl->canvas.save(path, frame, background);
}
}  // namespace clockplugin
