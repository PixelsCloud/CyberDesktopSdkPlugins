#include <sdk/Controls.h>
#include <sdk/Language.h>

#include "plugins/living-common/Drawing.h"
#include "plugins/lunar-suite/Lunar.h"
#include "plugins/lunar-suite/MechanicalLunar.h"

namespace lunarplugin {
struct Renderer::Impl {
    living::Drawing draw;
    cdsdk::Controls controls;
    mechanicalart::Artwork artwork;

    cybercanvas::Label label(std::wstring value, double size, cybercanvas::Rect bounds, uint32_t color,
                             bool bold = false) {
        cybercanvas::TextStyle font;
        font.family = L"Microsoft YaHei";
        font.locale = cdsdk::drawingLocale();
        font.size = size;
        font.bold = bold;
        font.align = cybercanvas::TextAlign::Center;
        return {bounds, cdsdk::displayText(value), font, color};
    }

    void blossom(double x, double y, double size, bool dark) {
        draw.line(x - size, y + size * .8, x + size, y - size * .8, dark ? 0x817664FF : 0x7D7565FF, 1.5);
        for (int i = 0; i < 3; ++i) {
            const double cx = x + (i - 1) * size * .64, cy = y - (i - 1) * size * .44;
            for (int petal = 0; petal < 5; ++petal) {
                const double a = petal * 6.2831853 / 5;
                draw.oval(cx + std::cos(a) * size * .19, cy + std::sin(a) * size * .19, size * .16, size * .16,
                          dark ? 0xC9C8B9FF : 0xF8E7DBFF);
            }
            draw.oval(cx, cy, size * .075, size * .075, 0xD4AA58FF);
        }
    }

