#include "plugins/weather-suite/Weather.h"

namespace {
constexpr weatherplugin::Spec spec{CD_WEATHER_ID, CD_WEATHER_ROWS, CD_WEATHER_COLUMNS};

CdResult CD_CALL create(const CdCreateContext* context, CdInstanceApi* out, uint32_t size) {
    return weatherplugin::create(spec, context, out, size);
}
}  // namespace

extern "C" CD_PLUGIN_EXPORT CdResult CD_CALL cd_plugin_query(uint32_t abi, CdPluginApi* out, uint32_t size) {
    if (abi != CD_PLUGIN_ABI_VERSION)
        return CD_ERROR_VERSION;
    if (!out || size < sizeof(CdPluginApi))
        return CD_ERROR_BUFFER;
    *out = {sizeof(CdPluginApi), CD_PLUGIN_ABI_VERSION, CD_WEATHER_ID, "1.0.1", create};
    return CD_OK;
}
