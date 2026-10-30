#include <sdk/Language.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>

#include "plugins/pixel-desk/LunarCalendar.h"
#include "plugins/pixel-desk/PixelDigits.h"
#include "plugins/pixel-desk/PixelImages.h"
#include "plugins/pixel-desk/PixelLayout.h"
#include "plugins/pixel-desk/PixelParts.h"

namespace pixeldesk {
using cybercanvas::Canvas;
using cybercanvas::Rect;
using Microsoft::WRL::ComPtr;

Rect LunarViewport::map(Rect area) const {
    return {origin.x + area.x * scale, origin.y + area.y * scale, area.width * scale, area.height * scale};
}

LunarViewport lunarViewport(CdRect safe, const LunarSpec& spec) {
    const auto bounds = fitBody(safe.width, safe.height, spec.visible.width, spec.visible.height);
    const double scale = bounds.width / spec.visible.width;
    return {{safe.x + bounds.x - spec.visible.x * scale, safe.y + bounds.y - spec.visible.y * scale}, scale};
}

Rect lunarCell(const LunarSpec& spec, const LunarViewport& viewport, unsigned index, unsigned rows) {
    if (rows < 4 || rows > 6 || index >= rows * 7) {
        return {};
    }
    const double column = spec.grid.width / 7, height = spec.grid.height / rows;
    return viewport.map({spec.grid.x + (index % 7) * column, spec.grid.y + (index / 7) * height, column, height});
}

namespace {
std::string dateText(lunarplugin::Date date, bool day) {
    char text[24]{};
    if (day) {
        sprintf_s(text, "%04d.%02d.%02d", date.year, date.month, date.day);
    } else {
        sprintf_s(text, "%04d.%02d", date.year, date.month);
    }
    return text;
}

std::wstring lunarText(const lunarplugin::Lunar& value, bool english) {
    if (english) {
        return L"Lunar " + std::to_wstring(value.month) + L"/" + std::to_wstring(value.day) +
               (value.leap ? L" (leap)" : L"");
    }
    const auto month = value.leap && cdsdk::currentUiLanguage == cdsdk::UiLanguage::Traditional
                           ? L"閏" + cdsdk::displayText(value.monthName.substr(1))
                           : cdsdk::displayText(value.monthName);
    return month + cdsdk::displayText(value.dayName);
}

std::wstring weekday(lunarplugin::Date date, bool english) {
    constexpr const wchar_t* zh[]{L"星期日", L"星期一", L"星期二", L"星期三", L"星期四", L"星期五", L"星期六"};
    constexpr const wchar_t* en[]{L"Sunday", L"Monday", L"Tuesday", L"Wednesday", L"Thursday", L"Friday", L"Saturday"};
    return english ? en[lunarplugin::weekday(date)] : zh[lunarplugin::weekday(date)];
}

std::wstring specialText(const lunarplugin::Lunar& lunar, bool english) {
    if (english && !lunar.festival.empty()) {
        // Short display names keep the compact physical badge readable. The
        // underlying festival identity still comes from the original calendar.
        constexpr std::pair<std::wstring_view, const wchar_t*> names[]{
            {L"春节", L"Spring Festival"}, {L"除夕", L"New Year's Eve"}, {L"元宵", L"Lantern Festival"},
            {L"端午", L"Dragon Boat"},     {L"七夕", L"Qixi"},           {L"中秋", L"Mid-Autumn"},
            {L"重阳", L"Double Ninth"},    {L"腊八", L"Laba"},           {L"清明", L"Qingming"}};
        for (const auto& [festival, text] : names) {
            if (lunar.festival == festival) {
                return text;
            }
        }
    }
    return cdsdk::displayText(lunar.festival.empty() ? lunar.term : lunar.festival);
}

void label(Canvas& canvas, std::wstring text, Rect area, double size, uint32_t ink, bool numeric = false) {
    if (text.empty() || area.width <= 0 || area.height <= 0) {
        return;
    }
    cybercanvas::TextStyle style;
    style.family = numeric ? L"Consolas" : L"Microsoft YaHei UI";
    style.locale = cdsdk::drawingLocale();
    style.size = std::max(1., size);
    style.bold = true;
    style.align = cybercanvas::TextAlign::Center;
    style.vertical = cybercanvas::VerticalAlign::Center;
    const auto fitted = canvas.fitTextInk(text, style, area);
    canvas.brush->SetColor(cybercanvas::color(ink));
    canvas.brush->SetOpacity(1);
    canvas.target->DrawTextLayout(D2D1::Point2F(float(fitted.origin.x), float(fitted.origin.y)), fitted.layout.Get(),
                                  canvas.brush.Get());
}

void rollingDigits(Canvas& canvas, std::string current, std::string previous, Rect area, lunarplugin::View view,
                   uint32_t ink) {
    canvas.clipped(area, [&] {
        if (view.progress >= 1 || current == previous) {
            pixelDigits(canvas, current, area, ink);
        } else {
            auto old = area, next = area;
            old.y -= view.progress * area.height * view.direction;
            next.y += (1 - view.progress) * area.height * view.direction;
            pixelDigits(canvas, previous, old, ink);
            pixelDigits(canvas, current, next, ink);
        }
    });
}
}  // namespace

struct LunarRenderer::Impl {
    struct Key {
        lunarplugin::Date selected, current;
        double width, height, dpi;
        std::string language, weekStart;
        uint32_t ink, accent;
        bool adjacent;
        const cybercanvas::ImageData* body;
        bool operator==(const Key&) const = default;
    };

