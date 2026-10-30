#include <CyberCanvas/Mechanical.h>
#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
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
            p.draw(spec, settings, data, float(safe.width), float(safe.height));
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
