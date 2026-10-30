#include <algorithm>
#include <cmath>

#include "plugins/system-info-suite/Monitor.h"

namespace monitorplugin {
Values decode(const CdSystemInfo& info) {
    Values values;
    if (info.struct_size < sizeof(CdSystemInfo))
        return values;
    const auto finite = [](double value) {
        return std::isfinite(value);
    };
    if (finite(info.cpu_percent)) {
        values.cpu = std::clamp(info.cpu_percent, 0.0, 100.0);
        values.valid |= CD_SYSTEM_CPU_VALID;
    }
    if (info.memory_total_bytes > 0 && info.memory_used_bytes <= info.memory_total_bytes) {
        values.ram = 100.0 * info.memory_used_bytes / info.memory_total_bytes;
        values.valid |= CD_SYSTEM_MEMORY_VALID;
    }
    if (finite(info.gpu_percent)) {
        values.gpu = std::clamp(info.gpu_percent, 0.0, 100.0);
        values.valid |= CD_SYSTEM_GPU_VALID;
    }
    if (info.struct_size >= sizeof(CdSystemInfoEx)) {
        const auto& extended = *reinterpret_cast<const CdSystemInfoEx*>(&info);
        values.valid &= extended.valid_fields;
        if ((extended.valid_fields & CD_SYSTEM_CPU_TEMPERATURE_VALID) && finite(extended.cpu_temperature_celsius) &&
            extended.cpu_temperature_celsius >= 0 && extended.cpu_temperature_celsius <= 150) {
            values.temperature = extended.cpu_temperature_celsius;
            values.valid |= CD_SYSTEM_CPU_TEMPERATURE_VALID;
        }
    }
    return values;
}

std::vector<Position> arrange(Spec spec, double width, double height, const Options& options) {
    const int count = spec.rows == 2 ? 4 : spec.columns, columns = spec.rows == 2 ? 2 : count;
    const double areaWidth = width / columns, areaHeight = height / spec.rows;
    // The grid fits the screen, so a nominal 72px cell can be ~70px.
    // The 48px gauge includes transparent padding: its drawn diameter is 44.1px.
    // Keep 14px typography through that small adjustment instead of scaling only
    // 1x1.
    const double scale = std::max(0.0, std::min({1.0, areaWidth / 46, areaHeight / 46}));
    int primary = options.primary == "ram"           ? 1
                  : options.primary == "gpu"         ? 2
                  : options.primary == "temperature" ? 3
                                                     : 0;
    std::vector<Position> positions;
    for (int i = 0; i < count; ++i)
        positions.push_back({(i % columns) * areaWidth + (areaWidth - 48 * scale) / 2,
                             (i / columns) * areaHeight + (areaHeight - 48 * scale) / 2, scale,
                             count == 1 ? primary : i});
    return positions;
}

}  // namespace monitorplugin
