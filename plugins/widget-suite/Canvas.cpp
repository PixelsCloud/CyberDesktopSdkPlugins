#include <CyberCanvas/EffectsDrawing.h>
#include <CyberCanvas/Mechanical.h>
#include <CyberCanvas/Scene.h>
#include <CyberCanvas/Sprites.h>
#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <sdk/Canvas.h>
#include <sdk/Controls.h>
#include <sdk/Language.h>
#include <sdk/Typography.h>
#include <sdk/WindowsFonts.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <stdexcept>
#include <tuple>

#include "plugins/mechanical-instruments/Artwork.h"
#include "plugins/widget-suite/AlbumArtwork.h"
#include "plugins/widget-suite/MechanicalInputCounter.h"
#include "plugins/widget-suite/MechanicalSpectrum.h"
#include "plugins/widget-suite/PlayerArtwork.h"
#include "plugins/widget-suite/Widget.h"

namespace widgetplugin {
using Microsoft::WRL::ComPtr;

namespace {
void require(HRESULT value) {
    if (FAILED(value))
        throw std::runtime_error("Widget drawing failed");
}

D2D1_COLOR_F paint(uint32_t color) {
    return D2D1::ColorF((color >> 24) / 255.f, ((color >> 16) & 255) / 255.f, ((color >> 8) & 255) / 255.f,
                        (color & 255) / 255.f);
}
}  // namespace

struct Canvas::Impl {
    cdsdk::Canvas canvas;
    uint32_t width = 0, height = 0;
    double dpr = 0;
    ComPtr<ID2D1Factory>& factory = canvas.factory;
    ComPtr<IDWriteFactory> write{canvas.nativeTextFactory()};
    std::unique_ptr<cdsdk::WindowsFonts> fonts = std::make_unique<cdsdk::WindowsFonts>(write.Get());
    ComPtr<ID2D1RenderTarget>& target = canvas.target;
    ComPtr<ID2D1SolidColorBrush>& brush = canvas.brush;

    cdsdk::Controls controls;
    mechanicalart::Artwork artwork;
    bool albumHovered = false;
    bool albumEnglish = false;
    bool mechanicalAlbumInput = false;
    CdRect albumInputBounds{};
    std::function<void()> invalidate;
    cybercanvas::ShadowPainter recordShadow;
    cybercanvas::Scene recordLabelScene;
    cybercanvas::Scene::Node recordLabelNode = 0;
    const Media* recordLabelMedia = nullptr;
    uint32_t recordLabelAccent = 0;

    void surface(const CdFrame& frame) {
        if (target && width == frame.width && height == frame.height && dpr == frame.device_pixel_ratio) {
            return;
        }
        // Matrix sprites remain a native extension; image uploads belong to Canvas.
        // The shared Canvas owns the main surface and resets it after this hook.
        matrixCache = {};
        width = frame.width;
        height = frame.height;
        dpr = frame.device_pixel_ratio;
    }

    void ink(uint32_t color) {
        brush->SetColor(paint(color));
    }

