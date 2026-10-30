#include <CyberCanvas/DataWidgets.h>
#include <CyberCanvas/Mechanical.h>
#include <Windows.h>
#include <d2d1.h>
#include <dwrite_1.h>
#include <sdk/Canvas.h>
#include <sdk/Language.h>
#include <sdk/Typography.h>
#include <sdk/WindowsFonts.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <stdexcept>

#include "plugins/mechanical-instruments/InstrumentPlates.h"
#include "plugins/system-info-suite/Monitor.h"
#include "sdk/WindowsRendering.h"

namespace monitorplugin {
using Microsoft::WRL::ComPtr;

namespace {
void require(HRESULT value) {
    if (FAILED(value))
        throw std::runtime_error("Monitor drawing error");
}

D2D1_COLOR_F paint(CdColor color) {
    return D2D1::ColorF(color.r / 255.f, color.g / 255.f, color.b / 255.f, color.a / 255.f);
}

struct Palette {
    CdColor ink, label, track, active, temperature;
};

Palette palette(const Options& options) {
    const bool dark = options.palette == "dark" ||
                      (options.palette == "theme" &&
                       (.2126 * options.themeText.r + .7152 * options.themeText.g + .0722 * options.themeText.b) > 155);
    return dark ? Palette{{227, 241, 245, 255},
                          {130, 156, 168, 255},
                          {61, 80, 91, 255},
                          {83, 214, 223, 255},
                          {228, 174, 115, 255}}
                : Palette{{40, 60, 69, 255},
                          {100, 122, 130, 255},
                          {204, 214, 216, 255},
                          {21, 139, 150, 255},
                          {173, 120, 62, 255}};
}
}  // namespace

struct Renderer::Impl {
    cdsdk::Canvas canvas;
    mechanicalart::Artwork artwork;
    ComPtr<ID2D1Factory>& factory = canvas.factory;
    ComPtr<IDWriteFactory> write{canvas.nativeTextFactory()};
    std::unique_ptr<cdsdk::WindowsFonts> fonts = std::make_unique<cdsdk::WindowsFonts>(write.Get());
    std::string language;
    ComPtr<ID2D1RenderTarget>& target = canvas.target;
    ComPtr<ID2D1SolidColorBrush>& brush = canvas.brush;

    std::unique_ptr<cybercanvas::MeterGauge> gauge = std::make_unique<cybercanvas::MeterGauge>();

