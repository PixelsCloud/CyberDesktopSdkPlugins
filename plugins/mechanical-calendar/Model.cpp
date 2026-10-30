#include <algorithm>
#include <cmath>
#include <cwchar>
#include <stdexcept>

#include "plugins/mechanical-calendar/Calendar.h"

namespace mechanicalcalendar {
cdsdk::Settings defaults() {
    return {{{"week_start", CD_SETTING_CHOICE, 0, 0, "monday", 0, 0, {"monday", "sunday"}},
             {"accent", CD_SETTING_COLOR, 0xf0c77eff},
             {"weekday_color", CD_SETTING_COLOR, 0xffefdbff},
             {"weekend_color", CD_SETTING_COLOR, 0xe9a16eff},
             {"tile_color", CD_SETTING_COLOR, 0x242626ff},
             {"adjacent_days", CD_SETTING_TOGGLE, 0},
             {"font_family", CD_SETTING_FONT, 0, 0, "Microsoft YaHei", 0, 128},
             {"language", CD_SETTING_CHOICE, 0, 0, "zh-CN", 0, 0, {"zh-CN", "zh-TW", "en"}},
             {"reduced_motion", CD_SETTING_TOGGLE, 0}}};
}

Date localDate(CdEnvironmentSnapshot snapshot) {
    const auto time = cdsdk::Environment::localTime(snapshot);
    return {time.tm_year + 1900, time.tm_mon + 1, time.tm_mday};
}

Month month(Date today, int offset, bool monday) {
    if (today.year < 1601 || today.year > 9999 || today.month < 1 || today.month > 12 || today.day < 1 ||
        today.day > widgetplugin::monthDays(today) || offset < -1200 || offset > 1200) {
        throw std::invalid_argument("Invalid calendar date or offset");
    }
    Month result;
    result.today = today;
    result.date = widgetplugin::shiftMonth({today.year, today.month, 1}, offset);
    result.monday = monday;
    result.days = unsigned(widgetplugin::monthDays(result.date));
    result.start = unsigned(widgetplugin::monthStart(result.date, monday));
    result.weeks = (result.start + result.days + 6) / 7;
    const auto previous = widgetplugin::shiftMonth(result.date, -1), next = widgetplugin::shiftMonth(result.date, 1);
    const int previousDays = widgetplugin::monthDays(previous);
    for (unsigned i = 0; i < result.cells.size(); ++i) {
        const int day = int(i) - int(result.start) + 1;
        const bool current = day >= 1 && day <= int(result.days);
        const Date date = current   ? Date{result.date.year, result.date.month, day}
                          : day < 1 ? Date{previous.year, previous.month, previousDays + day}
                                    : Date{next.year, next.month, day - int(result.days)};
        const auto weekday = (i + (monday ? 1 : 0)) % 7;
        result.cells[i] = {date, current, current && date == today, weekday == 0 || weekday == 6};
    }
    return result;
}

std::wstring headerDigits(Date date) {
    wchar_t text[16]{};
    std::swprintf(text, std::size(text), L"%04d%02d", date.year, date.month);
    return text;
}

Geometry arrange(double width, double height, unsigned weeks) {
    Geometry g;
    g.scale = std::max(.001, std::min((width - 4) / 1544, (height - 4) / 870));
    const double x = (width - 1544 * g.scale) / 2 - 34 * g.scale;
    const double y = (height - 870 * g.scale) / 2 - 30 * g.scale;
    const auto map = [&](cybercanvas::Rect r) {
        return cybercanvas::Rect{x + r.x * g.scale, y + r.y * g.scale, r.width * g.scale, r.height * g.scale};
    };
    g.body = map({0, 0, 1600, 960});
    g.grid = map({193, 311, 1225, 453});
    for (unsigned i = 0; i < 6; ++i) {
        // Centers follow the actual empty wheel faces in the 1600x960 material
        // coordinate space, including the narrower spacing after the year label.
        constexpr double positions[]{260, 355, 450, 548, 767, 864};
        g.wheels[i] = map({positions[i], 143, 64, 94});
    }
    g.yearLabel = map({660, 142, 64, 98});
    g.monthLabel = map({963, 142, 64, 98});
    g.buttons = {map({1153, 146, 88, 88}), map({1295, 146, 88, 88})};
    g.columnWidth = g.grid.width / 7;
    g.rowHeight = g.grid.height / (std::clamp(weeks, 4u, 6u) + 1);
    return g;
}

cybercanvas::Rect Geometry::cell(unsigned column, unsigned row) const {
    return {grid.x + column * columnWidth, grid.y + row * rowHeight, columnWidth, rowHeight};
}

unsigned buttonAt(const Geometry& geometry, cybercanvas::Point point) {
    for (unsigned i = 0; i < geometry.buttons.size(); ++i) {
        const auto& r = geometry.buttons[i];
        if (point.x >= r.x && point.x < r.x + r.width && point.y >= r.y && point.y < r.y + r.height) {
            return i + 1;
        }
    }
    return 0;
}
}  // namespace mechanicalcalendar
