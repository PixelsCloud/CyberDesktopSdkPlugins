#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>

#include "plugins/air-quality-suite/AirQuality.h"

namespace airplugin {
namespace {
bool dateValid(const std::string& date) {
    if (date.size() != 10 || date[4] != '-' || date[7] != '-')
        return false;
    for (size_t i = 0; i < 10; ++i)
        if (i != 4 && i != 7 && (date[i] < '0' || date[i] > '9'))
            return false;
    return (std::chrono::year(std::stoi(date.substr(0, 4))) /
            std::chrono::month(unsigned(std::stoi(date.substr(5, 2)))) /
            std::chrono::day(unsigned(std::stoi(date.substr(8, 2)))))
        .ok();
}
}  // namespace

std::wstring wide(const std::string& text) {
    if (text.empty())
        return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0);
    if (!count)
        throw Error::InvalidResponse;
    std::wstring result(size_t(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), result.data(), count);
    return result;
}

bool validStation(const std::string& id) {
    return !id.empty() && id.size() <= 16 && std::all_of(id.begin(), id.end(), [](char value) {
        return value >= '0' && value <= '9';
    });
}

int level(double aqi) {
    return aqi <= 50 ? 0 : aqi <= 100 ? 1 : aqi <= 150 ? 2 : aqi <= 200 ? 3 : aqi <= 300 ? 4 : 5;
}

uint32_t levelColor(int value) {
    constexpr uint32_t colors[]{0x74E0B3FF, 0xF0CE73FF, 0xEDAF70FF, 0xEF7F91FF, 0xB99CEEFF, 0xE199B5FF};
    return colors[std::clamp(value, 0, 5)];
}

std::wstring levelName(int value, bool english) {
    const wchar_t* zh[]{L"良好", L"一般", L"敏感人群不健康", L"不健康", L"很不健康", L"危险"};
    const wchar_t* en[]{L"Good",      L"Moderate",       L"Unhealthy for sensitive groups",
                        L"Unhealthy", L"Very unhealthy", L"Hazardous"};
    return (english ? en : zh)[std::clamp(value, 0, 5)];
}

std::string levelIcon(int value) {
    const char* icons[]{"leaf", "wind", "cloud-fog", "factory", "shield-alert", "triangle-alert"};
    return icons[std::clamp(value, 0, 5)];
}

std::wstring weekday(const std::string& date, bool english) {
    if (!dateValid(date))
        return L"--";
    const auto d = std::chrono::year(std::stoi(date.substr(0, 4))) /
                   std::chrono::month(unsigned(std::stoi(date.substr(5, 2)))) /
                   std::chrono::day(unsigned(std::stoi(date.substr(8, 2))));
    const unsigned i = std::chrono::weekday(std::chrono::sys_days(d)).c_encoding();
    const wchar_t* zh[]{L"周日", L"周一", L"周二", L"周三", L"周四", L"周五", L"周六"};
    const wchar_t* en[]{L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat"};
    return (english ? en : zh)[i];
}

cdsdk::Settings defaults() {
    return {{{"location_mode", CD_SETTING_CHOICE, 0, 0, "auto", 0, 0, {"auto", "city", "coordinates", "station"}},
             {"city", CD_SETTING_TEXT, 0, 0, "", 0, 128},
             {"station", CD_SETTING_TEXT, 0, 0, "", 0, 16},
             {"latitude", CD_SETTING_NUMBER, 0, 0, "", -90, 90},
             {"longitude", CD_SETTING_NUMBER, 0, 0, "", -180, 180},
             {"credentials_file", CD_SETTING_TEXT, 0, 0, "", 0, 1024},
             {"refresh_minutes", CD_SETTING_INTEGER, 15, 0, "", 5, 180},
             {"palette", CD_SETTING_CHOICE, 0, 0, "dark", 0, 0, {"dark", "light"}},
             {"background_corner_radius", CD_SETTING_INTEGER, 5, 0, "", 0, 32},
             {"font_family", CD_SETTING_FONT, 0, 0, "@siyuan-heavy"},
             {"language", CD_SETTING_CHOICE, 0, 0, "zh-CN", 0, 0, {"zh-CN", "en"}}}};
}

Query queryFrom(const cdsdk::Settings& s) {
    Query q;
    q.mode = s.at("location_mode").text;
    q.language = s.at("language").text;
    q.credentials = s.at("credentials_file").text;
    if (q.mode == "city")
        q.city = s.at("city").text;
    else if (q.mode == "station")
        q.station = s.at("station").text;
    else if (q.mode == "coordinates") {
        q.city = s.at("city").text;
        q.latitude = s.at("latitude").number;
        q.longitude = s.at("longitude").number;
    }
    return q;
}
}  // namespace airplugin
