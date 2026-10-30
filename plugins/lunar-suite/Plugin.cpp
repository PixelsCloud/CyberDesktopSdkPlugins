#include "plugins/lunar-suite/Lunar.h"

namespace {
CdResult CD_CALL create(const CdCreateContext* context, CdInstanceApi* out, uint32_t size) {
    return lunarplugin::create({CD_LUNAR_ID, CD_LUNAR_ROWS, CD_LUNAR_COLUMNS}, context, out, size);
}
}  // namespace

extern "C" CD_PLUGIN_EXPORT CdResult CD_CALL cd_plugin_query(uint32_t abi, CdPluginApi* out, uint32_t size) {
    if (abi != CD_PLUGIN_ABI_VERSION) {
        return CD_ERROR_VERSION;
    }
    if (!out || size < sizeof(*out)) {
        return CD_ERROR_BUFFER;
    }
    *out = {sizeof(*out), CD_PLUGIN_ABI_VERSION, CD_LUNAR_ID, "1.0.0", create};
    return CD_OK;
}
