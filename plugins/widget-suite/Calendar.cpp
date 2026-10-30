#include <Windows.h>

#include <algorithm>
#include <chrono>

#include "plugins/widget-suite/Widget.h"

namespace widgetplugin {
Date localDate() {
    SYSTEMTIME time{};
    GetLocalTime(&time);
    return {time.wYear, time.wMonth, time.wDay};
}

Date shiftMonth(Date date, int offset) {
    using namespace std::chrono;
    const auto value =
        year_month{year{date.year}, month{unsigned(date.month)}} + months{std::clamp(offset, -1200, 1200)};
    return {int(value.year()), int(unsigned(value.month())), date.day};
}

int monthDays(Date date) {
    using namespace std::chrono;
    return int(unsigned(year_month_day_last{year{date.year}, month_day_last{month{unsigned(date.month)}}}.day()));
}

int monthStart(Date date, bool monday) {
    using namespace std::chrono;
    const auto day = weekday{sys_days{year{date.year} / month{unsigned(date.month)} / 1}}.c_encoding();
    return int((day + (monday ? 6 : 0)) % 7);
}
}  // namespace widgetplugin
