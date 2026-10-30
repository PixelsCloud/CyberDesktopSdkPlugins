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

#include "plugins/air-quality-suite/AirQuality.h"
#include "plugins/air-quality-suite/MechanicalView.h"
#include "sdk/WindowsRendering.h"

namespace airplugin {
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
    bool dark = true, english = false;
    std::filesystem::path assets;
    ComPtr<IDWriteFactory> write{canvas.nativeTextFactory()};
    std::unique_ptr<cdsdk::WindowsFonts> fonts = std::make_unique<cdsdk::WindowsFonts>(write.Get());
    ComPtr<ID2D1RenderTarget>& target = canvas.target;
    ComPtr<ID2D1SolidColorBrush>& brush = canvas.brush;

    uint32_t ink = 0xE7F3F8FF, muted = 0xADC5D3FF, accent = 0x69D8E5FF;
    cdsdk::Images images;
    mechanicalart::Artwork mechanical;

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

    std::shared_ptr<const cybercanvas::ImageData> icon(const std::string& name) {
        // These bundled icons have a maximum edge of 256 pixels. Preserve their
        // original detail at every DPI; decode only on the bounded shared worker.
        images.set(name, assets / L"icons" / (wide(name) + L".png"), 0, 256);
        return images.get(name).image;
    }

