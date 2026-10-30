#include <CyberCanvas/Mechanical.h>
#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <sdk/Canvas.h>
#include <sdk/Images.h>
#include <sdk/Language.h>
#include <sdk/Typography.h>
#include <sdk/WindowsFonts.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <map>
#include <sstream>
#include <thread>

#include "plugins/mechanical-instruments/Artwork.h"
#include "plugins/weather-suite/Weather.h"
#include "sdk/WindowsRendering.h"

namespace weatherplugin {
using Microsoft::WRL::ComPtr;

namespace {
D2D1_COLOR_F color(uint32_t value) {
    return D2D1::ColorF((value >> 24) / 255.f, ((value >> 16) & 255) / 255.f, ((value >> 8) & 255) / 255.f,
                        (value & 255) / 255.f);
}

std::wstring rounded(double value) {
    const auto n = std::llround(value);
    return std::to_wstring(n == 0 ? 0 : n);
}

std::wstring decimal(double value) {
    std::wostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(1) << value;
    return out.str();
}
}  // namespace

struct Renderer::Impl {
    cdsdk::Canvas canvas;
    bool dark = true, english = false, imperial = false;
    std::filesystem::path assets;
    ComPtr<IDWriteFactory> write{canvas.nativeTextFactory()};
    std::unique_ptr<cdsdk::WindowsFonts> fonts = std::make_unique<cdsdk::WindowsFonts>(write.Get());
    ComPtr<ID2D1RenderTarget>& target = canvas.target;
    ComPtr<ID2D1SolidColorBrush>& brush = canvas.brush;

    uint32_t ink = 0xE7F3F8FF, muted = 0xADC5D3FF, accent = 0x69D8E5FF;
    cdsdk::Images images;
    std::map<std::string, std::pair<std::string, std::shared_ptr<const cybercanvas::ImageData>>> iconImages;
    mechanicalart::Artwork artwork;

    explicit Impl(std::filesystem::path root) : assets(std::move(root)) {}

    void label(const std::wstring& source, float size, D2D1_RECT_F area, uint32_t rgba, bool center = false,
               bool relief = false) {
        auto item = cdsdk::label(*fonts, source, size, area, rgba, center, true);
        if (relief) {
            item.shadowOffset = {.4, 1.3};
            item.shadow = dark ? 0x020B1640 : 0x49627424;
        }
        item.draw(canvas);
    }

    std::shared_ptr<const cybercanvas::ImageData> icon(const std::string& name, uint32_t edge,
                                                       const std::string& slot) {
        // Area-filter on the image worker at the destination's physical size.
        // A direct 256px -> 12px bilinear draw undersamples curved cloud edges.
        images.set(slot, assets / L"icons" / (wide(name) + L".png"), 0, edge);
        auto& retained = iconImages[slot];
        if (retained.first != name) {
            retained = {name, nullptr};
        }
        if (const auto ready = images.get(slot).image) {
            retained.second = ready;
        }
        return retained.second;
    }

    void weatherIcon(int code, bool day, D2D1_RECT_F area, bool ground = false, const std::string& slot = "current") {
        if (area.right <= area.left || area.bottom <= area.top) {
            return;
        }
        D2D1_MATRIX_3X2_F transform;
        target->GetTransform(&transform);
        const double scale =
            std::max(std::hypot(transform._11, transform._12), std::hypot(transform._21, transform._22));
        const auto edge = uint32_t(std::clamp(
            std::ceil(std::max(area.right - area.left, area.bottom - area.top) * scale * canvas.devicePixelRatio()), 8.,
            256.));
        auto image = icon(code < 0 ? "not-available" : iconName(code, day), edge, slot);
        if (!image) {
            return;
        }
        const auto size = D2D1::SizeF(float(image->width()), float(image->height()));
        const float w = area.right - area.left, h = area.bottom - area.top;
        const float fit = std::min(w / std::max(1.f, size.width), h / std::max(1.f, size.height));
        const auto box =
            D2D1::RectF(area.left + (w - size.width * fit) * .5f, area.top + (h - size.height * fit) * .5f,
                        area.left + (w + size.width * fit) * .5f, area.top + (h + size.height * fit) * .5f);
        if (ground) {
            brush->SetColor(color(dark ? 0x06152035 : 0x7C9DA322));
            target->FillEllipse(
                D2D1::Ellipse(D2D1::Point2F((box.left + box.right) * .5f, box.bottom - 1), w * .27f, 2.4f),
                brush.Get());
        }
        canvas.image(*image, {box.left, box.top, double(box.right) - box.left, double(box.bottom) - box.top},
                     {cybercanvas::ImageFit::Stretch});
    }

