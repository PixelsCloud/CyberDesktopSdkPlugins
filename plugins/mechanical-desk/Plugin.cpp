#include "plugins/mechanical-desk/Runtime.h"

namespace {
CdResult CD_CALL create(const CdCreateContext* context, CdInstanceApi* output, uint32_t size) {
    return cd_mechanical_desk_create(CD_MECHANICAL_DESK_FAMILY, CD_MECHANICAL_DESK_KIND, CD_MECHANICAL_DESK_ROWS,
                                     CD_MECHANICAL_DESK_COLUMNS, context, output, size);
}
}  // namespace

extern "C" CD_PLUGIN_EXPORT CdResult CD_CALL cd_plugin_query(uint32_t abi, CdPluginApi* output, uint32_t size) {
    if (abi != CD_PLUGIN_ABI_VERSION) {
        return CD_ERROR_VERSION;
    }
    if (!output || size < sizeof(*output)) {
        return CD_ERROR_BUFFER;
    }
    *output = {sizeof(*output), CD_PLUGIN_ABI_VERSION, CD_MECHANICAL_DESK_ID, "1.0.0", create};
    return CD_OK;
}