    void symbol(int severity, float cx, float cy, float radius) {
        brush->SetColor(color(0x020C1740));
        target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy + 2), radius + 1, radius + 1), brush.Get());
        const auto tint = severity < 0 ? 0xACC4D0FF : levelColor(severity);
        const D2D1_GRADIENT_STOP colors[]{{0, color((tint & 0xFFFFFF00) | 0x32)},
                                          {1, color((tint & 0xFFFFFF00) | 0x0B)}};
        const auto glow = canvas.linearGradient({cx - radius, cy - radius}, {cx + radius, cy + radius}, colors);
        target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius), glow.Get());
        brush->SetColor(color((tint & 0xFFFFFF00) | 0x55));
        target->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius), brush.Get(), .7f);
        auto glyph = icon(severity < 0 ? "circle-help" : levelIcon(severity));
        if (glyph) {
            const auto area =
                D2D1::RectF(cx - radius * .57f, cy - radius * .57f, cx + radius * .57f, cy + radius * .57f);
            canvas.image(*glyph, {area.left, area.top, double(area.right) - area.left, double(area.bottom) - area.top},
                         {cybercanvas::ImageFit::Stretch});
        }
    }

    void card(float w, float h, float requestedRadius) {
        const auto area = D2D1::RectF(1, 1, w - 3, h - 4);
        const float radius = std::clamp(requestedRadius, 0.f, std::max(0.f, std::min(w - 4, h - 5) * .5f));
        brush->SetColor(color(0x0000001C));
        target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(3, 4, w - 1, h - 1), radius, radius), brush.Get());
        const D2D1_GRADIENT_STOP colors[]{{0, color(dark ? 0x263C49FF : 0xF2F8F7FF)},
                                          {1, color(dark ? 0x13242FFF : 0xDCECE7FF)}};
        const auto gradient = canvas.linearGradient({0, 0}, {w * .4f, h}, colors);
        target->FillRoundedRectangle(D2D1::RoundedRect(area, radius, radius), gradient.Get());
        brush->SetColor(color(dark ? 0xD0EAE725 : 0xFFFFFFFF));
        target->DrawRoundedRectangle(D2D1::RoundedRect(area, radius, radius), brush.Get(), .65f);
    }

    std::wstring status(const Snapshot& data, bool loading) {
        if (data.valid)
            return levelName(level(data.aqi), english);
        if (loading)
            return english ? L"Loading" : L"获取中";
        if (data.error == Error::CredentialsMissing)
            return english ? L"Set token file" : L"设置凭据文件";
        if (data.error == Error::Unauthorized)
            return english ? L"Invalid token" : L"凭据无效";
        if (data.error == Error::LocationUnavailable)
            return english ? L"Set location" : L"定位失败";
        if (data.error == Error::NoData)
            return english ? L"No station data" : L"暂无站点数据";
        return english ? L"Unavailable" : L"未连接";
    }

    std::wstring source(const Snapshot& data) {
        for (const auto& item : data.sources) {
            if (item.url.find("waqi.info") != std::string::npos)
                continue;
            const auto start = item.name.find('('), end = item.name.rfind(')');
            return L"WAQI · " + wide(!english && start != std::string::npos && end > start
                                         ? item.name.substr(start + 1, end - start - 1)
                                         : item.name);
        }
        return L"WAQI · EPA";
    }

    void scale(const Snapshot& data, float x, float y, float span, float thickness = 3) {
        cybercanvas::SegmentedScale scale{{x, y, span, thickness}, {0, 50, 100, 150, 200, 300, 500}};
        scale.gap = 2;
        for (int i = 0; i < 6; ++i) {
            scale.colors.push_back((levelColor(i) & 0xFFFFFF00) | (data.valid ? 0x80 : 0x30));
        }
        auto theme = cybercanvas::WidgetTheme::dark();
        theme.foreground = ink;
        scale.draw(canvas, theme, data.valid ? std::optional<double>(data.aqi) : std::nullopt);
    }

    void footer(const Snapshot& data, float w, float h, bool compact) {
        const auto time = data.time.size() >= 16 ? wide(data.time.substr(11, 5)) : L"--:--";
        const auto updated = (data.cached ? std::wstring(english ? L"Cached · " : L"缓存 · ") : L"") + time;
        cybercanvas::StatusLine footer;
        footer.source = cdsdk::label(
            *fonts, source(data) + (compact && data.cached ? std::wstring(english ? L" · Cached" : L" · 缓存") : L""),
            7.5f, D2D1::RectF(9, h - 16, compact ? w - 9 : w * .76f, h - 5), muted, false, true);
        if (!compact) {
            footer.updated = cdsdk::label(*fonts, updated, 8, D2D1::RectF(w * .79f, h - 16, w - 10, h - 5),
                                          data.cached ? accent : muted, true, true);
        }
        footer.draw(canvas);
    }

    void metrics(const Snapshot& data, int count, float left, float top, float right, float bottom) {
        const wchar_t* names[]{L"PM2.5", L"PM10", L"O3", L"NO2", L"SO2", L"CO"};
        const int columns = count == 6 ? 3 : count, rows = count == 6 ? 2 : 1;
        const float cw = (right - left) / columns, rh = (bottom - top) / rows;
        for (int i = 0; i < count; ++i) {
            const float x = left + (i % columns) * cw, y = top + (i / columns) * rh;
            const auto box = D2D1::RectF(x + 2, y + 1, x + cw - 2, y + rh - 2);
            brush->SetColor(color(dark ? 0x04182245 : 0xFFFFFF5A));
            target->FillRoundedRectangle(D2D1::RoundedRect(box, 5, 5), brush.Get());
            cybercanvas::MetricItem metric;
            metric.caption = cdsdk::label(*fonts, names[i], 8.5,
                                          count == 6 ? D2D1::RectF(x + 7, y + 2, x + cw * .50f, y + rh - 3)
                                                     : D2D1::RectF(x + 5, y + 2, x + cw - 5, y + rh * .37f),
                                          muted, count != 6, true);
            metric.number = cdsdk::label(
                *fonts, data.pollutants[size_t(i)] < 0 ? L"--" : rounded(data.pollutants[size_t(i)]),
                count == 6 ? 18 : 21,
                count == 6 ? D2D1::RectF(x + cw * .50f, y + 2, x + cw - 7, y + rh - 3)
                           : D2D1::RectF(x + 4, y + rh * .34f, x + cw - 4, y + rh - 4),
                data.pollutants[size_t(i)] < 0 ? muted : levelColor(level(data.pollutants[size_t(i)])), true, true);
            metric.number.shadow = dark ? 0x020B1640 : 0x49627424;
            metric.number.shadowOffset = {.4, 1.3};
            metric.draw(canvas);
        }
    }

    void forecast(const Snapshot& data, float left, float top, float right, float bottom) {
        const float cw = (right - left) / 3;
        for (int i = 0; i < 3; ++i) {
            const auto* day = i < int(data.forecast.size()) ? &data.forecast[size_t(i)] : nullptr;
            const float x = left + i * cw;
            label(day ? weekday(day->date, english) : L"--", 8.5f, D2D1::RectF(x, top, x + cw, top + 13), muted, true);
            label(day ? rounded(day->average) : L"--", 16, D2D1::RectF(x, top + 13, x + cw, bottom),
                  day ? levelColor(level(day->average)) : muted, true, true);
        }
    }

    void compact(const Snapshot& data, bool loading, float w, float h, const std::wstring& city) {
        label(city, 11, D2D1::RectF(10, 5, w * .67f, 23), ink);
        label(L"US AQI", 8, D2D1::RectF(w * .67f, 5, w - 10, 23), muted, true);
        const float bottom = h - 31;
        label(data.valid ? rounded(data.aqi) : L"--", 35, D2D1::RectF(10, 22, w * .59f, bottom),
              data.valid ? levelColor(level(data.aqi)) : ink, false, true);
        const float r = std::clamp((bottom - 22) * .29f, 8.f, 18.f);
        symbol(data.valid ? level(data.aqi) : -1, w * .79f, 22 + (bottom - 22) * .38f, r);
        label(status(data, loading), 9, D2D1::RectF(w * .57f, bottom - 13, w - 9, bottom + 2),
              data.valid ? levelColor(level(data.aqi)) : muted, true);
        scale(data, 10, h - 26, w - 20);
        footer(data, w, h, true);
    }

    void strip(const Snapshot& data, bool loading, float w, float h, const std::wstring& city) {
        const float split = w * .48f;
        label(city, 11, D2D1::RectF(11, 5, split - 44, 22), ink);
        label(L"US AQI", 8, D2D1::RectF(split - 42, 5, split - 7, 22), muted, true);
        label(data.valid ? rounded(data.aqi) : L"--", 35, D2D1::RectF(11, 23, split * .63f, h - 30),
              data.valid ? levelColor(level(data.aqi)) : ink, false, true);
        symbol(data.valid ? level(data.aqi) : -1, split * .79f, 37, std::clamp((h - 45) * .22f, 8.f, 15.f));
        label(status(data, loading), 9, D2D1::RectF(split * .56f, h - 42, split - 7, h - 29),
              data.valid ? levelColor(level(data.aqi)) : muted, true);
        scale(data, 11, h - 25, split - 20);
        brush->SetColor(color(dark ? 0xDBEBE627 : 0x3A756E27));
        target->DrawLine(D2D1::Point2F(split, 11), D2D1::Point2F(split, h - 24), brush.Get(), .6f);
        label(english ? L"Pollutant sub-indices" : L"污染物分项指数", 8, D2D1::RectF(split + 7, 5, w - 10, 22), muted,
              true);
        metrics(data, 3, split + 6, 23, w - 8, h - 23);
        footer(data, w, h, false);
    }

    void dashboard(const Snapshot& data, bool loading, float w, float h, const std::wstring& city) {
        const float middle = h * .39f;
        label(city, 14, D2D1::RectF(13, 7, w * .66f, 28), ink);
        label(L"US AQI", 9, D2D1::RectF(w * .74f, 7, w - 12, 28), muted, true);
        label(data.valid ? rounded(data.aqi) : L"--", 46, D2D1::RectF(13, 28, w * .40f, middle - 4),
              data.valid ? levelColor(level(data.aqi)) : ink, false, true);
        label(status(data, loading), 12, D2D1::RectF(w * .40f, 31, w * .74f, middle - 5),
              data.valid ? levelColor(level(data.aqi)) : muted, true);
        symbol(data.valid ? level(data.aqi) : -1, w * .86f, 29 + (middle - 29) * .5f,
               std::clamp((middle - 29) * .34f, 10.f, 22.f));
        scale(data, 13, middle, w - 26, 4);
        const float metricsBottom = middle + (h - middle - 25) * .49f;
        label(english ? L"Pollutant sub-indices" : L"污染物分项指数", 8,
              D2D1::RectF(13, middle + 6, w * .55f, middle + 20), muted);
        metrics(data, 6, 11, middle + 21, w - 11, metricsBottom + 18);
        const float forecastTop = metricsBottom + 21;
        label(english ? L"PM2.5 index forecast" : L"PM2.5 指数预报", 8, D2D1::RectF(13, forecastTop, w * .44f, h - 21),
              muted);
        forecast(data, w * .43f, forecastTop - 1, w - 11, h - 20);
        footer(data, w, h, false);
    }
};

