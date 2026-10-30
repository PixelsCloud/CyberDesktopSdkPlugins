#include <Windows.h>

#include <chrono>
#include <map>
#include <mutex>
#include <stdexcept>

#include "plugins/lunar-suite/Lunar.h"
#include "sxtwl.h"

namespace lunarplugin {
namespace {
std::chrono::year_month_day civil(Date d) {
    return {std::chrono::year(d.year), std::chrono::month(unsigned(d.month)), std::chrono::day(unsigned(d.day))};
}

Date unpack(std::chrono::year_month_day d) {
    return {int(d.year()), int(unsigned(d.month())), int(unsigned(d.day()))};
}
}  // namespace

bool valid(Date d) {
    return d.year >= 1900 && d.year <= 2100 && civil(d).ok();
}

Date today() {
    SYSTEMTIME t{};
    GetLocalTime(&t);
    return {t.wYear, t.wMonth, t.wDay};
}

Date shiftDay(Date d, int amount) {
    return unpack(std::chrono::year_month_day(std::chrono::sys_days(civil(d)) + std::chrono::days(amount)));
}

int dayCount(Date d) {
    using namespace std::chrono;
    return int(unsigned(year_month_day_last{year(d.year), month_day_last{month(unsigned(d.month))}}.day()));
}

Date shiftMonth(Date d, int amount) {
    using namespace std::chrono;
    const auto m = civil(d).year() / civil(d).month() + months(amount);
    Date result{int(m.year()), int(unsigned(m.month())), 1};
    result.day = std::min(d.day, dayCount(result));
    return valid(result) ? result : d;
}

int weekday(Date d) {
    return int(std::chrono::weekday(std::chrono::sys_days(civil(d))).c_encoding());
}

std::wstring Lunar::label() const {
    return !festival.empty() ? festival : !term.empty() ? term : day == 1 ? monthName : dayName;
}

Lunar convert(Date date) {
    if (!valid(date)) {
        throw std::invalid_argument("Lunar date outside supported 1900-2100 range");
    }
    static std::mutex mutex;
    static std::map<int, Lunar> cache;
    const std::lock_guard lock(mutex);
    const int key = date.year * 10000 + date.month * 100 + date.day;
    if (const auto found = cache.find(key); found != cache.end()) {
        return found->second;
    }
    std::unique_ptr<Day> day(sxtwl::fromSolar(date.year, uint8_t(date.month), date.day));
    Lunar result{day->getLunarYear(true), day->getLunarMonth(), day->getLunarDay(), day->isLunarLeap()};
    constexpr const wchar_t* months[]{L"正月", L"二月", L"三月", L"四月", L"五月", L"六月",
                                      L"七月", L"八月", L"九月", L"十月", L"冬月", L"腊月"};
    constexpr const wchar_t* names[]{L"初一", L"初二", L"初三", L"初四", L"初五", L"初六", L"初七", L"初八",
                                     L"初九", L"初十", L"十一", L"十二", L"十三", L"十四", L"十五", L"十六",
                                     L"十七", L"十八", L"十九", L"二十", L"廿一", L"廿二", L"廿三", L"廿四",
                                     L"廿五", L"廿六", L"廿七", L"廿八", L"廿九", L"三十"};
    constexpr const wchar_t* terms[]{L"冬至", L"小寒", L"大寒", L"立春", L"雨水", L"惊蛰", L"春分", L"清明",
                                     L"谷雨", L"立夏", L"小满", L"芒种", L"夏至", L"小暑", L"大暑", L"立秋",
                                     L"处暑", L"白露", L"秋分", L"寒露", L"霜降", L"立冬", L"小雪", L"大雪"};
    result.monthName = (result.leap ? std::wstring(L"闰") : std::wstring()) + months[result.month - 1];
    result.dayName = names[result.day - 1];
    const int cycle = (result.year - 4) % 60;
    result.yearName = std::wstring(1, L"甲乙丙丁戊己庚辛壬癸"[cycle % 10]) +
                      std::wstring(1, L"子丑寅卯辰巳午未申酉戌亥"[cycle % 12]) + L"年";
    if (day->hasJieQi()) {
        result.term = terms[day->getJieQi()];
    }
    if (!result.leap) {
        const int md = result.month * 100 + result.day;
        for (const auto& pair : {std::pair{101, L"春节"},
                                 {115, L"元宵"},
                                 {505, L"端午"},
                                 {707, L"七夕"},
                                 {815, L"中秋"},
                                 {909, L"重阳"},
                                 {1208, L"腊八"}}) {
            if (md == pair.first) {
                result.festival = pair.second;
            }
        }
        if (result.month == 12 && result.day >= 29) {
            std::unique_ptr<Day> next(day->after(1));
            if (next->getLunarMonth() == 1 && next->getLunarDay() == 1) {
                result.festival = L"除夕";
            }
        }
    }
    if (result.term == L"清明") {
        result.festival = L"清明";
    }
    if (cache.size() >= 4096) {
        cache.clear();
    }
    cache.emplace(key, result);
    return result;
}

Month monthGrid(Date date, bool monday) {
    const Date first{date.year, date.month, 1};
    const int offset = (weekday(first) + (monday ? 6 : 0)) % 7;
    Month result;
    result.rows = (offset + dayCount(first) + 6) / 7;
    for (int i = 0; i < 42; ++i) {
        result.dates[i] = shiftDay(first, i - offset);
    }
    return result;
}
}  // namespace lunarplugin
