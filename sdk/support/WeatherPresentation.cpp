#include "sdk/WeatherPresentation.h"

namespace cdsdk::weather {

std::string iconName(int code, bool day) {
    switch (code) {
        case 0:
            return day ? "clear-day" : "clear-night";
        case 1:
            return day ? "mostly-clear-day" : "mostly-clear-night";
        case 2:
            return day ? "partly-cloudy-day" : "partly-cloudy-night";
        case 3:
            return "overcast";
        case 45:
        case 48:
            return "fog";
        case 51:
        case 53:
        case 55:
            return "drizzle";
        case 56:
        case 57:
        case 66:
        case 67:
            return "sleet";
        case 61:
        case 63:
        case 65:
        case 80:
        case 81:
        case 82:
            return "rain";
        case 71:
        case 73:
        case 75:
        case 85:
        case 86:
            return "snow";
        case 77:
            return "snowflake";
        case 95:
            return day ? "thunderstorms-day" : "thunderstorms-night";
        case 96:
        case 99:
            return day ? "thunderstorms-day-rain" : "thunderstorms-night-rain";
        default:
            return "not-available";
    }
}

std::wstring condition(int code, bool english) {
    switch (code) {
        case 0:
            return english ? L"Clear" : L"晴";
        case 1:
            return english ? L"Mostly clear" : L"晴间多云";
        case 2:
            return english ? L"Partly cloudy" : L"多云";
        case 3:
            return english ? L"Overcast" : L"阴";
        case 45:
        case 48:
            return english ? L"Fog" : L"雾";
        case 51:
        case 53:
        case 55:
            return english ? L"Drizzle" : L"毛毛雨";
        case 56:
        case 57:
            return english ? L"Freezing drizzle" : L"冻毛毛雨";
        case 61:
            return english ? L"Light rain" : L"小雨";
        case 63:
            return english ? L"Rain" : L"中雨";
        case 65:
            return english ? L"Heavy rain" : L"大雨";
        case 66:
        case 67:
            return english ? L"Freezing rain" : L"冻雨";
        case 71:
            return english ? L"Light snow" : L"小雪";
        case 73:
            return english ? L"Snow" : L"中雪";
        case 75:
            return english ? L"Heavy snow" : L"大雪";
        case 77:
            return english ? L"Snow grains" : L"雪粒";
        case 80:
        case 81:
        case 82:
            return english ? L"Rain showers" : L"阵雨";
        case 85:
        case 86:
            return english ? L"Snow showers" : L"阵雪";
        case 95:
            return english ? L"Thunderstorm" : L"雷雨";
        case 96:
        case 99:
            return english ? L"Thunder / hail" : L"雷雨冰雹";
        default:
            return english ? L"Unknown" : L"未知天气";
    }
}

}  // namespace cdsdk::weather
