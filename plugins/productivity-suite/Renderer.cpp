#include <CyberCanvas/Mechanical.h>
#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <sdk/Appearance.h>
#include <sdk/Canvas.h>
#include <sdk/Controls.h>
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

#include "plugins/mechanical-instruments/Artwork.h"
#include "plugins/mechanical-instruments/InstrumentPlates.h"
#include "plugins/productivity-suite/Productivity.h"
#include "sdk/WindowsRendering.h"

namespace productivity {
using Microsoft::WRL::ComPtr;

namespace {
D2D1_COLOR_F paint(uint32_t color) {
    return D2D1::ColorF((color >> 24) / 255.f, ((color >> 16) & 255) / 255.f, ((color >> 8) & 255) / 255.f,
                        (color & 255) / 255.f);
}
}  // namespace

struct Renderer::Impl {
    cdsdk::Canvas canvas;
    ComPtr<IDWriteFactory> write{canvas.nativeTextFactory()};
    std::unique_ptr<cdsdk::WindowsFonts> fonts = std::make_unique<cdsdk::WindowsFonts>(write.Get());
    ComPtr<ID2D1RenderTarget>& target = canvas.target;
    ComPtr<ID2D1SolidColorBrush>& brush = canvas.brush;
    cdsdk::Controls controls;
    cybercanvas::ProgressRing progressRing;
    std::array<cybercanvas::LineChart, 2> historyCharts;
    mechanicalart::Artwork artwork;

    void ink(uint32_t color) {
        brush->SetColor(paint(color));
    }

    void text(const std::wstring& source, float size, D2D1_RECT_F area, uint32_t color, bool center = true) {
        cdsdk::label(*fonts, source, size, area, color, center).draw(canvas);
    }

