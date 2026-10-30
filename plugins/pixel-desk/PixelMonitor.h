#pragma once

#include <sdk/Images.h>

#include <array>

#include "plugins/system-info-suite/Monitor.h"

namespace pixeldesk {
struct MonitorSpec {
    const char* id;
    const char* folder;
    unsigned columns, rows;
};

inline constexpr std::array<MonitorSpec, 4> kMonitorCatalog{{
    {"com.cyberdesktop.pixeldesk.monitor.1x1", "monitor-1x1", 1, 1},
    {"com.cyberdesktop.pixeldesk.monitor.2x1", "monitor-2x1", 2, 1},
    {"com.cyberdesktop.pixeldesk.monitor.4x1", "monitor-4x1", 4, 1},
    {"com.cyberdesktop.pixeldesk.monitor.2x2", "monitor-2x2", 2, 2},
}};

const MonitorSpec& monitorSpec(std::string_view id);
CdResult createMonitor(const CdCreateContext* context, CdInstanceApi* output, uint32_t size);

class MonitorFace {
public:
    MonitorFace();
    ~MonitorFace();
    void bind(const CdCreateContext& context, std::function<void()> changed);
    void pause(bool paused);
    bool ready() const;
    bool render(monitorplugin::Spec spec, const monitorplugin::Options& options, monitorplugin::Values values,
                const CdFrame& frame, CdRect safe = {});
    bool save(const std::filesystem::path& path, const CdFrame& frame);
    uint64_t cacheBuilds() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace pixeldesk