    void text(const std::wstring& source, float size, D2D1_RECT_F area, uint32_t color, bool center = true) {
        cdsdk::label(*fonts, source, size, area, color, center, true).draw(canvas);
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

#include "plugins/widget-suite/MatrixViews.inl"

    void music(Spec spec, const cdsdk::Settings& settings, const Spectrum& spectrum, float w, float h) {
        if (spec.mechanical && (spec.kind == Kind::Radial || spec.kind == Kind::Bars)) {
            drawMechanicalSpectrum(canvas, artwork, spec, spectrum, settings.at("peaks").integer != 0, w, h);
            return;
        }
        if (isMatrix(spec.kind)) {
            matrix(spec, settings, spectrum, w, h);
            return;
        }

        const bool radial = spec.kind == Kind::Radial, english = settings.at("language").text == "en";
        const uint32_t color = uint32_t(settings.at("color").integer), top = uint32_t(settings.at("color_top").integer);
        // The desktop brick supplies the surface. Do not stamp an extra plate
        // (or its rectangular shadow) into an otherwise transparent spectrum.
        if (settings.at("show_background").integer)
            card(w, h, float(settings.at("background_corner_radius").integer),
                 uint32_t(settings.at("background").integer));
        if (radial) {
            const float diameter = std::max(8.f, std::min(w - 14, h - 30)), radius = diameter * .28f,
                        length = diameter * .20f, cx = (w - 2) * .5f, cy = (h - 17) * .5f;
            constexpr int bars = 48;
            constexpr double pi = 3.14159265358979323846;
            const float stroke = std::max(1.f, diameter * .017f);
            for (int i = 0; i < bars; ++i) {
                const float level = spectrum.sample(double(i) * (Spectrum::count - 1) / (bars - 1)),
                            peak = spectrum.sample(double(i) * (Spectrum::count - 1) / (bars - 1), true);
                const double angle = 2 * pi * i / bars - pi * .5;
                const float dx = float(std::cos(angle)), dy = float(std::sin(angle));
                const auto base = paint(color), end = paint(top);
                brush->SetColor(D2D1::ColorF(std::lerp(base.r, end.r, level), std::lerp(base.g, end.g, level),
                                             std::lerp(base.b, end.b, level), base.a));
                target->DrawLine(D2D1::Point2F(cx + dx * radius, cy + dy * radius),
                                 D2D1::Point2F(cx + dx * (radius + std::max(1.f, length * level)),
                                               cy + dy * (radius + std::max(1.f, length * level))),
                                 brush.Get(), stroke);
                if (settings.at("peaks").integer && peak > .01f) {
                    ink(0xE5F7F4D0);
                    target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + dx * (radius + length * peak + 3),
                                                                    cy + dy * (radius + length * peak + 3)),
                                                      stroke * .4f, stroke * .4f),
                                        brush.Get());
                }
            }
            ink(0xD8EAF1FF);
            const float note = diameter * .19f;
            target->DrawLine(D2D1::Point2F(cx - note * .28f, cy - note * .5f),
                             D2D1::Point2F(cx - note * .28f, cy + note * .25f), brush.Get(), 2.4f);
            target->DrawLine(D2D1::Point2F(cx + note * .32f, cy - note * .65f),
                             D2D1::Point2F(cx + note * .32f, cy + note * .10f), brush.Get(), 2.4f);
            target->DrawLine(D2D1::Point2F(cx - note * .28f, cy - note * .5f),
                             D2D1::Point2F(cx + note * .32f, cy - note * .65f), brush.Get(), 3.5f);
            target->FillEllipse(
                D2D1::Ellipse(D2D1::Point2F(cx - note * .42f, cy + note * .3f), note * .22f, note * .13f), brush.Get());
            target->FillEllipse(
                D2D1::Ellipse(D2D1::Point2F(cx + note * .18f, cy + note * .15f), note * .22f, note * .13f),
                brush.Get());
            text(english ? L"MUSIC" : L"音乐", std::min(12.f, h * .065f), D2D1::RectF(8, h - 26, w - 8, h - 8),
                 0xC9DBE7FF);
            return;
        }
        text(english ? L"SPECTRUM" : L"频谱", std::min(11.f, h * .16f),
             D2D1::RectF(10, 4, w - 10, std::max(17.f, h * .23f)), 0xBFD0D8FF, false);
        const int count = spec.columns * 8;
        const float left = 10, right = w - 12, bottom = h - 13, upper = std::max(22.f, h * .28f),
                    plotHeight = std::max(2.f, bottom - upper), unit = (right - left) / count;
        ink(0xB6D5E710);
        for (int i = 0; i < 3; ++i)
            target->DrawLine(D2D1::Point2F(left, upper + i * plotHeight * .5f),
                             D2D1::Point2F(right, upper + i * plotHeight * .5f), brush.Get(), .5f);
        const D2D1_GRADIENT_STOP colors[]{{0, paint(top)}, {1, paint(color)}};
        const auto gradient = canvas.linearGradient({0, upper}, {0, bottom}, colors);
        for (int i = 0; i < count; ++i) {
            const double position = double(i) * (Spectrum::count - 1) / (count - 1);
            float level = spectrum.sample(position), peak = spectrum.sample(position, true);
            // Downsampling retains narrow musical peaks instead of averaging them
            // away.
            if (count < Spectrum::count) {
                const double half = double(Spectrum::count - 1) / count * .35;
                level = std::max({level, spectrum.sample(position - half), spectrum.sample(position + half)});
                peak = std::max({peak, spectrum.sample(position - half, true), spectrum.sample(position + half, true)});
            }
            const float x = left + i * unit, barWidth = std::max(.6f, unit * .63f),
                        barHeight = std::max(1.f, plotHeight * level);
            target->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(x, bottom - barHeight, x + barWidth, bottom),
                                  std::min(1.5f, barWidth * .4f), std::min(1.5f, barWidth * .4f)),
                gradient.Get());
            if (settings.at("peaks").integer && peak > .01f) {
                ink(0xECFAF5D8);
                const float y = bottom - peak * plotHeight - 4;
                target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, y, x + barWidth, y + 1.5f), .65f, .65f),
                                             brush.Get());
            }
        }
    }

    void photoImage(const Photo& photo, D2D1_RECT_F area, bool cover, float opacity) {
        if (!photo.bitmap) {
            return;
        }
        // Album and record callers already supply a rounded/circular geometry mask.
        canvas.image(*photo.bitmap, {area.left, area.top, area.right - area.left, area.bottom - area.top},
                     {cover ? cybercanvas::ImageFit::Cover : cybercanvas::ImageFit::Contain, 0, 1, opacity, false});
    }