    void card(float w, float h, float radius, uint32_t color) {
        radius = std::clamp(radius, 0.f, std::max(0.f, std::min(w - 4, h - 5) * .5f));
        ink(0x00000015);
        target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(3, 5, w - 1, h - 1), radius, radius), brush.Get());
        ink(0x0000000C);
        target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(1, 3, w - 1, h - 1), radius, radius), brush.Get());
        ink(color);
        target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(1, 1, w - 3, h - 4), radius, radius), brush.Get());
    }

    void modern(Spec spec, const cdsdk::Settings& settings, const Data& d, float w, float h) {
        const auto style = cdsdk::widgetAppearance(settings);
        cdsdk::widgetBackground(canvas, style, {0, 0, w, h});
        const bool en = settings.at("language").text == "en", wide = spec.columns >= 4, large = spec.rows > 1;
        const auto label = [&](const std::wstring& value, double size, cybercanvas::Rect area, uint32_t color,
                               bool center = false) {
            cdsdk::label(
                *fonts, value, std::max(13., size),
                D2D1::RectF(float(area.x), float(area.y), float(area.x + area.width), float(area.y + area.height)),
                color, center, false)
                .draw(canvas);
        };
        const auto track = [&](double x, double y, double width, double value) {
            canvas.box(x, y, width, 3, 1.5, style.track);
            canvas.box(x, y, width * std::clamp(value, 0., 1.), 3, 1.5, style.accent);
        };
        auto theme = cybercanvas::WidgetTheme::light();
        theme.foreground = style.foreground;
        theme.muted = style.secondary;
        theme.track = style.track;
        theme.accent = style.accent;
        theme.surface = 0;
        if (spec.kind == Kind::Timer) {
            const auto time = std::to_wstring(d.remaining / 60) + L":" + (d.remaining % 60 < 10 ? L"0" : L"") +
                              std::to_wstring(d.remaining % 60);
            const double total = double(settings.at(d.resting ? "break_minutes" : "focus_minutes").integer * 60);
            if (large) {
                const double diameter = std::min(w - 36., h - 68.), x = (w - diameter) / 2, y = 12;
                progressRing.draw(canvas, {x, y, diameter, diameter}, d.remaining / std::max(1., total), theme);
                label(time, std::min(38., diameter * .28), {x + 4, y + diameter * .30, diameter - 8, diameter * .38},
                      style.foreground, true);
                label(d.resting ? (en ? L"Break" : L"休息") : (en ? L"Focus" : L"专注"), 13,
                      {x, y + diameter * .65, diameter, 20}, style.secondary, true);
            } else {
                label(time, wide ? 44 : 34, {12, 12, w * (wide ? .51 : .85), h - 50}, style.foreground);
                if (wide)
                    label((d.resting ? (en ? L"Break" : L"休息") : (en ? L"Focus" : L"专注")) + std::wstring(L" · ") +
                              std::to_wstring(d.completed),
                          12, {w * .57, 16, w * .39, 24}, style.secondary);
            }
            for (const auto& hit : hitRegions(spec, d, settings, {0, 0, w, h})) {
                const auto& r = hit.rect;
                cybercanvas::IconButton button(uint32_t(hit.action), {r.x, r.y, r.width, r.height},
                                               hit.action == 2 ? "rotate-ccw"
                                               : d.running     ? "pause"
                                                               : "play");
                button.iconSize = 17;
                button.draw(canvas, theme, controls.icons.painter(), controls.visual(button.id));
            }
            return;
        }
        if (spec.kind == Kind::Countdown) {
            const double info = wide ? w * .44 : w * .68;
            label(wide ? std::wstring{} : productivity::wide(settings.at("title").text), 16, {12, 9, w - 24, 22},
                  style.secondary);
            label(std::to_wstring(std::abs(d.days)), std::min(64.f, h * .61f),
                  {12, wide ? 8. : 29., wide ? w * .38 : w * .66, h - (wide ? 16. : 36.)}, style.foreground, !wide);
            if (wide) {
                label(productivity::wide(settings.at("title").text), 16, {info, 15, w - info - 12, 25},
                      style.foreground);
            }
            label(d.days < 0 ? (en ? L"days ago" : L"天前") : (en ? L"days left" : L"天后"), 13,
                  {info, h * .46, w - info - 12, 20}, style.secondary);
            if (wide)
                label(productivity::wide(settings.at("date").text), 13, {info, h - 31, w - info - 12, 20},
                      style.secondary);
            return;
        }
        if (spec.kind == Kind::Network) {
            const double readout = wide ? w * .48 : w - 12.;
            for (int i = 0; i < 2; ++i) {
                const double y = 12 + i * (h - 24) / 2;
                controls.icons.paint(canvas, {12, y + 9, 14, 14}, i ? "arrow-up" : "arrow-down",
                                     i ? style.secondary : style.accent);
                label(d.valid ? capacity(i ? d.up : d.down, true) : L"—", std::min(23., readout * .12),
                      {34, y, readout - 38, (h - 24) / 2}, style.foreground);
            }
            if (wide && settings.at("history").integer) {
                uint64_t maximum = 1024;
                for (const auto& sample : d.history)
                    maximum = std::max({maximum, sample.first, sample.second});
                for (int channel = 0; channel < 2; ++channel) {
                    std::vector<double> samples(60 - std::min<size_t>(60, d.history.size()), NAN);
                    for (const auto& sample : d.history)
                        samples.push_back(double(channel ? sample.second : sample.first));
                    historyCharts[channel].samples(std::move(samples));
                    historyCharts[channel].range(std::pair{0., double(maximum)});
                    theme.accent = channel ? style.secondary : style.accent;
                    historyCharts[channel].draw(canvas, {w * .51, 14, w * .45, h - 28.}, theme);
                }
            }
            return;
        }
        if (spec.kind == Kind::Disk) {
            const double fraction = d.total ? double(d.total - d.free) / double(d.total) : 0;
            label(productivity::wide(settings.at("drive").text), 13, {12, 12, w - 24, 22}, style.secondary);
            label(d.valid ? std::to_wstring(int(std::round(fraction * 100))) + L"%" : L"—", large ? 43 : 33,
                  {12, 34, wide ? w * .38 : w - 24, h - 57.}, style.foreground);
            const double x = wide ? w * .45 : 12, y = wide ? h * .40 : large ? h - 44 : h - 24;
            track(x, y, w - x - 14, fraction);
            if (wide || large)
                label(d.valid ? capacity(d.free) + L" / " + capacity(d.total) : L"—", 10, {x, y + 10, w - x - 12, 20},
                      style.secondary);
            return;
        }
        if (spec.kind == Kind::Rss)
            label(d.source.empty() ? L"RSS" : productivity::wide(d.source), 13, {12, 8, w - 24, 20}, style.secondary);
        const auto notes = spec.kind == Kind::Notes ? lines(settings.at("text").text) : std::vector<std::string>{};
        bool any = false;
        for (const auto& hit : hitRegions(spec, d, settings, {0, 0, w, h})) {
            if (hit.action < 100)
                continue;
            any = true;
            const size_t index = size_t(hit.action - 100);
            const auto& r = hit.rect;
            if (spec.kind == Kind::Todo) {
                const auto& task = d.tasks[index];
                cybercanvas::Checkbox checkbox(uint32_t(hit.action), {r.x + 2, r.y + (r.height - 18) / 2, 18, 18});
                checkbox.draw(canvas, theme, task.done, controls.icons.painter(),
                              controls.visual(uint32_t(hit.action)));
                label(productivity::wide(task.text), 16, {r.x + 28, r.y, std::max(1., r.width - 32), r.height},
                      task.done ? style.secondary : style.foreground);
            } else if (spec.kind == Kind::Notes) {
                label(productivity::wide(notes[index]), 15, {r.x + 3, r.y, r.width - 6, r.height}, style.foreground);
            } else {
                label(productivity::wide(d.articles[index].title), 15, {r.x + 2, r.y, r.width - 5, r.height - 18},
                      style.foreground);
                label(productivity::wide(d.articles[index].date), 12, {r.x + 2, r.y + r.height - 18, r.width - 5, 18},
                      style.secondary);
            }
        }
        if (!any && !d.error.empty())
            label(productivity::wide(d.error), 13, {12, 30, w - 24, h - 40}, style.secondary);
        nav(spec, d, settings, w, h, style.secondary);
    }

