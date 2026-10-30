#include <sdk/Language.h>

#include <cmath>
#include <format>
#include <numbers>

#include "controllers/calendar/Calendar.h"
#include "controllers/lunar/Lunar.h"
#include "plugins/deep-whale/Skin.h"

namespace {
using whaleskin::Canvas;
using whaleskin::Rect;
using whaleskin::Skin;

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

std::wstring weekdayText(lunarplugin::Date date, bool english) {
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

void field(Skin& skin, Canvas& canvas, std::string_view name, const std::wstring& text, double factor = .77) {
    const auto r = skin.region(name);
    if (name == "festival" && skin.textured() && skin.rows() < 4 && !text.empty()) {
        auto badge = canvas.path();
        badge.moveTo({r.x, r.y + r.height / 2})
            .lineTo({r.x + r.height * .24, r.y})
            .lineTo({r.x + r.width - r.height * .24, r.y})
            .lineTo({r.x + r.width, r.y + r.height / 2})
            .lineTo({r.x + r.width - r.height * .24, r.y + r.height})
            .lineTo({r.x + r.height * .24, r.y + r.height})
            .close();
        canvas.fill(badge, 0xe5f2f7dd);
        canvas.stroke(badge, 0xd4b481cc, {.width = std::max(.7, r.height * .025)});
    }
    skin.label(canvas, text, r, r.height * factor);
}

void gridLines(Canvas& canvas, Rect r, unsigned columns, unsigned rows) {
    for (unsigned i = 0; i <= columns; ++i) {
        const double x = r.x + r.width * i / columns;
        canvas.line(x, r.y, x, r.y + r.height, 0xa18b6377, .8);
    }
    for (unsigned i = 0; i <= rows; ++i) {
        const double y = r.y + r.height * i / rows;
        canvas.line(r.x, y, r.x + r.width, y, 0xa18b6377, .8);
    }
}

Rect cell(Rect r, unsigned index, unsigned columns, unsigned rows) {
    return {r.x + (index % columns) * r.width / columns, r.y + (index / columns) * r.height / rows, r.width / columns,
            r.height / rows};
}
}  // namespace

namespace deep_calendar {
Renderer::Renderer(const CdCreateContext& context) : skin_(std::make_unique<Skin>()) {
    skin_->bind(context);
}

Renderer::~Renderer() = default;

void Renderer::onChange(std::function<void()> callback) {
    skin_->onChange(std::move(callback));
}

void Renderer::pause(bool value) {
    skin_->pause(value);
}

bool Renderer::ready() const {
    return skin_->ready();
}

uint64_t Renderer::cacheBuilds() const {
    return skin_->cacheBuilds();
}

bool Renderer::save(const std::filesystem::path& path, const CdFrame& frame) {
    return skin_->save(path, frame);
}

unsigned Renderer::hit(CdRect safe, cybercanvas::Point point) {
    for (unsigned i = 0; i < 2; ++i) {
        const auto r = skin_->displayRegion(i ? "next" : "previous", safe);
        if (point.x >= r.x && point.y >= r.y && point.x < r.x + r.width && point.y < r.y + r.height) {
            return i + 1;
        }
    }
    return 0;
}

bool Renderer::render(const Month& value, const cdsdk::Settings& settings, const Visual& visual, const CdFrame& frame,
                      CdRect safe) {
    skin_->appearance(settings);
    return skin_->render(frame, safe, [&](auto& canvas) {
        field(*skin_, canvas, "title", std::format(L"{}.{:02}", value.date.year, value.date.month), .9);
        skin_->button(canvas, skin_->region("previous"), "chevron-left", visual.pressed == 1);
        skin_->button(canvas, skin_->region("next"), "chevron-right", visual.pressed == 2);
        const auto r = skin_->region("grid");
        const auto paintPage = [&](const Month& page, double offset) {
            canvas.saved([&] {
                canvas.translate(0, offset);

                skin_->clear(canvas, r);
                gridLines(canvas, r, 7, page.weeks + 1);
                const std::array<std::wstring_view, 7> days =
                    settings.at("language").text == "en"
                        ? std::array<std::wstring_view, 7>{L"Su", L"Mo", L"Tu", L"We", L"Th", L"Fr", L"Sa"}
                        : std::array<std::wstring_view, 7>{L"日", L"一", L"二", L"三", L"四", L"五", L"六"};
                for (unsigned i = 0; i < 7; ++i) {
                    const auto d = cell(r, i, 7, page.weeks + 1);
                    skin_->label(canvas, std::wstring(days[(i + (page.monday ? 1 : 0)) % 7]), d, 12, whaleskin::kIvory,
                                 false);
                }
                for (unsigned i = 0; i < page.weeks * 7; ++i) {
                    const auto& day = page.cells[i];
                    if (!day.current && !settings.at("adjacent_days").integer) {
                        continue;
                    }
                    const auto d = cell(r, i + 7, 7, page.weeks + 1);
                    if (day.today) {
                        canvas.box(d.x + 1, d.y + 1, d.width - 2, d.height - 2, 1, 0xd6e8f5ff);
                    }
                    skin_->label(canvas, std::to_wstring(day.date.day), d, 14,
                                 day.weekend   ? 0x996a37ff
                                 : day.current ? whaleskin::kIvory
                                               : 0x807079ff,
                                 false);
                }
            });
        };
        canvas.clipped(r, [&] {
            const double progress = std::clamp(visual.progress, 0., 1.);
            if (progress < 1) {
                const auto old =
                    month(value.today,
                          (visual.previous.year - value.today.year) * 12 + visual.previous.month - value.today.month,
                          value.monday);
                const double eased = progress * progress * (3 - 2 * progress);
                paintPage(old, -visual.direction * eased * r.height);
                paintPage(value, visual.direction * (1 - eased) * r.height);
            } else {
                paintPage(value, 0);
            }
        });
    });
}
}  // namespace deep_calendar

namespace deep_lunar {
Renderer::Renderer() : skin_(std::make_unique<Skin>()) {}

Renderer::~Renderer() = default;

void Renderer::bind(const CdCreateContext& context, std::function<void(uint32_t)> action) {
    skin_->bind(context, std::move(action));
}

void Renderer::pause(bool value) {
    skin_->pause(value);
}

bool Renderer::pendingImages() const {
    return skin_->pending();
}

void Renderer::input(const CdInput& value) {
    skin_->input(value);
}

void Renderer::cancelInput() {
    skin_->cancelInput();
}

bool Renderer::save(const std::wstring& path, const CdFrame& frame) {
    return skin_->save(path, frame);
}

void Renderer::syncInput(Spec spec, const cdsdk::Settings& settings, View view, CdRect safe) {
    std::vector<cdsdk::Controls::Item> items;
    for (const auto& pair :
         std::array<std::pair<std::string_view, uint32_t>, 3>{{{"previous", 1}, {"next", 2}, {"today", 3}}}) {
        const auto r = skin_->region(pair.first);
        if (r.width > 0) {
            items.push_back({pair.second, r, std::string(pair.first)});
        }
    }
    if (spec.rows < 4) {
        items.push_back({3, {0, 0, skin_->size().width, skin_->size().height}, "Today"});
    } else {
        const auto month = monthGrid(view.selected, settings.at("week_start").text == "monday");
        const auto r = skin_->region("grid");
        for (unsigned i = 0; i < unsigned(month.rows * 7); ++i) {
            if (month.dates[i].month == view.selected.month || settings.at("adjacent_days").integer) {
                items.push_back({100 + i, cell(r, i, 7, month.rows), "Date " + std::to_string(month.dates[i].day)});
            }
        }
    }
    skin_->controls(safe, items);
}

bool Renderer::render(Spec spec, const cdsdk::Settings& settings, View view, const CdFrame& frame, CdRect safe) {
    const cdsdk::LanguageScope language(settings.at("language").text);
    const bool english = settings.at("language").text == "en";
    syncInput(spec, settings, view, safe);
    skin_->appearance(settings);
    return skin_->render(frame, safe, [&](auto& canvas) {
        const auto lunar = convert(view.selected);
        if (spec.rows >= 4) {
            field(*skin_, canvas, "title",
                  english ? std::format(L"{} / {}", view.selected.year, view.selected.month)
                          : std::format(L"{}年 {}月", view.selected.year, view.selected.month));
            skin_->button(canvas, skin_->region("previous"), "chevron-left");
            skin_->button(canvas, skin_->region("next"), "chevron-right");
            skin_->label(canvas, english ? L"Today" : L"今天", skin_->region("today"),
                         skin_->region("today").height * .56, whaleskin::kIvory, false);
            const auto month = monthGrid(view.selected, settings.at("week_start").text == "monday");
            const auto r = skin_->region("grid");
            skin_->clear(canvas, skin_->region("body"));
            skin_->clear(canvas, r);
            gridLines(canvas, r, 7, month.rows);
            const auto headings = skin_->region("weekdays");
            const std::array<std::wstring_view, 7> names =
                english ? std::array<std::wstring_view, 7>{L"Su", L"Mo", L"Tu", L"We", L"Th", L"Fr", L"Sa"}
                        : std::array<std::wstring_view, 7>{L"日", L"一", L"二", L"三", L"四", L"五", L"六"};
            for (unsigned i = 0; i < 7; ++i) {
                skin_->label(canvas,
                             std::wstring(names[(i + (settings.at("week_start").text == "monday" ? 1 : 0)) % 7]),
                             cell(headings, i, 7, 1), headings.height * .65, whaleskin::kIvory, false);
            }
            for (unsigned i = 0; i < unsigned(month.rows * 7); ++i) {
                const auto date = month.dates[i];
                if (!valid(date) || (date.month != view.selected.month && !settings.at("adjacent_days").integer)) {
                    continue;
                }
                auto d = cell(r, i, 7, month.rows);
                const auto converted = convert(date);
                if (date == view.selected) {
                    canvas.box(d.x + 1, d.y + 1, d.width - 2, d.height - 2, 1, 0xd6e8f5ff);
                }
                skin_->label(canvas, std::to_wstring(date.day), {d.x, d.y, d.width, d.height * .50}, 15,
                             weekday(date) == 0 || weekday(date) == 6 ? uint32_t(settings.at("accent_color").integer)
                                                                      : uint32_t(settings.at("text_color").integer),
                             false);
                skin_->label(canvas, english ? std::to_wstring(converted.day) : converted.dayName,
                             {d.x, d.y + d.height * .50, d.width, d.height * .50}, 12, whaleskin::kIvory, false);
            }
        } else {
            field(*skin_, canvas, "title",
                  spec.rows == 1 ? (english ? std::to_wstring(lunar.month) + L"/" + std::to_wstring(lunar.day)
                                            : cdsdk::displayText(lunar.monthName))
                                 : std::format(L"{}.{:02}", view.selected.year, view.selected.month),
                  1.05);
            field(*skin_, canvas, "day", std::to_wstring(view.selected.day), 1.2);
            constexpr std::array<std::wstring_view, 7> names{L"星期日", L"星期一", L"星期二", L"星期三",
                                                             L"星期四", L"星期五", L"星期六"};
            field(*skin_, canvas, "weekday", weekdayText(view.selected, english));
            if (spec.rows == 2) {
                const auto r = skin_->region("lunar");
                skin_->clear(canvas, r, 0xe5d5baff);
                skin_->label(canvas, lunarText(lunar, english), r, r.height * .66,
                             uint32_t(settings.at("text_color").integer), false);
            } else {
                field(*skin_, canvas, "lunar", english ? L"Lunar" : cdsdk::displayText(lunar.dayName), 1.0);
            }
        }
        const auto festival = skin_->region("festival");
        skin_->label(canvas, specialText(lunar, english), festival, festival.height * .65,
                     uint32_t(settings.at("accent_color").integer));
    });
}
}  // namespace deep_lunar