#include "plugins/widget-suite/InputStatsViews.inl"
#include "plugins/widget-suite/MechanicalAlbumViews.inl"
#include "plugins/widget-suite/ObjectPlayerViews.inl"
#include "plugins/widget-suite/PlayerViews.inl"
#include "plugins/widget-suite/TonearmViews.inl"

    void album(const cdsdk::Settings& settings, const Photo& photo, const Photo& previous, float fade, float w,
               float h) {
        const float radius = std::clamp(float(settings.at("background_corner_radius").integer), 0.f,
                                        std::max(0.f, std::min(w - 4, h - 5) * .5f));
        card(w, h, radius, 0x192630FF);
        const auto area = D2D1::RectF(1, 1, w - 3, h - 4);
        ComPtr<ID2D1RoundedRectangleGeometry> mask;
        require(factory->CreateRoundedRectangleGeometry(D2D1::RoundedRect(area, radius, radius), mask.GetAddressOf()));
        canvas.masked(mask.Get(), [&] {
            const bool cover = settings.at("fit").text == "cover";
            if (previous.bitmap && fade < 1)
                photoImage(previous, area, cover, 1);
            if (photo.bitmap)
                photoImage(photo, area, cover, previous.bitmap ? fade : 1);
            else {
                const bool english = settings.at("language").text == "en";
                controls.icons.paint(canvas, {w * .38, h * .27, w * .24, h * .28}, "file-image", 0xB9CBD950);
                auto theme = cybercanvas::WidgetTheme::dark();
                theme.font = cdsdk::textStyle(*fonts, std::min(14.f, w * .052f));
                theme.padding = 0;
                theme.muted = 0xD6E2EAFF;
                cybercanvas::ContentNotice{{12, h * .6, w - 26, h * .18},
                                           {},
                                           cdsdk::displayText(english ? L"No readable photos" : L"暂无可显示图片"),
                                           cybercanvas::ContentState::Empty,
                                           false}
                    .draw(canvas, theme);
            }
            if (photo.bitmap && settings.at("counter").integer) {
                const auto caption = std::to_wstring(photo.index + 1) + L" / " + std::to_wstring(photo.count);
                const float size = std::min(11.f, h * .08f);
                ink(0x10202C80);
                target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(w - 78, h - 30, w - 12, h - 10), 6, 6),
                                             brush.Get());
                text(caption, size, D2D1::RectF(w - 77, h - 30, w - 12, h - 10), 0xFFFFFFFF);
            }
        });
    }

    void calendar(const cdsdk::Settings& settings, Date today, int offset, float w, float h) {
        const auto date = shiftMonth(today, offset);
        const bool english = settings.at("language").text == "en", monday = settings.at("week_start").text == "monday";
        const auto background = uint32_t(settings.at("background").integer),
                   accent = uint32_t(settings.at("accent").integer);
        const auto rgb = paint(background);
        const bool dark = rgb.r * .2126f + rgb.g * .7152f + rgb.b * .0722f < .5f;
        const uint32_t inkColor = dark ? 0xE2EDF3FF : 0x263039FF, muted = dark ? 0xA1B1BACC : 0x263039AA,
                       weekend = dark ? 0xE7AD88FF : 0x995C3EFF;
        card(w, h, float(settings.at("background_corner_radius").integer), background);
        const float pad = std::max(8.f, w * .035f), header = std::clamp(h * .18f, 22.f, 40.f),
                    weekHeight = std::clamp(h * .13f, 15.f, 26.f), cell = (w - 2 * pad) / 7,
                    row = (h - header - weekHeight - 10) / 6;
        const wchar_t* months[]{L"",     L"January", L"February",  L"March",   L"April",    L"May",     L"June",
                                L"July", L"August",  L"September", L"October", L"November", L"December"};
        const auto title = english ? std::wstring(months[date.month]) + L" " + std::to_wstring(date.year)
                                   : std::to_wstring(date.year) + L"年" + std::to_wstring(date.month) + L"月";
        text(title, std::clamp(header * .53f, 11.f, 20.f), D2D1::RectF(pad, 4, w - 65, header), inkColor, false);
        auto theme = cybercanvas::WidgetTheme::dark();
        theme.foreground = inkColor;
        theme.surface = 0;
        for (uint32_t i = 0; i < 2; ++i) {
            cybercanvas::IconButton button(i + 1, {w - (i ? 31. : 57.), 0, i ? 31. : 26., header},
                                           i ? "chevron-right" : "chevron-left");
            button.iconSize = 14;
            button.draw(canvas, theme, controls.icons.painter(), controls.visual(i + 1));
        }
        const wchar_t* zh[]{L"日", L"一", L"二", L"三", L"四", L"五", L"六"};
        const wchar_t* en[]{L"Su", L"Mo", L"Tu", L"We", L"Th", L"Fr", L"Sa"};
        for (int i = 0; i < 7; ++i) {
            const int weekday = (i + (monday ? 1 : 0)) % 7;
            text(english ? en[weekday] : zh[weekday], std::clamp(weekHeight * .56f, 8.f, 13.f),
                 D2D1::RectF(pad + i * cell, header, pad + (i + 1) * cell, header + weekHeight),
                 weekday == 0 || weekday == 6 ? weekend : muted);
        }
        const int start = monthStart(date, monday), days = monthDays(date);
        const float size = std::clamp(std::min(row * .65f, cell * .42f), 8.f, 15.f),
                    radius = std::max(3.f, std::min(row, cell) * .40f);
        cybercanvas::CalendarGrid grid{{pad, header + weekHeight, w - 2 * pad, row * 6}, 6};
        for (int i = 0; i < 42; ++i) {
            const int day = i - start + 1;
            const auto area = grid.cell(size_t(i));
            const float x = float(area.x), y = float(area.y);
            const bool current = day >= 1 && day <= days;
            int number = day;
            if (!current) {
                if (!settings.at("adjacent_days").integer)
                    continue;
                number = day < 1 ? monthDays(shiftMonth(date, -1)) + day : day - days;
            }
            const bool active = current && offset == 0 && day == today.day;
            const int weekday = (i % 7 + (monday ? 1 : 0)) % 7;
            const uint32_t color = active                         ? 0xFFFFFFFF
                                   : !current                     ? (dark ? 0xA1B1BA50 : 0x26303950)
                                   : weekday == 0 || weekday == 6 ? weekend
                                                                  : inkColor;
            cybercanvas::CalendarCell dateCell;
            dateCell.bounds = area;
            dateCell.day = cdsdk::label(*fonts, std::to_wstring(number), size, D2D1::RectF(x, y, x + cell, y + row),
                                        color, true, true);
            dateCell.selected = active;
            dateCell.current = current;
            dateCell.highlight = {x + cell * .5 - radius, y + row * .5 - radius, radius * 2, radius * 2};
            dateCell.radius = radius;
            dateCell.selection = accent;
            dateCell.draw(canvas);
        }
    }
};