    void card(float w, float h, float requestedRadius) {
        const auto area = D2D1::RectF(1, 1, w - 3, h - 4);
        const float radius = std::clamp(requestedRadius, 0.f, std::max(0.f, std::min(w - 4, h - 5) * .5f));
        brush->SetColor(color(0x00000017));
        target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(3, 4, w - 1, h - 1), radius, radius), brush.Get());
        const D2D1_GRADIENT_STOP colors[]{{0, color(dark ? 0x294C60FF : 0xF4F9FCFF)},
                                          {1, color(dark ? 0x142A36FF : 0xDDEAF0FF)}};
        const auto gradient = canvas.linearGradient({0, 0}, {w * .30f, h}, colors);
        target->FillRoundedRectangle(D2D1::RoundedRect(area, radius, radius), gradient.Get());
        brush->SetColor(color(dark ? 0xB8D4E51D : 0xFFFFFFFF));
        target->DrawRoundedRectangle(D2D1::RoundedRect(area, radius, radius), brush.Get(), .65f);
    }

    std::wstring temperature(double celsius, bool valid = true) {
        return valid ? rounded(displayTemperature(celsius, imperial)) + L"°" : L"--°";
    }

    std::wstring summary(const Snapshot& data, bool loading) {
        if (data.valid)
            return condition(data.code, english);
        if (loading)
            return english ? L"Loading" : L"获取天气";
        if (data.error == Error::LocationUnavailable)
            return english ? L"Set location" : L"请设置位置";
        if (data.error == Error::CityNotFound)
            return english ? L"City not found" : L"未找到城市";
        return english ? L"Unavailable" : L"暂无天气";
    }

    std::wstring range(const Day& day) {
        return temperature(day.high) + L" / " + temperature(day.low);
    }

    void forecast(const Snapshot& data, int count, float left, float top, float right, float bottom,
                  bool instrument = false) {
        const float column = (right - left) / count, rowHeight = bottom - top;
        for (int i = 0; i < count; ++i) {
            const Day* day = i < int(data.days.size()) ? &data.days[size_t(i)] : nullptr;
            const float x = left + i * column;
            cybercanvas::MetricItem forecast;
            forecast.caption = cdsdk::label(*fonts,
                                            i == 0 ? (english ? L"Today" : L"今天")
                                            : day  ? weekday(day->date, english)
                                                   : L"--",
                                            instrument ? 10 : 9,
                                            D2D1::RectF(x, top, x + column, top + (instrument ? 12 : rowHeight * .22f)),
                                            muted, true, true);
            weatherIcon(day ? day->code : -1, true,
                        D2D1::RectF(x + 5, top + (instrument ? 14 : rowHeight * .25f), x + column - 5,
                                    bottom - (instrument ? 15 : rowHeight * .28f)),
                        false, "forecast:" + std::to_string(i));
            forecast.number = cdsdk::label(
                *fonts, day ? range(*day) : L"-- / --", instrument ? 11 : 9,
                D2D1::RectF(x + 2, instrument ? bottom - 13 : top + rowHeight * .77f, x + column - 2, bottom), ink,
                true, true);
            forecast.draw(canvas);
        }
    }

    void footer(const Snapshot& data, bool loading, float w, float h, bool compact) {
        const auto time = data.time.size() >= 16 ? wide(data.time.substr(11, 5)) : L"--:--";
        const auto state = loading && !data.valid      ? (english ? L"Loading" : L"获取中")
                           : data.error != Error::None ? (data.valid ? (english ? L"Cached" : L"缓存")
                                                          : data.error == Error::LocationUnavailable
                                                              ? (english ? L"Location failed" : L"定位失败")
                                                              : (english ? L"Offline" : L"未连接"))
                                                       : time;
        if (compact)
            cybercanvas::StatusLine{cdsdk::label(*fonts, L"Open-Meteo · " + state, 8,
                                                 D2D1::RectF(9, h - 15, w - 9, h - 5), muted, true, true)}
                .draw(canvas);
        else {
            cybercanvas::StatusLine{
                cdsdk::label(*fonts, L"Open-Meteo", 8, D2D1::RectF(12, h - 16, w * .48f, h - 5), muted, false, true),
                cdsdk::label(*fonts, state, 8, D2D1::RectF(w * .64f, h - 16, w - 13, h - 5),
                             data.error == Error::None ? muted : accent, true, true)}
                .draw(canvas);
        }
    }

    void compact(const Snapshot& data, bool loading, float w, float h, const std::wstring& city) {
        if (h < 72) {
            label(city, 11, D2D1::RectF(9, 4, w - 9, 18), ink, false);
            weatherIcon(data.valid ? data.code : -1, data.day, D2D1::RectF(8, 21, w * .40f, h - 5));
            label(temperature(data.temperature, data.valid), 24, D2D1::RectF(w * .43f, 20, w - 8, h - 5), ink);
            return;
        }
        label(city, 12, D2D1::RectF(11, 6, w - 32, 22), ink);
        label(imperial ? L"°F" : L"°C", 9, D2D1::RectF(w - 29, 6, w - 10, 22), muted, true);
        const float bodyTop = 24, bodyBottom = h - 31;
        weatherIcon(data.valid ? data.code : -1, data.day, D2D1::RectF(7, bodyTop, w * .48f, bodyBottom), true);
        label(temperature(data.temperature, data.valid), 36, D2D1::RectF(w * .48f, bodyTop - 1, w - 9, bodyBottom), ink,
              false, true);
        label(summary(data, loading), 9, D2D1::RectF(9, h - 30, w * .51f, h - 18), muted);
        label(data.days.empty() ? L"-- / --" : range(data.days[0]), 9, D2D1::RectF(w * .51f, h - 30, w - 9, h - 18),
              muted, true);
        footer(data, loading, w, h, true);
    }

    void strip(const Snapshot& data, bool loading, float w, float h, const std::wstring& city) {
        const float split = w * .47f;
        if (h < 64) {
            label(city, 11, D2D1::RectF(9, 4, split - 7, 18), ink, false);
            weatherIcon(data.valid ? data.code : -1, data.day, D2D1::RectF(8, 21, split * .40f, h - 5));
            label(temperature(data.temperature, data.valid), 24, D2D1::RectF(split * .43f, 20, split - 7, h - 5), ink);
            forecast(data, 3, split + 6, 4, w - 8, h - 4, true);
            return;
        }
        label(city, 12, D2D1::RectF(12, 5, split - 32, 22), ink);
        label(imperial ? L"°F" : L"°C", 9, D2D1::RectF(split - 29, 5, split - 9, 22), muted, true);
        weatherIcon(data.valid ? data.code : -1, data.day, D2D1::RectF(9, 24, split * .46f, h - 24), true);
        label(temperature(data.temperature, data.valid), 37, D2D1::RectF(split * .48f, 22, split - 7, h - 29), ink,
              false, true);
        label(summary(data, loading), 9, D2D1::RectF(12, h - 30, split - 7, h - 18), muted);
        brush->SetColor(color(dark ? 0xD5E5F023 : 0x5D819326));
        target->DrawLine(D2D1::Point2F(split, 12), D2D1::Point2F(split, h - 25), brush.Get(), .6f);
        forecast(data, 3, split + 6, 9, w - 8, h - 23);
        footer(data, loading, w, h, false);
    }

    void dashboard(const Snapshot& data, bool loading, float w, float h, const std::wstring& city) {
        const float main = h * .47f;
        label(city, 15, D2D1::RectF(14, 7, w * .62f, 27), ink);
        label(summary(data, loading), 10, D2D1::RectF(14, 27, w * .62f, 42), muted);
        label(temperature(data.temperature, data.valid), 55, D2D1::RectF(13, 41, w * .62f, main), ink, false, true);
        label(imperial ? L"°F" : L"°C", 11, D2D1::RectF(w * .40f, main - 19, w * .59f, main - 3), muted);
        weatherIcon(data.valid ? data.code : -1, data.day, D2D1::RectF(w * .62f, 9, w - 14, main - 2), true);
        const float metricTop = main + 3, metricBottom = main + std::max(20.f, h * .13f), column = (w - 28) / 3;
        brush->SetColor(color(dark ? 0x09202D38 : 0xFFFFFF65));
        target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(12, metricTop, w - 12, metricBottom), 5, 5),
                                     brush.Get());
        const std::wstring captions[]{english ? L"Feels" : L"体感", english ? L"Humidity" : L"湿度",
                                      english ? L"Wind" : L"风速"};
        const std::wstring values[]{
            temperature(data.feels, data.valid), data.valid ? rounded(data.humidity) + L"%" : L"--%",
            data.valid ? decimal(data.wind * (imperial ? 2.236936 : 1)) + (imperial ? L" mph" : L" m/s") : L"--"};
        for (int i = 0; i < 3; ++i) {
            const float x = 14 + i * column;
            cybercanvas::MetricItem{
                cdsdk::label(*fonts, captions[i], 8,
                             D2D1::RectF(x + 2, metricTop + 3, x + column * .43f, metricBottom - 3), muted, false,
                             true),
                cdsdk::label(*fonts, values[i], 11,
                             D2D1::RectF(x + column * .43f, metricTop + 3, x + column - 3, metricBottom - 3), ink, true,
                             true)}
                .draw(canvas);
        }
        forecast(data, 5, 11, metricBottom + 7, w - 10, h - 23);
        footer(data, loading, w, h, false);
    }

