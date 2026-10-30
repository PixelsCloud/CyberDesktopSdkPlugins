#pragma once
#include <sdk/PluginApi.h>
#include <sdk/Settings.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace weatherplugin {
struct Spec {
    const char* id;
    int rows, columns;
    bool mechanical = false;
};
enum class Error { None, Network, CityNotFound, InvalidResponse, RateLimited, LocationUnavailable };

struct Location {
    std::string city, country, language = "zh-CN";
    bool coordinates = false;
    double latitude = 0, longitude = 0;
    bool automatic = true;
    uint32_t forecastDays = 5, forecastHours = 0;
    bool operator==(const Location&) const = default;
};

struct Day {
    std::string date;
    int code = 0;
    double high = 0, low = 0, precipitation = 0;
    bool operator==(const Day&) const = default;
};

struct Hour {
    uint64_t timeUtcMs = 0;
    double temperature = 0, precipitation = 0;
    int code = 0;
    bool operator==(const Hour&) const = default;
};

struct Snapshot {
    bool valid = false, cached = false;
    Error error = Error::None;
    std::string city, region, time;
    std::string diagnostic;
    double latitude = 0, longitude = 0, temperature = 0, feels = 0, humidity = 0, wind = 0;
    int code = 0;
    bool day = true;
    uint64_t fetchedAt = 0;
    uint64_t observedAt = 0;
    std::string timezone;
    std::vector<Day> days;
    std::vector<Hour> hours;
    std::array<double, 2> needles{-1, -1};
    bool operator==(const Snapshot&) const = default;
};

std::string iconName(int code, bool day);
std::wstring condition(int code, bool english);
std::wstring weekday(const std::string& date, bool english);
std::wstring wide(const std::string& utf8);
double displayTemperature(double celsius, bool imperial);
cdsdk::Settings defaults(bool mechanical = false);
Location locationFrom(const cdsdk::Settings&);

class Renderer {
public:
    explicit Renderer(std::filesystem::path assets);
    ~Renderer();
    void bind(const CdCreateContext&);
    void pause(bool paused);
    bool render(Spec, const cdsdk::Settings&, const Snapshot&, bool loading, const CdFrame&, CdRect);
    // Blocking readiness is reserved for offline design exports and tests.
    // Production instances use render() and receive local image-completion redraws.
    bool renderReference(Spec, const cdsdk::Settings&, const Snapshot&, bool loading, const CdFrame&, CdRect);
    bool savePng(const std::filesystem::path&, const CdFrame&);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

CdResult create(Spec, const CdCreateContext*, CdInstanceApi*, uint32_t);
}  // namespace weatherplugin
