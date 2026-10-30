#include "plugins/system-info-suite/Monitor.h"
#ifndef CD_MONITOR_ID
#error Monitor identity is required
#endif
namespace {
constexpr monitorplugin::Spec spec{CD_MONITOR_ID, CD_MONITOR_ROWS, CD_MONITOR_COLUMNS};

CdResult CD_CALL create(const CdCreateContext* context, CdInstanceApi* out, uint32_t size) {
    return monitorplugin::create(spec, context, out, size);
}
}  // namespace

extern "C" CD_PLUGIN_EXPORT CdResult CD_CALL cd_plugin_query(uint32_t abi, CdPluginApi* out, uint32_t size) {
    if (abi != CD_PLUGIN_ABI_VERSION)
        return CD_ERROR_VERSION;
    if (!out || size < sizeof(CdPluginApi))
        return CD_ERROR_BUFFER;
    *out = {sizeof(CdPluginApi), CD_PLUGIN_ABI_VERSION, CD_MONITOR_ID, "1.0.0", create};
    return CD_OK;
}
