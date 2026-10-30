#pragma once

#include <sdk/Images.h>
#include <sdk/Update.h>
#include <sdk/cpp/Services.h>

#include "plugins/air-quality-suite/AirQuality.h"

namespace pixeldesk {
struct AirSpec {
    const char* id;
    const char* folder;
    unsigned columns, rows;
    cybercanvas::Size source;
    cybercanvas::Rect visible;
    cybercanvas::Size world;
};

inline constexpr std::array<AirSpec, 3> kAirCatalog{{
    {"com.cyberdesktop.pixeldesk.air.2x1", "air-2x1", 2, 1, {394, 259}, {2, 2, 390, 255}, {390, 255}},
    {"com.cyberdesktop.pixeldesk.air.4x1", "air-4x1", 4, 1, {904, 261}, {2, 2, 900, 257}, {900, 257}},
    {"com.cyberdesktop.pixeldesk.air.4x2", "air-4x2", 4, 2, {917, 416}, {2, 2, 913, 412}, {913, 412}},
}};

const AirSpec& airSpec(std::string_view id);
cdsdk::Settings airDefaults(bool large = false);
airplugin::Snapshot airSnapshot(const cdsdk::AirSnapshot& value, const airplugin::Query& location);

class AirRenderer {
public:
    AirRenderer(const CdCreateContext& context, const AirSpec& spec);
    ~AirRenderer();
    void onChange(std::function<void()> callback);
    void pause(bool paused);
    bool ready() const;
    bool render(const cdsdk::Settings& settings, const airplugin::Snapshot& snapshot, bool loading,
                const CdFrame& frame, CdRect safe = {});
    bool save(const std::filesystem::path& path, const CdFrame& frame);
    uint64_t cacheBuilds() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class AirInstance {
public:
    explicit AirInstance(const CdCreateContext& context);
    ~AirInstance();
    cdsdk::Settings settings;
    CdResult render(const CdFrame& frame);
    CdResult applySettings(const CdSettingValue* values, uint32_t count);
    void layoutChanged(const CdLayout& layout);
    void input(const CdInput& event);
    void setPaused(bool paused);
    void invalidate();
    const airplugin::Snapshot& snapshot() const;
    bool loading() const;

private:
    CdResult subscribe(cdsdk::Settings next, bool clear);
    void receive(const cdsdk::AirSnapshot& value, const airplugin::Query& location);
    cdsdk::Services services_;
    AirRenderer renderer_;
    cdsdk::AirWatch watch_;
    cdsdk::RedrawRequests redraw_;
    airplugin::Snapshot snapshot_;
    CdRect safe_{};
    bool hidden_ = false, loading_ = true;
};
}  // namespace pixeldesk