    void text(const std::wstring& source, bool number, float baseline, CdColor color) {
        const auto value = cdsdk::displayText(source);
        auto style = cdsdk::textStyle(*fonts, number ? 14 : 8, false);
        style.vertical = cybercanvas::VerticalAlign::Top;
        style.ellipsis = false;
        style.characterSpacing = number ? -.2 : .25;
        auto layout = canvas.nativeTextLayout(value, style, {1000, 1000});
        DWRITE_TEXT_METRICS metrics{};
        require(layout->GetMetrics(&metrics));
        DWRITE_LINE_METRICS line{};
        UINT32 count = 0;
        require(layout->GetLineMetrics(&line, 1, &count));
        brush->SetColor(paint(color));
        target->DrawTextLayout(D2D1::Point2F((48 - metrics.width) * .5f, baseline - line.baseline), layout.Get(),
                               brush.Get());
    }
};

Renderer::Renderer() : m_impl(std::make_unique<Impl>()) {}

Renderer::~Renderer() = default;

void Renderer::bind(const CdCreateContext& context, std::function<void()> changed) {
    m_impl->artwork.bind(context);
    m_impl->canvas.bindGpu(context);
    m_impl->artwork.onChange(std::move(changed));
}

void Renderer::pause(bool paused) {
    m_impl->artwork.pause(paused);
}

bool Renderer::render(Spec spec, const Options& options, Values values, const CdFrame& frame, CdRect safe) {
    const cdsdk::LanguageScope languageScope(options.language);
    try {
        auto& p = *m_impl;
        if (!p.canvas.valid(frame)) {
            return false;
        }
        const double width = frame.width / frame.device_pixel_ratio, height = frame.height / frame.device_pixel_ratio;
        p.fonts->select(p.write.Get(), options.fontFamily);
        p.canvas.fontCollection(p.fonts->collection());
        p.language = options.language;
        if (safe.width <= 0 || safe.height <= 0)
            safe = {0, 0, width, height};
        safe.x = std::clamp(safe.x, 0.0, width);
        safe.y = std::clamp(safe.y, 0.0, height);
        safe.width = std::clamp(safe.width, 0.0, width - safe.x);
        safe.height = std::clamp(safe.height, 0.0, height - safe.y);
        const auto colors = palette(options);
        const double numbers[]{values.cpu, values.ram, values.gpu, values.temperature};
        const uint32_t bits[]{CD_SYSTEM_CPU_VALID, CD_SYSTEM_MEMORY_VALID, CD_SYSTEM_GPU_VALID,
                              CD_SYSTEM_CPU_TEMPERATURE_VALID};
        const wchar_t* names[]{L"CPU", options.language == "en" ? L"RAM" : L"内存", L"GPU",
                               options.language == "en" ? L"CPU T" : L"温度"};
        auto drawing = p.canvas.frame(frame, safe);
        if (spec.mechanical && safe.width > 20 && safe.height > 20) {
            const auto plate = mechanicalart::monitorPlate(spec.rows, spec.columns);
            const auto stage = mechanicalart::arrangePlate(plate, safe.width, safe.height);
            const int metrics[]{0, 2, 1, 3};
            const double needle[]{options.needles.cpu, options.needles.ram, options.needles.gpu,
                                  options.needles.temperature};
            const int primary = options.primary == "ram"           ? 1
                                : options.primary == "gpu"         ? 2
                                : options.primary == "temperature" ? 3
                                                                   : 0;
            stage.draw(p.canvas, [&] {
                const bool material = mechanicalart::plateImage(p.canvas, p.artwork, plate, stage);
                for (unsigned i = 0; i < plate.count; ++i) {
                    const int metric = plate.count == 1 ? primary : spec.rows > 1 ? metrics[i] : int(i);
                    const auto face = plate.faces[i];
                    if (!material) {
                        p.canvas.oval(face.center.x, face.center.y, face.radius * 1.12, face.radius * 1.12, 0x88938dff);
                        p.canvas.oval(face.center.x, face.center.y, face.radius, face.radius, 0x151e22ff);
                    }
                    const bool valid = (values.valid & bits[metric]) && std::isfinite(numbers[metric]);
                    const double actual = valid ? numbers[metric] : 0;
                    const auto value =
                        valid ? std::to_wstring(int(std::lround(actual))) + (metric == 3 ? L"°C" : L"%") : L"—";
                    constexpr const wchar_t* labels[]{L"CPU", L"MEM", L"GPU", L""};
                    mechanicalart::instrumentScale(p.canvas, face);
                    mechanicalart::instrumentReadout(p.canvas, face, labels[metric], value,
                                                     (valid && options.interpolate ? needle[metric] : actual) / 100,
                                                     valid, metric == 3);
                }
            });
            drawing.finish();
            return true;
        }
        for (const auto position : arrange(spec, safe.width, safe.height, options)) {
            if (position.scale <= 0)
                continue;
            const int metric = position.metric;
            const bool valid = (values.valid & bits[metric]) != 0;
            const double value = valid && std::isfinite(numbers[metric]) ? numbers[metric] : 0;
            p.target->SetTransform(
                D2D1::Matrix3x2F::Scale(float(position.scale), float(position.scale)) *
                D2D1::Matrix3x2F::Translation(float(safe.x + position.x), float(safe.y + position.y)));
            const auto packed = [](CdColor color) {
                return uint32_t(color.r) << 24 | uint32_t(color.g) << 16 | uint32_t(color.b) << 8 | color.a;
            };
            auto gaugeTheme = cybercanvas::WidgetTheme::dark();
            gaugeTheme.track = packed(colors.track);
            gaugeTheme.accent = packed(metric == 3 ? colors.temperature : colors.active);
            p.gauge->draw(p.factory.Get(), p.target.Get(), p.brush.Get(), {0, 0, 48, 48},
                          std::clamp(value, 0., 100.) / 100, gaugeTheme);
            p.text(names[metric], false, 19, colors.label);
            const std::wstring valueText = metric == 3 ? std::to_wstring(int(std::lround(value))) + L"°C"
                                           : valid     ? std::to_wstring(int(std::lround(value))) + L"%"
                                                       : L"—";
            p.text(valueText, true, 33, colors.ink);
            p.brush->SetColor(paint(colors.track));
            p.target->DrawLine(D2D1::Point2F(20, 46), D2D1::Point2F(28, 46), p.brush.Get(), .8f);
        }
        drawing.finish();
        return true;
    } catch (...) {
        return false;
    }
}

bool Renderer::savePng(const std::wstring& path, const CdFrame& frame, CdColor background) {
    return m_impl->canvas.save(path, frame, background);
}
}  // namespace monitorplugin
