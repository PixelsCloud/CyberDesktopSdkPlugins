#include "plugins/hourglass-suite/Hourglass.h"

namespace {
CdResult CD_CALL create(const CdCreateContext* context, CdInstanceApi* output, uint32_t size) noexcept {
    return hourglassplugin::create({CD_HOURGLASS_ID, CD_HOURGLASS_SHOW_TIMER != 0}, context, output, size);
}
}  // namespace

extern "C" CD_PLUGIN_EXPORT CdResult CD_CALL cd_plugin_query(uint32_t abi, CdPluginApi* output, uint32_t size) {
    if (abi != CD_PLUGIN_ABI_VERSION) {
        return CD_ERROR_VERSION;
    }
    if (!output || size < sizeof(CdPluginApi)) {
        return CD_ERROR_BUFFER;
    }
    *output = {sizeof(CdPluginApi), CD_PLUGIN_ABI_VERSION, CD_HOURGLASS_ID, "1.2.1", create};
    return CD_OK;
}
