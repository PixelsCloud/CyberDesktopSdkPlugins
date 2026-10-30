#pragma once
#include <sdk/PluginApi.h>
#include <sdk/Settings.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace airplugin {
struct Spec {
    const char* id;
    int rows, columns;
    bool mechanical = false;
};
enum class Error {
    None,
    Network,
    NoData,
    InvalidResponse,
    RateLimited,
    LocationUnavailable,
    CredentialsMissing,
    Unauthorized
};

struct Query {
    std::string mode = "auto", city, station, language = "zh-CN", credentials;
    double latitude = 0, longitude = 0;
    bool operator==(const Query&) const = default;
};

struct Source {
    std::string name, url;
    bool operator==(const Source&) const = default;
};

struct Forecast {
    std::string date;
    double average = 0, low = 0, high = 0;
    bool operator==(const Forecast&) const = default;
};

struct Snapshot {
    bool valid = false, cached = false;
    Error error = Error::None;
    std::string city, station, url, time, dominant, diagnostic;
    double latitude = 0, longitude = 0, aqi = 0;
    // WAQI iaqi values are pollutant sub-indices, never raw concentrations.
    std::array<double, 6> pollutants{{-1, -1, -1, -1, -1, -1}};
    std::vector<Source> sources;
    std::vector<Forecast> forecast;
    uint64_t fetchedAt = 0, observedAt = 0;
    bool operator==(const Snapshot&) const = default;
};

std::wstring wide(const std::string&);
bool validStation(const std::string&);
int level(double);
uint32_t levelColor(int);
std::wstring levelName(int, bool english);
std::string levelIcon(int);
std::wstring weekday(const std::string&, bool english);
cdsdk::Settings defaults();
Query queryFrom(const cdsdk::Settings&);

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
}  // namespace airplugin