Canvas::Canvas() : impl(std::make_unique<Impl>()) {}

Canvas::~Canvas() = default;

void Canvas::bind(const CdCreateContext& context, std::function<void(uint32_t)> action) {
    impl->controls.bind(context, std::move(action));
    impl->canvas.bindGpu(context);
    impl->artwork.bind(context);
    impl->invalidate = [host = *context.host, pointer = context.host_context] {
        host.request_redraw(pointer);
    };
}

void Canvas::pause(bool paused) {
    impl->artwork.pause(paused);
}

void Canvas::input(const CdInput& input) {
    auto& p = *impl;
    if (p.mechanicalAlbumInput) {
        const auto r = p.albumInputBounds;
        const bool hovered = input.kind != CD_INPUT_LEAVE && input.kind != CD_INPUT_CANCEL && input.x >= r.x &&
                             input.y >= r.y && input.x < r.x + r.width && input.y < r.y + r.height;
        if (hovered != p.albumHovered) {
            p.albumHovered = hovered;
            if (p.invalidate) {
                p.invalidate();
            }
        }
        if (input.kind == CD_INPUT_LEAVE || input.kind == CD_INPUT_CANCEL) {
            p.controls.cancel();
        }
    }
    impl->controls.input(input);
}

void Canvas::cancelInput() {
    impl->controls.cancel();
    impl->albumHovered = false;
}

