#pragma once

#include <sdk/Images.h>
#include <sdk/Update.h>
#include <sdk/cpp/Services.h>

#include "plugins/weather-suite/Weather.h"

namespace pixeldesk {
struct WeatherSpec {
    const char* id;
    const char* folder;
    unsigned columns, rows;
    cybercanvas::Size source;
    cybercanvas::Rect visible;
    cybercanvas::Size world;
    bool normalized = false;
};

inline constexpr std::array<WeatherSpec, 3> kWeatherCatalog{{
    {"com.cyberdesktop.pixeldesk.weather.2x1", "weather-2x1", 2, 1, {458, 267}, {2, 2, 454, 263}, {454, 263}, true},
    {"com.cyberdesktop.pixeldesk.weather.4x1", "weather-4x1", 4, 1, {953, 201}, {2, 2, 949, 197}, {949, 271}, true},
    {"com.cyberdesktop.pixeldesk.weather.4x2",
     "weather-4x2",
     4,
     2,
     {1608, 978},
     {37, 85, 1536, 836},
     {1536, 836},
     true},
}};

const WeatherSpec& weatherSpec(std::string_view id);
weatherplugin::Snapshot weatherSnapshot(const cdsdk::WeatherSnapshot& value, const weatherplugin::Location& location);

class WeatherRenderer {
public:
    WeatherRenderer(const CdCreateContext& context, const WeatherSpec& spec);
    ~WeatherRenderer();
    void onChange(std::function<void()> callback);
    void pause(bool paused);
    bool ready() const;
    bool render(const cdsdk::Settings& settings, const weatherplugin::Snapshot& snapshot, bool loading,
                const CdFrame& frame, CdRect safe = {});
    bool save(const std::filesystem::path& path, const CdFrame& frame);
    uint64_t cacheBuilds() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class WeatherInstance {
public:
    explicit WeatherInstance(const CdCreateContext& context);
    ~WeatherInstance();
    cdsdk::Settings settings;
    CdResult render(const CdFrame& frame);
    CdResult applySettings(const CdSettingValue* values, uint32_t count);
    void layoutChanged(const CdLayout& layout);
    void input(const CdInput& event);
    void setPaused(bool paused);
    void invalidate();
    const weatherplugin::Snapshot& snapshot() const;
    bool loading() const;

private:
    CdResult subscribe(cdsdk::Settings next, bool clear);
    void receive(const cdsdk::WeatherSnapshot& value, const weatherplugin::Location& location);
    cdsdk::Services services_;
    WeatherRenderer renderer_;
    cdsdk::WeatherWatch watch_;
    cdsdk::RedrawRequests redraw_;
    weatherplugin::Snapshot snapshot_;
    CdRect safe_{};
    bool hidden_ = false, loading_ = true;
};
}  // namespace pixeldesk
