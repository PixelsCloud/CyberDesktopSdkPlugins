#include <sdk/WindowsFonts.h>

#include <algorithm>
#include <cmath>
#include <optional>

#include "plugins/pixel-desk/Calendar.h"
#include "plugins/pixel-desk/PixelImages.h"
#include "plugins/pixel-desk/PixelLayout.h"
#include "plugins/pixel-desk/PixelParts.h"

namespace pixeldesk {
using cybercanvas::Canvas;
using cybercanvas::Rect;
using Microsoft::WRL::ComPtr;

cdsdk::Settings calendarDefaults() {
    auto values = mechanicalcalendar::defaults();
    const CdSettingValue colors[]{{sizeof(CdSettingValue), CD_SETTING_COLOR, "accent", kMint},
                                  {sizeof(CdSettingValue), CD_SETTING_COLOR, "weekday_color", kInk},
                                  {sizeof(CdSettingValue), CD_SETTING_COLOR, "weekend_color", 0xb55d3fff},
                                  {sizeof(CdSettingValue), CD_SETTING_COLOR, "tile_color", kCream}};
    values.apply(colors, unsigned(std::size(colors)));
    return values;
}

CalendarGeometry calendarGeometry(double width, double height, unsigned weeks) {
    CalendarGeometry g;
    const auto bounds = fitBody(width, height, 1629, 1113);
    g.scale = bounds.width / 1629;
    const double x = bounds.x - 6 * g.scale, y = bounds.y - 6 * g.scale;
    const auto map = [&](Rect r) {
        return Rect{x + r.x * g.scale, y + r.y * g.scale, r.width * g.scale, r.height * g.scale};
    };
    g.body = map({0, 0, 1641, 1125});
    g.grid = map({105, 294, 1431, 657});
    g.header = map({480, 114, 660, 138});
    g.buttons = {map({315, 165, 108, 96}), map({1152, 165, 108, 96})};
    g.columnWidth = g.grid.width / 7;
    g.rowHeight = g.grid.height / (std::clamp(weeks, 4u, 6u) + 1);
    return g;
}

Rect CalendarGeometry::cell(unsigned column, unsigned row) const {
    return {grid.x + column * columnWidth, grid.y + row * rowHeight, columnWidth, rowHeight};
}

unsigned calendarButtonAt(const CalendarGeometry& geometry, cybercanvas::Point point) {
    for (unsigned i = 0; i < geometry.buttons.size(); ++i) {
        const auto& r = geometry.buttons[i];
        if (point.x >= r.x && point.x < r.x + r.width && point.y >= r.y && point.y < r.y + r.height) {
            return i + 1;
        }
    }
    return 0;
}

namespace {
void skin(Canvas& canvas, const cybercanvas::ImageData& body, Rect area) {
    canvas.image(
        body, area,
        {.fit = cybercanvas::ImageFit::Stretch, .clip = false, .sampling = cybercanvas::ImageSampling::Nearest});
}

std::wstring title(CalendarDate date) {
    auto digits = mechanicalcalendar::headerDigits(date);
    digits.insert(4, 1, L'.');
    return digits;
}
}  // namespace

struct CalendarRenderer::Impl {
    struct Key {
        CalendarMonth month;
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
    CalendarGeometry geometry;
    uint64_t builds = 0;

    explicit Impl(const CdCreateContext& context) {
        icons.bind(context);
        images.bind(context);
        canvas.bindGpu(context);
        path = std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(context.plugin_directory))) /
               "assets" / "body.png";
    }

    void label(Canvas& layer, std::wstring text, Rect area, double size, uint32_t color) {
        cybercanvas::TextStyle style;
        style.family = fonts.family();
        style.locale = key->language == "en" ? L"en-US" : key->language == "zh-TW" ? L"zh-TW" : L"zh-CN";
        style.size = std::max(1., size);
        style.bold = true;
        style.tabularFigures = true;
        style.align = cybercanvas::TextAlign::Center;
        style.vertical = cybercanvas::VerticalAlign::Center;
        const auto fitted = layer.fitTextInk(text, style, area);
        layer.brush->SetColor(cybercanvas::color(color));
        layer.brush->SetOpacity(1);
        layer.target->DrawTextLayout(D2D1::Point2F(float(fitted.origin.x), float(fitted.origin.y)), fitted.layout.Get(),
                                     layer.brush.Get());
    }