void Canvas::syncInput(Spec spec, const Media& media, CdRect safe, const Photo& photo, AlbumPlayback album) {
    auto& p = *impl;
    p.mechanicalAlbumInput = spec.mechanical && spec.kind == Kind::Album;
    p.albumInputBounds = safe;
    std::vector<cdsdk::Controls::Item> items;
    if (spec.kind == Kind::Player) {
        const auto layout = arrangePlayer(spec, float(safe.width), float(safe.height));
        constexpr uint32_t flags[]{CD_MEDIA_CAN_PREVIOUS, CD_MEDIA_CAN_TOGGLE, CD_MEDIA_CAN_NEXT};
        for (int i = 0; i < 3; ++i) {
            const auto r = layout.buttons[i];
            items.push_back(
                {uint32_t(i + 1),
                 {r.x, r.y, r.width, r.height},
                 i == 0                             ? "Previous track"
                 : i == 2                           ? "Next track"
                 : media.status == CD_MEDIA_PLAYING ? "Pause"
                                                    : "Play",
                 !media.source.empty() && (media.controls & flags[i]) && media.commandResult != CD_ERROR_BUSY});
        }
    } else if (p.mechanicalAlbumInput) {
        const auto layout = arrangeAlbum(safe.width, safe.height);
        for (unsigned i = 0; i < 3; ++i) {
            const char* name = p.albumEnglish ? (i == 0         ? "Previous photo"
                                                 : i == 2       ? "Next photo"
                                                 : album.paused ? "Resume slideshow"
                                                                : "Pause slideshow")
                                              : (i == 0         ? "上一张"
                                                 : i == 2       ? "下一张"
                                                 : album.paused ? "继续轮播"
                                                                : "暂停轮播");
            items.push_back({i + 1, layout.buttons[i], name,
                             bool(photo.bitmap) && (i == 1 || (photo.count > 1 && !album.pending))});
        }
    } else if (spec.kind == Kind::Calendar) {
        const double header = std::clamp(safe.height * .18, 22., 40.);
        items = {{1, {safe.width - 57, 0, 26, header}, "Previous month"},
                 {2, {safe.width - 31, 0, 31, header}, "Next month"}};
    }
    p.controls.items(std::move(items), {safe.x, safe.y});
}