    const LunarSpec& spec;
    cdsdk::Canvas canvas;
    cdsdk::Images images;
    cdsdk::Controls controls;
    std::function<void(uint32_t)> action;
    std::filesystem::path path;
    std::shared_ptr<const cybercanvas::ImageData> body;
    std::optional<Key> key;
    cybercanvas::SurfaceImage face;
    ComPtr<ID2D1RenderTarget> owner;
    LunarViewport viewport;
    uint64_t builds = 0;

    Impl(const CdCreateContext& context, const LunarSpec& value) : spec(value) {
        images.bind(context);
        canvas.bindGpu(context);
        controls.bind(context, [this](uint32_t id) {
            if (action) {
                action(id);
            }
        });
        path = std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(context.plugin_directory))) /
               "assets" / "body.png";
    }

    void syncInput(const cdsdk::Settings& settings, lunarplugin::View date, CdRect safe) {
        if (spec.rows == 1 || safe.width <= 0 || safe.height <= 0) {
            controls.items({});
            return;
        }
        const auto view = lunarViewport(safe, spec);
        std::vector<cdsdk::Controls::Item> items{{1, view.map(spec.buttons[0]), "Previous month"},
                                                 {2, view.map(spec.buttons[1]), "Next month"}};
        if (spec.rows >= 4) {
            items.push_back({3, view.map(spec.today), "Today"});
            const auto grid = lunarplugin::monthGrid(date.selected, settings.at("week_start").text == "monday");
            for (unsigned i = 0; i < unsigned(grid.rows * 7); ++i) {
                const auto day = grid.dates[i];
                if (!lunarplugin::valid(day) ||
                    (day.month != date.selected.month && !settings.at("adjacent_days").integer)) {
                    continue;
                }
                items.push_back({100 + i, lunarCell(spec, view, i, unsigned(grid.rows)), dateText(day, true), true,
                                 true, day == date.selected});
            }
        }
        controls.items(std::move(items));
    }

    void monthly(Canvas& layer, lunarplugin::View view, const lunarplugin::Lunar& selected,
                 const std::wstring& special) {
        const bool english = key->language == "en", monday = key->weekStart == "monday";
        // Deep clay ink improves contrast on light paper. Solid badges retain
        // their original clay fill; explicitly chosen accent colors are preserved.
        const uint32_t accentInk = key->accent == kClay ? 0xb55d3fff : key->accent;
        const auto grid = lunarplugin::monthGrid(view.selected, monday);
        const auto weeks = viewport.map(spec.weeks);
        const double columnWidth = weeks.width / 7;
        constexpr const wchar_t* zh[]{L"日", L"一", L"二", L"三", L"四", L"五", L"六"};
        constexpr const wchar_t* en[]{L"Su", L"Mo", L"Tu", L"We", L"Th", L"Fr", L"Sa"};
        for (unsigned column = 0; column < 7; ++column) {
            const unsigned weekday = (column + (monday ? 1 : 0)) % 7;
            const Rect area{weeks.x + column * columnWidth, weeks.y, columnWidth, weeks.height};
            layer.strokeBox(area, 0, 0xd7c3a4ff, {.width = float(std::max(.5 / key->dpi, viewport.scale * 2))});
            label(layer, english ? en[weekday] : zh[weekday], area, area.height * .62,
                  weekday == 0 || weekday == 6 ? accentInk
                  : spec.darkHeader            ? kCream
                                               : key->ink);
        }
        for (unsigned i = 0; i < unsigned(grid.rows * 7); ++i) {
            const auto day = grid.dates[i];
            const auto area = lunarCell(spec, viewport, i, unsigned(grid.rows));
            const double line = std::max(.5 / key->dpi, viewport.scale * 2), gap = viewport.scale * 4;
            layer.strokeBox(area, 0, 0xd7c3a4ff, {.width = float(line)});
            const bool current = day.month == view.selected.month;
            if (!lunarplugin::valid(day) || (!current && !key->adjacent)) {
                continue;
            }
            const bool chosen = day == view.selected, today = day == view.current;
            const bool weekend = lunarplugin::weekday(day) == 0 || lunarplugin::weekday(day) == 6;
            const auto value = lunarplugin::convert(day);
            uint32_t color = weekend ? accentInk : key->ink;
            if (!current) {
                color = (color & 0xffffff00) | 100;
            }
            const Rect face{area.x + gap, area.y + gap, area.width - gap * 2, area.height - gap * 2};
            if (chosen) {
                layer.box(face.x, face.y, face.width, face.height, 0, kMint);
                layer.strokeBox(face, 0, 0x4d9684ff, {.width = float(line * 1.5)});
                color = key->ink;
            } else if (today) {
                layer.strokeBox(face, 0, key->accent, {.width = float(line * 1.5)});
            }
            label(layer, std::to_wstring(day.day), {area.x, area.y + area.height * .07, area.width, area.height * .46},
                  std::min(area.height * .35, area.width * .39), color, true);
            const bool marked = !value.festival.empty() || !value.term.empty();
            // English festival and solar-term names are too long for a small cell.
            // Keep the lunar day legible there; selecting it reveals the full name
            // in the footer, with the same accent that marks the special date.
            const auto detail = english  ? std::to_wstring(value.day)
                                : chosen ? cdsdk::displayText(value.dayName)
                                : marked ? specialText(value, false)
                                         : cdsdk::displayText(value.dayName);
            label(layer, detail, {area.x + gap, area.y + area.height * .57, area.width - gap * 2, area.height * .29},
                  std::min(area.height * .22, area.width * .24),
                  chosen || !current ? color
                  : marked           ? accentInk
                                     : color);
        }
        const auto footer = viewport.map(spec.footer);
        label(layer,
              lunarText(selected, english) + L" · " + (special.empty() ? weekday(view.selected, english) : special),
              footer, footer.height * .8, spec.darkHeader ? kCream : accentInk, english);
    }

    void prepare(const cdsdk::Settings& settings, lunarplugin::View view, double width, double height, double dpi) {
        viewport = lunarViewport({0, 0, width, height}, spec);
        const auto edge = uint32_t(std::clamp(
            std::ceil(std::max(spec.source.width, spec.source.height) * viewport.scale * dpi / 64) * 64, 64., 4096.));
        pixeldesk::requestPixelImage(images, "body", path, 1, edge, dpi);
        images.poll();
        if (const auto next = images.get("body").image) {
            body = next;
        }
        const Key next{view.selected,
                       view.current,
                       width,
                       height,
                       dpi,
                       settings.at("language").text,
                       settings.at("week_start").text,
                       uint32_t(settings.at("text_color").integer),
                       uint32_t(settings.at("accent_color").integer),
                       settings.at("adjacent_days").integer != 0,
                       body.get()};
        if (key == next && owner == canvas.target) {
            return;
        }
        key = next;
        const bool english = key->language == "en";
        const auto lunar = lunarplugin::convert(view.selected);
        const auto special = specialText(lunar, english);
        cybercanvas::OffscreenSurface surface;
        canvas.renderLayer(
            surface, {uint32_t(std::ceil(width * dpi)), uint32_t(std::ceil(height * dpi)), dpi}, [&](Canvas& layer) {
                const auto map = [&](Rect r) {
                    return viewport.map(r);
                };
                if (body) {
                    layer.image(*body, map({0, 0, spec.source.width, spec.source.height}),
                                {.fit = cybercanvas::ImageFit::Stretch,
                                 .clip = false,
                                 .sampling = cybercanvas::ImageSampling::Nearest});
                } else {
                    stepped(layer, map(spec.visible), 15 * viewport.scale, kCream);
                }
                if (spec.rows >= 4) {
                    const auto paper = map(spec.paper);
                    layer.box(paper.x, paper.y, paper.width, paper.height, 0, kCream);
                    monthly(layer, view, lunar, special);
                    return;
                }
                const auto line = map(spec.divider);
                layer.box(line.x, line.y, line.width, line.height, 0, 0xc3ae91ff);
                const auto lunarArea = map(spec.lunar), badge = map(spec.badge);
                label(layer, lunarText(lunar, english), lunarArea, lunarArea.height * .83, key->ink, english);
                if (spec.rows > 1) {
                    const auto area = map(spec.weekday);
                    label(layer, weekday(view.selected, english), area, area.height * .85, key->ink, english);
                }
                if (!special.empty()) {
                    stepped(layer, badge, 13 * viewport.scale, key->accent);
                    label(layer, special,
                          {badge.x + 6 * viewport.scale, badge.y + 3 * viewport.scale,
                           badge.width - 12 * viewport.scale, badge.height - 6 * viewport.scale},
                          badge.height * .78, kCream);
                } else if (spec.rows == 1) {
                    label(layer, weekday(view.selected, english), badge, badge.height * .65, key->ink, english);
                } else {
                    label(layer, cdsdk::displayText(lunar.yearName), badge, badge.height * .75, key->ink, english);
                }
            });
        face = surface.snapshot();
        owner = canvas.target;
        ++builds;
    }

    void draw(lunarplugin::View view) {
        canvas.target->DrawBitmap(face.Get(), D2D1::RectF(0, 0, float(key->width), float(key->height)), 1,
                                  D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
        if (spec.darkHeader) {
            const auto title = [&](lunarplugin::Date date) {
                return std::to_wstring(date.year) + (key->language == "en" ? L" / " : L" 年 ") +
                       std::to_wstring(date.month) + (key->language == "en" ? L"" : L" 月");
            };
            const auto area = viewport.map(spec.date);
            canvas.clipped({area.x, area.y, area.width, area.height}, [&] {
                if (view.progress >= 1 || title(view.selected) == title(view.previous)) {
                    label(canvas, title(view.selected), area, area.height * .75, kCream);
                } else {
                    auto old = area, next = area;
                    old.y -= view.progress * area.height * view.direction;
                    next.y += (1 - view.progress) * area.height * view.direction;
                    label(canvas, title(view.previous), old, area.height * .75, kCream);
                    label(canvas, title(view.selected), next, area.height * .75, kCream);
                }
            });
        } else {
            rollingDigits(canvas, dateText(view.selected, spec.rows == 1), dateText(view.previous, spec.rows == 1),
                          viewport.map(spec.date), view, key->ink);
        }
        if (spec.rows < 4) {
            rollingDigits(canvas, std::to_string(view.selected.day), std::to_string(view.previous.day),
                          viewport.map(spec.day), view, key->ink);
        }
        if (spec.rows > 1) {
            for (unsigned i = 0; i < 2; ++i) {
                const auto r = viewport.map(spec.buttons[i]);
                const auto state = controls.visual(i + 1);
                if (spec.rows >= 4) {
                    stepped(canvas, r, 6 * viewport.scale, key->ink);
                    const double inset = 4 * viewport.scale;
                    stepped(canvas, {r.x + inset, r.y + inset, r.width - inset * 2, r.height - inset * 2},
                            4 * viewport.scale,
                            state.pressed     ? key->accent
                            : state.hovered   ? kMint
                            : spec.darkHeader ? kMint
                                              : kCream);
                } else if (state.hovered || state.pressed) {
                    stepped(canvas, r, 4 * viewport.scale, state.pressed ? key->accent : kMint);
                }
                const double side = std::min(r.width, r.height) * .7;
                const Rect icon{r.x + (r.width - side) / 2, r.y + (r.height - side) / 2, side, side};
                if (!controls.icons.paint(canvas, icon, i ? "chevron-right" : "chevron-left",
                                          state.pressed ? kCream : key->ink)) {
                    label(canvas, key->language == "en" ? (i ? L"Next" : L"Prev") : (i ? L"下月" : L"上月"), r,
                          r.height * .32, key->ink);
                }
            }
            if (spec.rows >= 4) {
                const auto r = viewport.map(spec.today);
                const auto state = controls.visual(3);
                stepped(canvas, r, 6 * viewport.scale, key->ink);
                const double inset = 4 * viewport.scale;
                stepped(canvas, {r.x + inset, r.y + inset, r.width - inset * 2, r.height - inset * 2},
                        4 * viewport.scale,
                        state.pressed   ? key->accent
                        : state.hovered ? kMint
                                        : kCream);
                label(canvas, key->language == "en" ? L"Today" : L"今日", r, r.height * .5,
                      state.pressed ? kCream : key->ink);
            }
        }
    }
};

