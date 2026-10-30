#include "plugins/common/RuntimeLibrary.h"
#include "plugins/mecha-desk/Runtime.h"

namespace {
CdResult CD_CALL create(const CdCreateContext* context, CdInstanceApi* output, uint32_t size) try {
    if (!context || !context->plugin_directory) {
        return CD_ERROR_ARGUMENT;
    }
    static cdplugin::RuntimeLibrary runtime(context->plugin_directory, L"cyberdesktop_mecha_runtime.dll");
    const auto factory = reinterpret_cast<decltype(&cd_mecha_create)>(runtime.symbol("cd_mecha_create"));
    return factory ? factory(CD_MECHA_FAMILY, CD_MECHA_SOURCE, CD_MECHA_KIND, CD_MECHA_ROWS, CD_MECHA_COLUMNS, context,
                             output, size)
                   : CD_ERROR_UNSUPPORTED;
} catch (...) {
    return CD_ERROR_INTERNAL;
}
}  // namespace

extern "C" CD_PLUGIN_EXPORT CdResult CD_CALL cd_plugin_query(uint32_t abi, CdPluginApi* output, uint32_t size) {
    if (abi != CD_PLUGIN_ABI_VERSION) {
        return CD_ERROR_VERSION;
    }
    if (!output || size < sizeof(*output)) {
        return CD_ERROR_BUFFER;
    }
    *output = {sizeof(*output), CD_PLUGIN_ABI_VERSION, CD_MECHA_ID, "1.0.0", create};
    return CD_OK;
}
