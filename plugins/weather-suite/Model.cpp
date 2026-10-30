#include <Windows.h>
#include <sdk/WeatherPresentation.h>

#include <algorithm>
#include <chrono>
#include <cmath>

#include "plugins/weather-suite/Weather.h"

namespace weatherplugin {
namespace {
bool dateValid(const std::string& date) {
    if (date.size() != 10 || date[4] != '-' || date[7] != '-')
        return false;
    for (size_t i = 0; i < date.size(); ++i)
        if (i != 4 && i != 7 && (date[i] < '0' || date[i] > '9'))
            return false;
    const auto value = std::chrono::year(std::stoi(date.substr(0, 4))) /
                       std::chrono::month(unsigned(std::stoi(date.substr(5, 2)))) /
                       std::chrono::day(unsigned(std::stoi(date.substr(8, 2))));
    return value.ok();
}
}  // namespace

std::wstring wide(const std::string& text) {
    if (text.empty())
        return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0);
    if (count <= 0)
        throw Error::InvalidResponse;
    std::wstring result(size_t(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), result.data(), count);
    return result;
}

std::string iconName(int code, bool day) {
    return cdsdk::weather::iconName(code, day);
}

std::wstring condition(int code, bool english) {
    return cdsdk::weather::condition(code, english);
}

std::wstring weekday(const std::string& date, bool english) {
    if (!dateValid(date))
        return L"--";
    const auto value = std::chrono::year(std::stoi(date.substr(0, 4))) /
                       std::chrono::month(unsigned(std::stoi(date.substr(5, 2)))) /
                       std::chrono::day(unsigned(std::stoi(date.substr(8, 2))));
    const unsigned day = std::chrono::weekday(std::chrono::sys_days(value)).c_encoding();
    const wchar_t* zh[]{L"周日", L"周一", L"周二", L"周三", L"周四", L"周五", L"周六"};
    const wchar_t* en[]{L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat"};
    return english ? en[day] : zh[day];
}

double displayTemperature(double celsius, bool imperial) {
    return imperial ? celsius * 1.8 + 32 : celsius;
}

cdsdk::Settings defaults(bool mechanical) {
    return {{{"auto_location", CD_SETTING_TOGGLE, 1},
             {"city", CD_SETTING_TEXT, 0, 0, "", 0, 128},
             {"country", CD_SETTING_TEXT, 0, 0, "", 0, 2},
             {"coordinates", CD_SETTING_TOGGLE, 0},
             {"latitude", CD_SETTING_NUMBER, 0, 0, "", -90, 90},
             {"longitude", CD_SETTING_NUMBER, 0, 0, "", -180, 180},
             {"refresh_minutes", CD_SETTING_INTEGER, 15, 0, "", 5, 180},
             {"units", CD_SETTING_CHOICE, 0, 0, "metric", 0, 0, {"metric", "imperial"}},
             {"palette", CD_SETTING_CHOICE, 0, 0, "dark", 0, 0, {"dark", "light"}},
             {"reduced_motion", CD_SETTING_TOGGLE, 0},
             {"background_corner_radius", CD_SETTING_INTEGER, 5, 0, "", 0, 32},
             {"font_family", CD_SETTING_FONT, 0, 0, mechanical ? "Microsoft YaHei UI" : "@siyuan-heavy"},
             {"language", CD_SETTING_CHOICE, 0, 0, "zh-CN", 0, 0, {"zh-CN", "en"}}}};
}

Location locationFrom(const cdsdk::Settings& settings) {
    if (settings.at("auto_location").integer) {
        Location location;
        location.language = settings.at("language").text;
        return location;
    }
    return {settings.at("city").text,
            settings.at("country").text,
            settings.at("language").text,
            settings.at("coordinates").integer != 0,
            settings.at("latitude").number,
            settings.at("longitude").number,
            false};
}
}  // namespace weatherplugin
