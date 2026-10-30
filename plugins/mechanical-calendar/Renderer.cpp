#include <sdk/WindowsFonts.h>

#include <algorithm>
#include <cmath>
#include <optional>

#include "plugins/mechanical-calendar/Calendar.h"

namespace mechanicalcalendar {
using cybercanvas::Canvas;
using cybercanvas::Rect;
using Microsoft::WRL::ComPtr;

namespace {
uint32_t shade(uint32_t rgba, double scale, double lift = 0) {
    const auto channel = [&](unsigned shift) {
        return uint32_t(std::clamp(((rgba >> shift) & 255) * scale + lift, 0., 255.));
    };
    return (channel(24) << 24) | (channel(16) << 16) | (channel(8) << 8) | (rgba & 255);
}

uint32_t opacity(uint32_t rgba, double value) {
    return (rgba & 0xffffff00) | uint32_t((rgba & 255) * value);
}
}  // namespace

struct Renderer::Impl {
    struct Key {
        Month month;
        double width, height, dpi;
        std::array<uint32_t, 4> colors;
        std::string font, language;
        bool adjacent;
        const cybercanvas::ImageData* body;
        bool operator==(const Key&) const = default;
    };

    cdsdk::Canvas canvas;
    cdsdk::Icons icons;
    cdsdk::Images images;
    cdsdk::WindowsFonts fonts{canvas.nativeTextFactory()};
    std::filesystem::path path;
    std::shared_ptr<const cybercanvas::ImageData> body;
    cybercanvas::SurfaceImage background;
    ComPtr<ID2D1RenderTarget> owner;
    std::optional<Key> key;
    Geometry geometry;
    uint64_t builds = 0;

