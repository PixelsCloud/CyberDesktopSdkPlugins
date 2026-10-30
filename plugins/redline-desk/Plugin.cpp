#include "plugins/common/RuntimeLibrary.h"
#include "plugins/redline-desk/Runtime.h"

namespace {
CdResult CD_CALL create(const CdCreateContext* context, CdInstanceApi* output, uint32_t size) try {
    if (!context || !context->plugin_directory) {
        return CD_ERROR_ARGUMENT;
    }
    static cdplugin::RuntimeLibrary runtime(context->plugin_directory, L"cyberdesktop_redline_runtime.dll");
    const auto factory = reinterpret_cast<decltype(&cd_redline_create)>(runtime.symbol("cd_redline_create"));
    return factory ? factory(CD_REDLINE_FAMILY, CD_REDLINE_SOURCE, CD_REDLINE_KIND, CD_REDLINE_ROWS, CD_REDLINE_COLUMNS,
                             context, output, size)
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
    *output = {sizeof(*output), CD_PLUGIN_ABI_VERSION, CD_REDLINE_ID, "1.0.0", create};
    return CD_OK;
}