#include "plugins/productivity-suite/MechanicalViews.inl"
#include "plugins/productivity-suite/NetworkObjectView.inl"
#include "plugins/productivity-suite/ObjectViews.inl"
#include "plugins/productivity-suite/Views.inl"
};

Renderer::Renderer() : impl(std::make_unique<Impl>()) {}

Renderer::~Renderer() = default;

void Renderer::bind(const CdCreateContext& context, std::function<void(uint32_t)> action) {
    impl->controls.bind(context, std::move(action));
    impl->canvas.bindGpu(context);
    impl->artwork.bind(context);
}

void Renderer::pause(bool paused) {
    impl->artwork.pause(paused);
}

void Renderer::input(const CdInput& event) {
    impl->controls.input(event);
}

void Renderer::cancelInput() {
    impl->controls.cancel();
}

void Renderer::syncInput(Spec spec, const cdsdk::Settings& settings, const Data& data, CdRect safe) {
    auto& p = *impl;
    std::vector<cdsdk::Controls::Item> controls;
    for (const auto& hit : hitRegions(spec, data, settings, {0, 0, safe.width, safe.height})) {
        const bool task = spec.kind == Kind::Todo && hit.action >= 100;
        std::string name = hit.action == 1   ? (data.running ? "Pause" : "Start")
                           : hit.action == 2 ? "Reset"
                           : hit.action == 3 ? "Previous"
                           : hit.action == 4 ? "Next"
                                             : "Open";
        if (task) {
            name = data.tasks.at(size_t(hit.action - 100)).text;
        } else if (hit.action >= 100 && spec.kind == Kind::Rss) {
            name = data.articles.at(size_t(hit.action - 100)).title;
        } else if (hit.action >= 100 && spec.kind == Kind::Notes) {
            name = lines(settings.at("text").text).at(size_t(hit.action - 100));
        }
        controls.push_back({uint32_t(hit.action),
                            {hit.rect.x, hit.rect.y, hit.rect.width, hit.rect.height},
                            name,
                            true,
                            task,
                            task && data.tasks.at(size_t(hit.action - 100)).done});
    }
    p.controls.items(std::move(controls), {safe.x, safe.y});
}

bool Renderer::render(Spec spec, const cdsdk::Settings& settings, const Data& data, const CdFrame& frame, CdRect safe) {
    const cdsdk::LanguageScope languageScope(settings.at("language").text);
    try {
        auto& p = *impl;
        if (!p.canvas.valid(frame)) {
            return false;
        }
        p.fonts->select(p.write.Get(), settings.at("font_family").text);
        p.canvas.fontCollection(p.fonts->collection());

        const double width = frame.width / frame.device_pixel_ratio, height = frame.height / frame.device_pixel_ratio;
        if (safe.width <= 0 || safe.height <= 0)
            safe = {0, 0, width, height};
        safe.x = std::clamp(safe.x, 0.0, width);
        safe.y = std::clamp(safe.y, 0.0, height);
        safe.width = std::clamp(safe.width, 0.0, width - safe.x);
        safe.height = std::clamp(safe.height, 0.0, height - safe.y);

        syncInput(spec, settings, data, safe);
        auto painting = p.canvas.frame(frame, safe);
        if (safe.width > 12 && safe.height > 12) {
            if (!spec.mechanical && cdsdk::modernAppearance(settings)) {
                p.modern(spec, settings, data, float(safe.width), float(safe.height));
            } else {
                p.draw(spec, settings, data, float(safe.width), float(safe.height));
            }
        }
        p.controls.drawFocus(p.canvas, cybercanvas::WidgetTheme::dark());
        painting.finish();
        return true;
    } catch (...) {
        return false;
    }
}

bool Renderer::savePng(const std::filesystem::path& path, const CdFrame& frame) {
    return impl->canvas.save(path.wstring(), frame);
}
}  // namespace productivity