    explicit Impl(const CdCreateContext& context) {
        icons.bind(context);
        images.bind(context);
        canvas.bindGpu(context);
        path = std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(context.plugin_directory))) /
               "assets" / "body.png";
    }

    void label(Canvas& layer, std::wstring_view text, Rect bounds, double size, uint32_t color,
               bool centerInk = false) {
        cybercanvas::TextStyle style;
        style.family = fonts.family();
        style.locale = key->language == "en" ? L"en-US" : key->language == "zh-TW" ? L"zh-TW" : L"zh-CN";
        style.size = std::max(1., size);
        style.align = cybercanvas::TextAlign::Center;
        style.vertical = cybercanvas::VerticalAlign::Center;
        style.bold = true;
        style.tabularFigures = true;
        if (centerInk) {
            const auto fitted = layer.fitTextInk(std::wstring(text), style, bounds);
            layer.brush->SetColor(cybercanvas::color(color));
            layer.brush->SetOpacity(1);
            layer.target->DrawTextLayout(D2D1::Point2F(float(fitted.origin.x), float(fitted.origin.y)),
                                         fitted.layout.Get(), layer.brush.Get());
        } else {
            layer.text(std::wstring(text), style, bounds, color);
        }
    }

    void prepare(const Month& month, const cdsdk::Settings& settings, double width, double height, double dpi) {
        const auto nextGeometry = arrange(width, height, month.weeks);
        const auto edge = uint32_t(std::clamp(std::ceil(nextGeometry.body.width * dpi / 64) * 64, 64., 4096.));
        images.set("body", path, 1, edge);
        images.poll();
        if (const auto next = images.get("body").image) {
            body = next;
        }
        const Key next{month,
                       width,
                       height,
                       dpi,
                       {uint32_t(settings.at("accent").integer), uint32_t(settings.at("weekday_color").integer),
                        uint32_t(settings.at("weekend_color").integer), uint32_t(settings.at("tile_color").integer)},
                       settings.at("font_family").text,
                       settings.at("language").text,
                       settings.at("adjacent_days").integer != 0,
                       body.get()};
        if (key == next && owner == canvas.target) {
            return;
        }
        geometry = nextGeometry;
        owner.Reset();
        key = next;
        fonts.select(canvas.nativeTextFactory(), next.font);
        canvas.fontCollection(fonts.collection());
        const auto& g = geometry;
        const auto [accent, ink, weekend, tile] = next.colors;
        cybercanvas::OffscreenSurface surface;
        canvas.renderLayer(
            surface, {uint32_t(std::ceil(width * dpi)), uint32_t(std::ceil(height * dpi)), dpi}, [&](Canvas& layer) {
                if (body) {
                    layer.image(*body, g.body, {.fit = cybercanvas::ImageFit::Stretch, .clip = false});
                }
                const bool english = next.language == "en";
                constexpr const wchar_t* zh[]{L"日", L"一", L"二", L"三", L"四", L"五", L"六"};
                constexpr const wchar_t* en[]{L"Su", L"Mo", L"Tu", L"We", L"Th", L"Fr", L"Sa"};
                const double gap = std::max(.45 / dpi, g.scale * 2.2);
                for (unsigned row = 0; row <= month.weeks; ++row) {
                    for (unsigned column = 0; column < 7; ++column) {
                        const auto area = g.cell(column, row);
                        const Rect face{area.x + gap / 2, area.y + gap / 2, area.width - gap, area.height - gap};
                        layer.gradientBox(face, 3.5 * g.scale, opacity(shade(tile, 1.06), .92),
                                          opacity(shade(tile, .83), .92));
                        layer.strokeBox(face, 3.5 * g.scale, opacity(shade(tile, 1., 36), .45),
                                        {.width = float(std::max(.3, g.scale))});
                        if (row == 0) {
                            const unsigned weekday = (column + (month.monday ? 1 : 0)) % 7;
                            label(layer, english ? en[weekday] : zh[weekday], area, g.rowHeight * .53,
                                  weekday == 0 || weekday == 6 ? weekend : ink);
                            continue;
                        }
                        const auto& day = month.cells[(row - 1) * 7 + column];
                        if (!day.current && !next.adjacent) {
                            continue;
                        }
                        uint32_t color = day.weekend ? weekend : ink;
                        if (!day.current) {
                            color = opacity(color, .38);
                        }
                        if (day.today && (accent & 255)) {
                            layer.strokeBox(face, 3.5 * g.scale, accent,
                                            {.width = float(std::max(1. / dpi, 1.8 * g.scale))});
                        }
                        const auto text = std::to_wstring(day.date.day);
                        label(layer, text, area, std::min(g.rowHeight * .65, g.columnWidth * .42), color);
                    }
                }
            });
        background = surface.snapshot();
        owner = canvas.target;
        ++builds;
    }

    void draw(const Month& month, const Visual& visual) {
        const auto extent = background->GetSize();
        canvas.target->DrawBitmap(background.Get(), D2D1::RectF(0, 0, extent.width, extent.height));
        const auto& g = geometry;
        const auto current = headerDigits(month.date), previous = headerDigits(visual.previous);
        const auto ink = key->colors[1];
        for (unsigned i = 0; i < g.wheels.size(); ++i) {
            const auto area = g.wheels[i];
            const double size = std::min(area.height * .78, area.width * 1.25);
            canvas.clipped({area.x, area.y, area.width, area.height}, [&] {
                if (visual.progress >= 1 || current[i] == previous[i]) {
                    label(canvas, current.substr(i, 1), area, size, ink, true);
                } else {
                    auto old = area, next = area;
                    old.y -= visual.progress * area.height * visual.direction;
                    next.y += (1 - visual.progress) * area.height * visual.direction;
                    label(canvas, previous.substr(i, 1), old, size, ink, true);
                    label(canvas, current.substr(i, 1), next, size, ink, true);
                }
            });
        }
        for (unsigned i = 0; i < g.buttons.size(); ++i) {
            const auto bounds = g.buttons[i];
            const bool pressed = visual.pressed == i + 1, hover = visual.hovered == i + 1;
            const double radius = g.scale * 8;
            canvas.gradientBox(bounds, radius,
                               pressed ? 0x8f612fff
                               : hover ? 0xf2d69fff
                                       : 0x444b4bff,
                               pressed ? 0xbe914dff
                               : hover ? 0xb98c49ff
                                       : 0x202626ff);
            canvas.strokeBox(bounds, radius, hover || pressed ? 0xffdfa2ff : 0xb6bbaaff,
                             {.width = float(std::max(1. / key->dpi, 2 * g.scale))});
            const double side = std::min(bounds.width, bounds.height) * .94;
            const Rect icon{bounds.x + (bounds.width - side) / 2, bounds.y + (bounds.height - side) / 2, side, side};
            const uint32_t color = pressed ? 0xfff5dcff : hover ? 0x241c12ff : 0xfff5dcff;
            if (!icons.paint(canvas, icon, i ? "chevron-right" : "chevron-left", color)) {
                // Older or standalone hosts get readable labels, never font-symbol icons.
                label(canvas, key->language == "en" ? (i ? L"Next" : L"Prev") : (i ? L"下月" : L"上月"), bounds,
                      bounds.height * .25, color);
            }
        }
    }
};

Renderer::Renderer(const CdCreateContext& context) : impl_(std::make_unique<Impl>(context)) {}

Renderer::~Renderer() = default;

void Renderer::onChange(std::function<void()> callback) {
    impl_->images.onChange(std::move(callback));
}

void Renderer::pause(bool paused) {
    impl_->images.pause(paused);
}

bool Renderer::ready() const {
    return impl_->body && !impl_->images.pending() && impl_->images.get("body").image == impl_->body;
}

bool Renderer::render(const Month& month, const cdsdk::Settings& settings, const Visual& visual, const CdFrame& frame,
                      CdRect safe) {
    if (!impl_->canvas.valid(frame) || month.weeks < 4 || month.weeks > 6) {
        return false;
    }
    impl_->canvas.render(frame, safe, [&](auto&, CdRect area) {
        impl_->prepare(month, settings, area.width, area.height, frame.device_pixel_ratio);
        impl_->draw(month, visual);
    });
    return true;
}

bool Renderer::save(const std::filesystem::path& path, const CdFrame& frame) {
    return impl_->canvas.save(path.wstring(), frame);
}

uint64_t Renderer::cacheBuilds() const {
    return impl_->builds;
}
}  // namespace mechanicalcalendar