Renderer::Renderer(std::filesystem::path assets) : impl(std::make_unique<Impl>(std::move(assets))) {}

Renderer::~Renderer() = default;

void Renderer::bind(const CdCreateContext& context) {
    impl->images.bind(context);
    impl->canvas.bindGpu(context);
    impl->mechanical.bind(context);
}

void Renderer::pause(bool paused) {
    impl->images.pause(paused);
    impl->mechanical.pause(paused);
}

bool Renderer::renderReference(Spec spec, const cdsdk::Settings& settings, const Snapshot& data, bool loading,
                               const CdFrame& frame, CdRect safe) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    for (;;) {
        if (!render(spec, settings, data, loading, frame, safe)) {
            return false;
        }
        if (!impl->images.pending() && !impl->mechanical.pending()) {
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
        p.dark = settings.at("palette").text == "dark";
        p.ink = p.dark ? 0xE8F4F3FF : 0x284A42FF;
        p.muted = p.dark ? 0xA4BDC7FF : 0x577B72FF;
        p.accent = 0x74E0B3FF;

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
            if (!spec.mechanical) {
                p.card(width, height, float(settings.at("background_corner_radius").integer));
            }
            const auto city = data.valid ? wide(data.city)
                              : settings.at("location_mode").text == "auto"
                                  ? std::wstring(loading ? (english ? L"Locating…" : L"定位中…")
                                                         : (english ? L"Your location" : L"当前位置"))
                              : settings.at("city").text.empty() ? std::wstring(english ? L"Air quality" : L"空气质量")
                                                                 : wide(settings.at("city").text);
            if (spec.mechanical)
                drawMechanicalAir(p.canvas, p.mechanical, spec, data, city, width, height, english);
            else if (spec.rows > 1)
                p.dashboard(data, loading, width, height, city);
            else if (spec.columns == 2)
                p.compact(data, loading, width, height, city);
            else
                p.strip(data, loading, width, height, city);
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
}  // namespace airplugin