LunarRenderer::LunarRenderer(const CdCreateContext& context, const LunarSpec& spec)
    : impl_(std::make_unique<Impl>(context, spec)) {}

LunarRenderer::~LunarRenderer() = default;

void LunarRenderer::onChange(std::function<void()> callback) {
    impl_->images.onChange(std::move(callback));
}

void LunarRenderer::onAction(std::function<void(uint32_t)> callback) {
    impl_->action = std::move(callback);
}

void LunarRenderer::pause(bool paused) {
    impl_->images.pause(paused);
    if (paused) {
        impl_->controls.cancel();
    }
}

void LunarRenderer::input(const CdInput& input) {
    impl_->controls.input(input);
}

void LunarRenderer::cancelInput() {
    impl_->controls.cancel();
}

void LunarRenderer::syncInput(const cdsdk::Settings& settings, lunarplugin::View view, CdRect safe) {
    impl_->syncInput(settings, view, safe);
}

bool LunarRenderer::ready() const {
    return impl_->body && !impl_->images.pending() && impl_->images.get("body").image == impl_->body;
}

bool LunarRenderer::render(const cdsdk::Settings& settings, lunarplugin::View view, const CdFrame& frame, CdRect safe) {
    if (!impl_->canvas.valid(frame) || !lunarplugin::valid(view.selected)) {
        return false;
    }
    const cdsdk::LanguageScope language(settings.at("language").text);
    impl_->canvas.render(frame, safe, [&](auto&, CdRect area) {
        impl_->syncInput(settings, view, area);
        impl_->prepare(settings, view, area.width, area.height, frame.device_pixel_ratio);
        impl_->draw(view);
    });
    return true;
}

bool LunarRenderer::save(const std::filesystem::path& path, const CdFrame& frame) {
    return impl_->canvas.save(path.wstring(), frame);
}

uint64_t LunarRenderer::cacheBuilds() const {
    return impl_->builds;
}
}  // namespace pixeldesk