#include "plugins/weather-suite/ObjectViews.inl"

    void mechanical(Spec spec, const Snapshot& data, bool loading, double w, double h, const std::wstring& city) {
        if (spec.rows == 2 && spec.columns == 4) {
            objectWeather(data, loading, w, h, city);
            return;
        }
        using namespace cybercanvas::mechanical;
        Instruments m(canvas, cdsdk::textStyle(*fonts, 10), cdsdk::displayText);
        const bool large = spec.rows > 1, compact = spec.columns == 2;
        auto a = m.chassis(w, h, large ? (english ? L"WEATHER STATION" : L"气象站") : L"");
        ink = kInk;
        muted = kMuted;
        accent = kBrass;
        const auto time = data.time.size() >= 16 ? wide(data.time.substr(11, 5)) : L"--:--";
        const auto status =
            data.cached || (data.valid && data.error != Error::None) ? (english ? L"CACHED" : L"缓存") : time;
        if (a.height >= 52) {
            m.text(L"Open-Meteo · " + status, {a.x, a.y + a.height - 12, a.width, 12}, 9, kMuted);
            a.height -= 16;
        }
        const bool forecasts = !compact && (!large || a.height >= 104);
        const double forecastHeight = large && forecasts ? 45 : 0;
        const double mainHeight = a.height - (forecastHeight ? forecastHeight + 6 : 0);
        const double primaryWidth = compact ? a.width : a.width * .47;
        m.text(city, {a.x + 2, a.y, primaryWidth - 4, 13}, 11, kInk, cybercanvas::TextAlign::Start);
        const double bodyY = a.y + 15, bodyHeight = std::max(10., mainHeight - 15);
        const double iconSize = std::min(primaryWidth * .40, bodyHeight);
        weatherIcon(data.valid ? data.code : -1, data.day,
                    D2D1::RectF(float(a.x + 1), float(bodyY), float(a.x + iconSize), float(bodyY + iconSize)));
        const double valueHeight = std::max(10., bodyHeight - 13);
        const cybercanvas::Rect readout{a.x + iconSize + 6, bodyY, std::max(1., primaryWidth - iconSize - 9),
                                        valueHeight};
        m.window(readout);
        m.text(temperature(data.temperature, data.valid), readout, std::clamp(valueHeight * .82, 15., 34.));
        m.text(summary(data, loading), {readout.x, bodyY + valueHeight + 2, readout.width, 11}, 10, kMuted);
        if (compact) {
            return;
        }
        if (large) {
            const double meterWidth = (a.width - primaryWidth) * .5;
            const double diameter = std::min(meterWidth - 6, mainHeight);
            m.gauge({a.x + primaryWidth + 3, a.y, meterWidth - 6, diameter},
                    data.needles[0] >= 0 ? data.needles[0] : data.humidity / 100, english ? L"RH" : L"湿度",
                    data.valid ? rounded(data.humidity) + L"%" : L"—", data.valid);
            m.gauge({a.x + primaryWidth + meterWidth + 3, a.y, meterWidth - 6, diameter},
                    data.needles[1] >= 0 ? data.needles[1] : data.wind / 25, imperial ? L"mph" : L"m/s",
                    data.valid ? decimal(data.wind * (imperial ? 2.236936 : 1)) : L"—", data.valid,
                    imperial ? 55.9 : 25);
            if (forecasts) {
                const double top = a.y + mainHeight + 6;
                m.window({a.x, top, a.width, forecastHeight});
                forecast(data, 3, float(a.x + 4), float(top + 3), float(a.x + a.width - 4),
                         float(top + forecastHeight - 3), true);
            }
        } else if (forecasts && a.height >= 38) {
            forecast(data, 3, float(a.x + primaryWidth + 6), float(a.y), float(a.x + a.width), float(a.y + a.height),
                     true);
        }
    }
};