bool Canvas::render(Spec spec, const cdsdk::Settings& settings, const Spectrum& spectrum, const Photo& photo,
                    const Photo& previous, float fade, Date date, int offset, const CdFrame& frame, CdRect safe,
                    const Media& media, int pressed, const CdInputStats& inputStats, const CounterDrums* counterDrums,
                    AlbumPlayback albumPlayback) {
    const cdsdk::LanguageScope languageScope(settings.at("language").text);
    try {
        auto& p = *impl;
        if (!p.canvas.valid(frame)) {
            return false;
        }
        p.surface(frame);
        p.fonts->select(p.write.Get(), settings.at("font_family").text);
        p.canvas.fontCollection(p.fonts->collection());

        const double width = frame.width / frame.device_pixel_ratio, height = frame.height / frame.device_pixel_ratio;
        if (safe.width <= 0 || safe.height <= 0)
            safe = {0, 0, width, height};
        safe.x = std::clamp(safe.x, 0.0, width);
        safe.y = std::clamp(safe.y, 0.0, height);
        safe.width = std::clamp(safe.width, 0.0, width - safe.x);
        safe.height = std::clamp(safe.height, 0.0, height - safe.y);

        p.albumEnglish = settings.at("language").text == "en";
        syncInput(spec, media, safe, photo, albumPlayback);
        auto painting = p.canvas.frame(frame, safe);
        if (safe.width > 12 && safe.height > 12) {
            if (spec.kind == Kind::InputStats) {
                if (spec.mechanical) {
                    drawMechanicalInputCounter(p.canvas, p.artwork, inputStats, counterDrums, safe.width, safe.height,
                                               GetTickCount64());
                } else {
                    p.inputStats(settings, inputStats, float(safe.width), float(safe.height));
                }
            } else if (spec.kind == Kind::Player)
                p.player(spec, settings, media, pressed, float(safe.width), float(safe.height));
            else if (spec.kind == Kind::Album) {
                if (spec.mechanical) {
                    p.mechanicalAlbum(settings, photo, previous, fade, albumPlayback, safe.width, safe.height);
                } else {
                    p.album(settings, photo, previous, fade, float(safe.width), float(safe.height));
                }
            } else if (spec.kind == Kind::Calendar)
                p.calendar(settings, date, offset, float(safe.width), float(safe.height));
            else
                p.music(spec, settings, spectrum, float(safe.width), float(safe.height));
        }
        auto focusTheme = cybercanvas::WidgetTheme::dark();
        if (p.mechanicalAlbumInput) {
            focusTheme.accent = mechanicalart::kBrass;
            focusTheme.radius = arrangeAlbum(safe.width, safe.height).buttons[0].width / 2;
        }
        p.controls.drawFocus(p.canvas, focusTheme);
        painting.finish();
        return true;
    } catch (...) {
        return false;
    }
}

bool Canvas::savePng(const std::wstring& path, const CdFrame& frame) {
    return impl->canvas.save(path, frame);
}

bool Canvas::pendingImages() const {
    return impl->artwork.pending();
}
}  // namespace widgetplugin