    void prepare(const CalendarMonth& month, const cdsdk::Settings& settings, double width, double height, double dpi) {
        const auto g = calendarGeometry(width, height, month.weeks);
        const auto edge = uint32_t(std::clamp(std::ceil(g.body.width * dpi / 64) * 64, 64., 4096.));
        pixeldesk::requestPixelImage(images, "body", path, 1, edge, dpi);
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
        geometry = g;
        key = next;
        fonts.select(canvas.nativeTextFactory(), next.font);
        canvas.fontCollection(fonts.collection());
        const auto [accent, ink, weekend, tile] = next.colors;
        cybercanvas::OffscreenSurface surface;
        canvas.renderLayer(
            surface, {uint32_t(std::ceil(width * dpi)), uint32_t(std::ceil(height * dpi)), dpi}, [&](Canvas& layer) {
                if (body) {
                    skin(layer, *body, g.body);
                }
                // Replace the empty paper with native color, keeping the case corners
                // and clips intact. Every grid row is generated from the real month.
                layer.box(g.body.x + 105 * g.scale, g.body.y + 105 * g.scale, 1431 * g.scale, 846 * g.scale, 0, tile);
                const bool english = next.language == "en";
                constexpr const wchar_t* zh[]{L"日", L"一", L"二", L"三", L"四", L"五", L"六"};
                constexpr const wchar_t* en[]{L"S", L"M", L"T", L"W", L"T", L"F", L"S"};
                const double line = std::max(.45 / dpi, g.scale * 3), gap = 4 * g.scale;
                for (unsigned row = 0; row <= month.weeks; ++row) {
                    for (unsigned column = 0; column < 7; ++column) {
                        const auto area = g.cell(column, row);
                        const Rect face{area.x + gap, area.y + gap, area.width - gap * 2, area.height - gap * 2};
                        layer.strokeBox(area, 0, 0xd9c5a4ff, {.width = float(line)});
                        if (!row) {
                            const unsigned weekday = (column + (month.monday ? 1 : 0)) % 7;
                            label(layer, english ? en[weekday] : zh[weekday], face, g.rowHeight * .51,
                                  weekday == 0 || weekday == 6 ? weekend : ink);
                            continue;
                        }
                        const auto& day = month.cells[(row - 1) * 7 + column];
                        if (!day.current && !next.adjacent) {
                            continue;
                        }
                        if (day.today && (accent & 255)) {
                            layer.box(face.x, face.y, face.width, face.height, 0, accent);
                            layer.strokeBox(face, 0, 0x4f9586ff, {.width = float(line * 1.5)});
                        }
                        uint32_t color = day.weekend ? weekend : ink;
                        if (!day.current) {
                            color = (color & 0xffffff00) | 100;
                        }
                        label(layer, std::to_wstring(day.date.day), face,
                              std::min(g.rowHeight * .58, g.columnWidth * .39), color);
                    }
                }
            });
        background = surface.snapshot();
        owner = canvas.target;
        ++builds;
    }

    void draw(const CalendarMonth& month, const CalendarVisual& visual) {
        canvas.target->DrawBitmap(background.Get(), D2D1::RectF(0, 0, float(key->width), float(key->height)), 1,
                                  D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
        const auto& g = geometry;
        const auto ink = key->colors[1];
        canvas.clipped({g.header.x, g.header.y, g.header.width, g.header.height}, [&] {
            if (visual.progress >= 1 || visual.previous == month.date) {
                label(canvas, title(month.date), g.header, g.header.height * .72, ink);
            } else {
                auto old = g.header, next = g.header;
                old.y -= visual.progress * g.header.height * visual.direction;
                next.y += (1 - visual.progress) * g.header.height * visual.direction;
                label(canvas, title(visual.previous), old, g.header.height * .72, ink);
                label(canvas, title(month.date), next, g.header.height * .72, ink);
            }
        });
        for (unsigned i = 0; i < 2; ++i) {
            auto r = g.buttons[i];
            const bool pressed = visual.pressed == i + 1, hovered = visual.hovered == i + 1;
            stepped(canvas, r, 7 * g.scale, kInk);
            r = {r.x + 5 * g.scale, r.y + 5 * g.scale, r.width - 10 * g.scale, r.height - 10 * g.scale};
            stepped(canvas, r, 5 * g.scale, pressed ? kClay : hovered ? 0xb6e5c6ff : kMint);
            canvas.line(r.x + 5 * g.scale, r.y + 5 * g.scale, r.x + r.width - 5 * g.scale, r.y + 5 * g.scale, kCream,
                        5 * g.scale);
            const double side = std::min(r.width, r.height) * .72;
            const Rect icon{r.x + (r.width - side) / 2, r.y + (r.height - side) / 2, side, side};
            if (!icons.paint(canvas, icon, i ? "chevron-right" : "chevron-left", pressed ? kCream : kInk)) {
                label(canvas, key->language == "en" ? (i ? L"Next" : L"Prev") : (i ? L"下月" : L"上月"), r,
                      r.height * .35, pressed ? kCream : kInk);
            }
        }
    }
};

CalendarRenderer::CalendarRenderer(const CdCreateContext& context) : impl_(std::make_unique<Impl>(context)) {}

CalendarRenderer::~CalendarRenderer() = default;

void CalendarRenderer::onChange(std::function<void()> callback) {
    impl_->images.onChange(std::move(callback));
}

void CalendarRenderer::pause(bool paused) {
    impl_->images.pause(paused);
}

bool CalendarRenderer::ready() const {
    return impl_->body && !impl_->images.pending() && impl_->images.get("body").image == impl_->body;
}

bool CalendarRenderer::render(const CalendarMonth& month, const cdsdk::Settings& settings, const CalendarVisual& visual,
                              const CdFrame& frame, CdRect safe) {
    if (!impl_->canvas.valid(frame) || month.weeks < 4 || month.weeks > 6) {
        return false;
    }
    impl_->canvas.render(frame, safe, [&](auto&, CdRect area) {
        impl_->prepare(month, settings, area.width, area.height, frame.device_pixel_ratio);
        impl_->draw(month, visual);
    });
    return true;
}

bool CalendarRenderer::save(const std::filesystem::path& path, const CdFrame& frame) {
    return impl_->canvas.save(path.wstring(), frame);
}

uint64_t CalendarRenderer::cacheBuilds() const {
    return impl_->builds;
}
}  // namespace pixeldesk