Renderer::Renderer(std::filesystem::path assets) : impl(std::make_unique<Impl>(std::move(assets))) {}

Renderer::~Renderer() = default;

void Renderer::bind(const CdCreateContext& context) {
    impl->images.bind(context);
    impl->canvas.bindGpu(context);
    impl->artwork.bind(context);
}

void Renderer::pause(bool paused) {
    impl->images.pause(paused);
    impl->artwork.pause(paused);
}

bool Renderer::renderReference(Spec spec, const cdsdk::Settings& settings, const Snapshot& data, bool loading,
                               const CdFrame& frame, CdRect safe) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    for (;;) {
        if (!render(spec, settings, data, loading, frame, safe)) {
            return false;
        }
        if (!impl->images.pending()) {
            impl->images.poll();
            return render(spec, settings, data, loading, frame, safe);
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

bool Renderer::render(Spec spec, const cdsdk::Settings& settings, const Snapshot& data, bool loading,
                      const CdFrame& frame, CdRect safe) {
    const cdsdk::LanguageScope languageScope(settings.at("language").text);
    try {
        auto& p = *impl;
        if (!p.canvas.valid(frame)) {
            return false;
        }
        const bool english = settings.at("language").text == "en";
        p.fonts->select(p.write.Get(), settings.at("font_family").text);
        p.canvas.fontCollection(p.fonts->collection());
        p.english = english;
        p.imperial = settings.at("units").text == "imperial";
        p.dark = settings.at("palette").text == "dark";
        p.ink = p.dark ? 0xE7F3F8FF : 0x294859FF;
        p.muted = p.dark ? 0xADC5D3FF : 0x637E8EFF;
        p.accent = p.dark ? 0x69D8E5FF : 0x137E96FF;

        const double w = frame.width / frame.device_pixel_ratio, h = frame.height / frame.device_pixel_ratio;
        if (safe.width <= 0 || safe.height <= 0)
            safe = {0, 0, w, h};
        safe.x = std::clamp(safe.x, 0., w);
        safe.y = std::clamp(safe.y, 0., h);
        safe.width = std::clamp(safe.width, 0., w - safe.x);
        safe.height = std::clamp(safe.height, 0., h - safe.y);

        auto drawing = p.canvas.frame(frame, safe);
        if (safe.width > 48 && safe.height > 40) {
            const float width = float(safe.width), height = float(safe.height);
            const auto city =
                data.valid ? wide(data.city)
                : settings.at("auto_location").integer ? std::wstring(loading ? (english ? L"Locating…" : L"定位中…")
                                                                      : data.error == Error::LocationUnavailable
                                                                          ? (english ? L"Location failed" : L"定位失败")
                                                                          : (english ? L"Your location" : L"当前位置"))
                : settings.at("city").text.empty() ? std::wstring(english ? L"Custom location" : L"自定义位置")
                                                   : wide(settings.at("city").text);
            if (spec.mechanical) {
                p.mechanical(spec, data, loading, width, height, city);
            } else {
                p.card(width, height, float(settings.at("background_corner_radius").integer));
                if (spec.rows > 1)
                    p.dashboard(data, loading, width, height, city);
                else if (spec.columns == 2)
                    p.compact(data, loading, width, height, city);
                else
                    p.strip(data, loading, width, height, city);
            }
        }
        drawing.finish();
        return true;
    } catch (...) {
        return false;
    }
}

bool Renderer::savePng(const std::filesystem::path& path, const CdFrame& frame) {
    return impl->canvas.save(path.wstring(), frame);
}
}  // namespace weatherplugin
