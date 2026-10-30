#include "plugins/ambience-suite/Atmosphere.h"

namespace {
CdResult CD_CALL create(const CdCreateContext* context, CdInstanceApi* out, uint32_t size) {
    return atmosphere::create(
        {CD_AMBIENCE_ID, atmosphere::Kind(CD_AMBIENCE_KIND), CD_AMBIENCE_ROWS, CD_AMBIENCE_COLUMNS}, context, out,
        size);
}
}  // namespace

extern "C" CD_PLUGIN_EXPORT CdResult CD_CALL cd_plugin_query(uint32_t abi, CdPluginApi* out, uint32_t size) {
    if (abi != CD_PLUGIN_ABI_VERSION) {
        return CD_ERROR_VERSION;
    }
    if (!out || size < sizeof(*out)) {
        return CD_ERROR_BUFFER;
    }
    *out = {sizeof(*out), CD_PLUGIN_ABI_VERSION, CD_AMBIENCE_ID, CD_AMBIENCE_VERSION, create};
    return CD_OK;
}
