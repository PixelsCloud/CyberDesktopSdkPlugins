#include "CyberCanvas/DashboardGauge.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>

#include "CyberCanvas/Canvas.h"
#include "CyberCanvas/Native.h"
#include "CyberCanvas/Widgets.h"

namespace cybercanvas {
namespace {
D2D1_POINT_2F point(double angle, double radius) {
    const double radians = angle * 3.14159265358979323846 / 180;
    return D2D1::Point2F(float(40 + radius * std::cos(radians)), float(40 + radius * std::sin(radians)));
}

ComPtr<ID2D1PathGeometry> arc(ID2D1Factory& factory, double span) {
    ComPtr<ID2D1PathGeometry> path;
    ComPtr<ID2D1GeometrySink> sink;
    require(factory.CreatePathGeometry(path.GetAddressOf()));
    require(path->Open(sink.GetAddressOf()));
    sink->BeginFigure(point(135, 27), D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddArc(D2D1::ArcSegment(point(135 + span, 27), D2D1::SizeF(27, 27), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE,
                                  span > 180 ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    require(sink->Close());
    return path;
}

uint32_t alpha(uint32_t rgba, uint8_t opacity) {
    return (rgba & 0xffffff00u) | opacity;
}

D2D1_COLOR_F lighter(uint32_t rgba) {
    auto result = color(rgba);
    const float maximum = std::max({result.r, result.g, result.b});
    const float minimum = std::min({result.r, result.g, result.b});
    if (maximum <= 0) {
        return result;
    }
    const float value = maximum * 1.35f;
    const float saturation = maximum > minimum ? (maximum - minimum) / maximum : 0;
    const float adjusted = std::max(0.f, saturation - std::max(0.f, value - 1));
    const float newMaximum = std::min(1.f, value);
    const auto channel = [&](float component) {
        return saturation > 0
                   ? newMaximum * (1 - adjusted + adjusted * (component / maximum - 1 + saturation) / saturation)
                   : newMaximum;
    };
    result.r = channel(result.r);
    result.g = channel(result.g);
    result.b = channel(result.b);
    return result;
}
}  // namespace

struct DashboardGauge::Impl {
    ComPtr<ID2D1Factory> factory;
    ComPtr<ID2D1PathGeometry> track, active;
    ComPtr<ID2D1StrokeStyle> rounded;
    ComPtr<ID2D1StrokeStyle> square;
    std::array<ComPtr<ID2D1PathGeometry>, 32> ticks;
    std::array<ComPtr<ID2D1PathGeometry>, 32> remainingTicks;
    double span = -1;

    void prepare(ID2D1Factory& current) {
        if (factory.Get() == &current) {
            return;
        }
        factory = &current;
        require(current.CreateStrokeStyle(
            D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND), nullptr, 0,
            rounded.ReleaseAndGetAddressOf()));
        require(current.CreateStrokeStyle(
            D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_SQUARE, D2D1_CAP_STYLE_SQUARE, D2D1_CAP_STYLE_SQUARE), nullptr,
            0, square.ReleaseAndGetAddressOf()));
        track = arc(current, 270);
        active.Reset();
        span = -1;
        for (int count = 1; count <= 31; ++count) {
            ComPtr<ID2D1PathGeometry> path;
            ComPtr<ID2D1GeometrySink> sink;
            require(current.CreatePathGeometry(path.GetAddressOf()));
            require(path->Open(sink.GetAddressOf()));
            for (int tick = 0; tick < count; ++tick) {
                sink->BeginFigure(point(135 + tick * 9, 34), D2D1_FIGURE_BEGIN_HOLLOW);
                sink->AddLine(point(135 + tick * 9, tick % 5 == 0 ? 38 : 36));
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
            }
            require(sink->Close());
            ticks[count] = std::move(path);
            require(current.CreatePathGeometry(path.ReleaseAndGetAddressOf()));
            require(path->Open(sink.ReleaseAndGetAddressOf()));
            for (int tick = count; tick <= 30; ++tick) {
                sink->BeginFigure(point(135 + tick * 9, 34), D2D1_FIGURE_BEGIN_HOLLOW);
                sink->AddLine(point(135 + tick * 9, tick % 5 == 0 ? 38 : 36));
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
            }
            require(sink->Close());
            remainingTicks[count] = std::move(path);
        }
        remainingTicks[0] = ticks[31];
    }
};

DashboardGauge::DashboardGauge() : impl_(std::make_unique<Impl>()) {}

DashboardGauge::~DashboardGauge() = default;

void DashboardGauge::draw(Canvas& canvas, Rect bounds, double fraction, const std::wstring& number,
                          const std::wstring& unit, const WidgetTheme& theme) {
    detail::layoutRect(bounds);
    if (bounds.width <= 0 || bounds.height <= 0 || !std::isfinite(fraction)) {
        return;
    }
    auto& p = *impl_;
    p.prepare(*canvas.factory.Get());
    fraction = std::clamp(fraction, 0., 1.);
    const double span = std::round(270 * 16 * fraction) / 16;
    if (p.span != span) {
        p.active = span > 0 ? arc(*p.factory.Get(), span) : nullptr;
        p.span = span;
    }
    canvas.saved([&] {
        const double side = std::min(bounds.width, bounds.height);
        canvas.translate(bounds.x + (bounds.width - side) / 2, bounds.y + (bounds.height - side) / 2);
        canvas.scale(side / 80, side / 80);
        canvas.brush->SetColor(color(alpha(theme.foreground, 20)));
        canvas.target->DrawGeometry(p.track.Get(), canvas.brush.Get(), 5.5f);
        if (p.active) {
            canvas.brush->SetColor(color(alpha(theme.accent, 30)));
            canvas.target->DrawGeometry(p.active.Get(), canvas.brush.Get(), 9, p.rounded.Get());
            auto dark = color(theme.accent);
            dark.r /= 1.15f;
            dark.g /= 1.15f;
            dark.b /= 1.15f;
            const std::array<D2D1_GRADIENT_STOP, 2> stops{{{0, dark}, {1, lighter(theme.accent)}}};
            const auto gradient = canvas.linearGradient({13, 67}, {67, 13}, stops);
            canvas.target->DrawGeometry(p.active.Get(), gradient.Get(), 5.5f, p.rounded.Get());
        }
        const int activeTicks = int(std::ceil(fraction * 30));
        canvas.brush->SetColor(color(alpha(theme.foreground, 45)));
        canvas.target->DrawGeometry(p.remainingTicks[activeTicks].Get(), canvas.brush.Get(), .8f, p.square.Get());
        if (activeTicks > 0) {
            canvas.brush->SetColor(color(alpha(theme.accent, 170)));
            canvas.target->DrawGeometry(p.ticks[activeTicks].Get(), canvas.brush.Get(), .8f, p.square.Get());
        }
        auto text = theme.font;
        text.size = 24;
        text.weight = FontWeight::SemiBold;
        text.align = TextAlign::Center;
        text.vertical = VerticalAlign::Center;
        text.ellipsis = false;
        canvas.text(number, text, {0, 22, 80, 32}, theme.accent);
        text.size = unit == L"%" ? 11 : 9;
        text.weight = FontWeight::Normal;
        canvas.text(unit, text, {0, 51, 80, 14}, theme.muted);
    });
}
}  // namespace cybercanvas
