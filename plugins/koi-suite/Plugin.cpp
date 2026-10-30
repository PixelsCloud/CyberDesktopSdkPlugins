#include "plugins/koi-suite/Pond.h"

namespace {
CdResult CD_CALL create(const CdCreateContext* context, CdInstanceApi* output, uint32_t size) noexcept {
    return koiplugin::create({CD_KOI_ID, CD_KOI_ROWS, CD_KOI_COLUMNS, CD_KOI_COUNT}, context, output, size);
}
}  // namespace

extern "C" CD_PLUGIN_EXPORT CdResult CD_CALL cd_plugin_query(uint32_t abi, CdPluginApi* output, uint32_t size) {
    if (abi != CD_PLUGIN_ABI_VERSION) {
        return CD_ERROR_VERSION;
    }
    if (!output || size < sizeof(CdPluginApi)) {
        return CD_ERROR_BUFFER;
    }
    *output = {sizeof(CdPluginApi), CD_PLUGIN_ABI_VERSION, CD_KOI_ID, "1.1.7", create};
    return CD_OK;
}