    void paint(Spec spec, const cdsdk::Settings& settings, View view, double w, double h) {
        const auto mode = settings.at("lighting").text;
        const bool dark = mode == "night" || mode == "theme" && view.dark;
        const uint32_t bg = dark ? 0x202C33FF : 0xFFFBF2FF, fg = dark ? 0xF3E7CBFF : 0x293A35FF,
                       muted = dark ? 0xA6B8B7FF : 0x7B8276FF, red = dark ? 0xE9BD94FF : 0xAD4134FF;
        const double r = settings.at("background_corner_radius").integer;
        if (settings.at("show_background").integer) {
            draw.box(1, 1, w - 3, h - 4, r, bg);
        }
        const bool english = settings.at("language").text == "en";
        const auto value = convert(view.selected);
        const auto special = value.festival.empty() ? value.term : value.festival;
        const auto lunarDate = [&](const Lunar& date) {
            if (english) {
                return L"Lunar " + std::to_wstring(date.month) + L"/" + std::to_wstring(date.day) +
                       (date.leap ? L" (leap)" : L"");
            }
            const auto month = date.leap && cdsdk::currentUiLanguage == cdsdk::UiLanguage::Traditional
                                   ? L"閏" + cdsdk::displayText(date.monthName.substr(1))
                                   : cdsdk::displayText(date.monthName);
            return month + cdsdk::displayText(date.dayName);
        };
        if (spec.rows == 1) {
            const double flowerWidth = std::min(w * .2, h * .50);
            blossom(flowerWidth * .55 + 4, h * .53, flowerWidth * .57, dark);
            const double left = flowerWidth + 6, width = w - left - 7;
            draw.text(lunarDate(value), std::clamp(h * .27, 10., 18.), {left, h * .10, width, h * .47}, red, true,
                      true);
            draw.text(special.empty() ? std::to_wstring(view.selected.month) + (english ? L"/" : L"月") +
                                            std::to_wstring(view.selected.day) + (english ? L"" : L"日")
                                      : special,
                      std::clamp(h * .16, 8., 11.), {left, h * .57, width, h * .28}, muted);
            return;
        }
        const bool monthly = spec.rows >= 4;
        const double top = std::clamp(h * .09, 12., 24.);
        if (settings.at("show_background").integer) {
            draw.box(1, 1, w - 3, top + 4, r, dark ? 0x273A40FF : 0xAD4134FF);
            draw.box(1, top - r, w - 3, r * 2 + 1, 0, dark ? 0x273A40FF : 0xAD4134FF);
        }
        for (double x : {w * .17, w * .82}) {
            draw.box(x, 0, std::max(2., w * .012), top * .8, 2, dark ? 0xB3A07AFF : 0xD5BC83FF);
        }
        const double headerY = top + 4, headerH = std::clamp(h * .13, 19., 36.);
        const auto title = std::to_wstring(view.selected.year) + (english ? L"-" : L"年") +
                           std::to_wstring(view.selected.month) + (english ? L"" : L"月");
        draw.text(title, std::clamp(w * .052, 10., 17.), {24, headerY, w - 50, headerH}, red);
        auto theme = dark ? cybercanvas::WidgetTheme::dark() : cybercanvas::WidgetTheme::light();
        theme.surface = 0;
        theme.foreground = red;
        for (uint32_t id : {1u, 2u}) {
            cybercanvas::IconButton button(id, {id == 1 ? 4 : w - 25, headerY, 20, headerH},
                                           id == 1 ? "chevron-left" : "chevron-right");
            button.iconSize = 18;
            button.draw(draw, theme, controls.icons.painter(), controls.visual(id));
        }
        if (!monthly) {
            const double dayTop = headerY + headerH;
            const double bottom = std::max(dayTop + 1, h * .65);
            draw.text(std::to_wstring(view.selected.day),
                      std::clamp(std::min(w * .52, (bottom - dayTop) * .9), 20., 92.),
                      {8, dayTop - 2, w - 18, bottom - dayTop}, red, true, true);
            const int wd = weekday(view.selected);
            constexpr const wchar_t* zh[]{L"星期日", L"星期一", L"星期二", L"星期三", L"星期四", L"星期五", L"星期六"};
            constexpr const wchar_t* en[]{L"Sunday",   L"Monday", L"Tuesday", L"Wednesday",
                                          L"Thursday", L"Friday", L"Saturday"};
            const double detail = std::clamp(h * .057, 8., 12.);
            draw.text(english ? en[wd] : zh[wd], detail, {8, bottom - 1, w - 18, detail * 1.6}, muted);
            draw.line(w * .16, bottom + detail * 1.75, w * .84, bottom + detail * 1.75, dark ? 0xE9BD9440 : 0xAD413440,
                      .6);
            draw.text(lunarDate(value), std::clamp(w * .085, 10., 17.),
                      {8, bottom + detail * 1.9, w - 18, detail * 1.8}, red);
            if (!special.empty() && h > 115) {
                draw.text(special, detail, {w * .20, bottom + detail * 3.9 + 2, w * .6, detail * 1.4}, fg);
            }
            if (w > 100 && h > 130) {
                blossom(17, h - 22, 15, dark);
            }
            return;
        }
        const bool monday = settings.at("week_start").text == "monday";
        const Month grid = monthGrid(view.selected, monday);
        const double left = 9, cell = (w - 20) / 7, weekY = headerY + headerH + 3,
                     weekH = std::clamp(h * .065, 15., 24.), startY = weekY + weekH,
                     footer = std::clamp(h * .10, 24., 40.), rowH = (h - startY - footer - 6) / grid.rows;
        constexpr const wchar_t* zhDays[]{L"日", L"一", L"二", L"三", L"四", L"五", L"六"};
        constexpr const wchar_t* enDays[]{L"Su", L"Mo", L"Tu", L"We", L"Th", L"Fr", L"Sa"};
        for (int i = 0; i < 7; ++i) {
            const int wd = (i + (monday ? 1 : 0)) % 7;
            draw.text(english ? enDays[wd] : zhDays[wd], std::clamp(cell * .25, 8., 13.),
                      {left + i * cell, weekY, cell, weekH}, wd == 0 || wd == 6 ? red : muted);
        }
        const cybercanvas::CalendarGrid geometry{{left, startY, w - 20, rowH * grid.rows}, size_t(grid.rows)};
        for (int i = 0; i < grid.rows * 7; ++i) {
            const auto day = grid.dates[i];
            if (!valid(day)) {
                continue;
            }
            const bool current = day.month == view.selected.month, selected = day == view.selected,
                       now = day == view.current;
            if (!current && !settings.at("adjacent_days").integer) {
                continue;
            }
            const auto bounds = geometry.cell(size_t(i));
            const double x = bounds.x, y = bounds.y;
            const auto ink = selected ? 0xFFFAEEFF : !current ? muted & 0xFFFFFF00 | 0x70 : fg;
            cybercanvas::CalendarCell item;
            item.bounds = bounds;
            item.selected = selected;
            item.today = now;
            item.current = current;
            item.highlight = selected ? cybercanvas::Rect{x + 2, y + 1, cell - 4, rowH - 2}
                                      : cybercanvas::Rect{x + 3, y + 2, cell - 6, rowH - 4};
            item.radius = selected ? std::min(5., rowH * .12) : 4;
            item.selection = dark ? 0x665B44FF : 0xAD4134FF;
            item.todayColor = dark ? 0xB5965A33 : 0xAD413416;
            item.day = label(std::to_wstring(day.day), std::clamp(rowH * .35, 10., 21.), {x, y + 1, cell, rowH * .54},
                             ink, selected);
            const auto lunar = convert(day);
            item.detail = label(
                english && lunar.festival.empty() && lunar.term.empty() ? std::to_wstring(lunar.day) : lunar.label(),
                std::clamp(std::min(cell * .22, rowH * .24), 7., 11.), {x, y + rowH * .53, cell, rowH * .35},
                selected                                         ? 0xFFFAEEFF
                : !current                                       ? ink
                : !lunar.festival.empty() || !lunar.term.empty() ? red
                                                                 : muted);
            item.draw(draw);
        }
        draw.line(12, h - footer - 2, w - 15, h - footer - 2, dark ? 0xD6C6A733 : 0xAD413433, .6);
        draw.text(lunarDate(value) + (special.empty() ? L"" : L" · " + cdsdk::displayText(special)),
                  std::clamp(w * .042, 10., 15.), {8, h - footer, w - 18, footer - 5}, red);
    }
};

Renderer::Renderer() : impl_(std::make_unique<Impl>()) {}

Renderer::~Renderer() = default;

void Renderer::bind(const CdCreateContext& context, std::function<void(uint32_t)> action) {
    impl_->controls.bind(context, std::move(action));
    impl_->draw.bindGpu(context);
    impl_->artwork.bind(context);
}

void Renderer::pause(bool paused) {
    impl_->artwork.pause(paused);
}

bool Renderer::pendingImages() const {
    return impl_->artwork.pending();
}

void Renderer::input(const CdInput& event) {
    impl_->controls.input(event);
}

void Renderer::cancelInput() {
    impl_->controls.cancel();
}

void Renderer::syncInput(Spec spec, const cdsdk::Settings& settings, View view, CdRect safe) {
    std::vector<cdsdk::Controls::Item> items;
    const double w = safe.width, h = safe.height;
    if (spec.mechanical && w > 20 && h > 20) {
        const auto layout = mechanicalLunarLayout(spec, w, h);
        if (spec.rows == 1) {
            items.push_back({3, layout.stage.map(layout.visible), "Today"});
        } else {
            items = {{1, layout.stage.map(layout.previous), "Previous month",
                      shiftMonth(view.selected, -1) != view.selected},
                     {2, layout.stage.map(layout.next), "Next month", shiftMonth(view.selected, 1) != view.selected},
                     {3, layout.stage.map(layout.header), "Today"}};
            if (spec.rows >= 4) {
                const auto month = monthGrid(view.selected, settings.at("week_start").text == "monday");
                const cybercanvas::CalendarGrid grid{layout.dates, size_t(month.rows)};
                for (size_t i = 0; i < size_t(month.rows * 7); ++i) {
                    const auto day = month.dates[i];
                    if (valid(day) && (day.month == view.selected.month || settings.at("adjacent_days").integer)) {
                        items.push_back({uint32_t(100 + i), layout.stage.map(grid.cell(i)),
                                         std::to_string(day.year) + "-" + std::to_string(day.month) + "-" +
                                             std::to_string(day.day)});
                    }
                }
            }
        }
        impl_->controls.items(std::move(items), {safe.x, safe.y});
        return;
    }
    if (w > 20 && h > 20) {
        if (spec.rows == 1) {
            items.push_back({3, {0, 0, w, h}, "Today"});
        } else {
            const double top = std::clamp(h * .09, 12., 24.) + 4, header = std::clamp(h * .13, 19., 36.);
            items = {{1, {4, top, 20, header}, "Previous month"},
                     {2, {w - 25, top, 20, header}, "Next month"},
                     {3, {24, top, w - 49, header}, "Today"}};
            if (spec.rows >= 4) {
                const auto dates = monthGrid(view.selected, settings.at("week_start").text == "monday");
                const double y = top + header + 3 + std::clamp(h * .065, 15., 24.);
                cybercanvas::CalendarGrid grid{{9, y, w - 20, h - y - std::clamp(h * .10, 24., 40.) - 6},
                                               size_t(dates.rows)};
                for (size_t i = 0; i < size_t(dates.rows * 7); ++i) {
                    const auto date = dates.dates[i];
                    if (!valid(date) || (date.month != view.selected.month && !settings.at("adjacent_days").integer)) {
                        continue;
                    }
                    items.push_back({uint32_t(100 + i), grid.cell(i),
                                     std::to_string(date.year) + "-" + std::to_string(date.month) + "-" +
                                         std::to_string(date.day)});
                }
            }
        }
    }
    impl_->controls.items(std::move(items), {safe.x, safe.y});
}

bool Renderer::render(Spec spec, const cdsdk::Settings& settings, View view, const CdFrame& frame, CdRect safe) {
    const cdsdk::LanguageScope languageScope(settings.at("language").text);
    try {
        auto painting = impl_->draw.frame(frame, safe);
        const auto area = painting.area();
        syncInput(spec, settings, view, {safe.x, safe.y, area.width, area.height});
        if (area.width > 20 && area.height > 20 && valid(view.selected)) {
            if (spec.mechanical) {
                drawMechanicalLunar(impl_->draw, impl_->artwork, impl_->controls, spec, settings, view, area.width,
                                    area.height);
            } else {
                impl_->paint(spec, settings, view, area.width, area.height);
            }
        }
        impl_->controls.drawFocus(impl_->draw,
                                  view.dark ? cybercanvas::WidgetTheme::dark() : cybercanvas::WidgetTheme::light());
        painting.finish();
        return true;
    } catch (...) {
        return false;
    }
}

bool Renderer::save(const std::wstring& filename, const CdFrame& frame) {
    return impl_->draw.save(filename, frame);
}

int hit(Spec spec, double w, double h, double x, double y) {
    if (x < 0 || y < 0 || x >= w || y >= h || spec.rows == 1) {
        return 0;
    }
    if (spec.mechanical) {
        const auto layout = mechanicalLunarLayout(spec, w, h);
        for (const auto& [box, result] :
             {std::pair{layout.previous, -1}, std::pair{layout.next, 1}, std::pair{layout.header, 2}}) {
            const auto mapped = layout.stage.map(box);
            if (x >= mapped.x && y >= mapped.y && x < mapped.x + mapped.width && y < mapped.y + mapped.height) {
                return result;
            }
        }
        return 0;
    }
    const double top = std::clamp(h * .09, 12., 24.) + 4, header = std::clamp(h * .13, 19., 36.);
    if (y >= top && y <= top + header) {
        return x < 25 ? -1 : x > w - 26 ? 1 : 2;
    }
    return 0;
}
}  // namespace lunarplugin
