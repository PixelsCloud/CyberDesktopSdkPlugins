#include <sdk/cpp/Plugin.h>

#include "plugins/mechanical-sign/Sign.h"

extern "C" CD_PLUGIN_EXPORT CdResult CD_CALL cd_plugin_query(uint32_t abi, CdPluginApi* output, uint32_t size) {
    return cdsdk::PluginAdapter<mechanicalsign::Instance>::query(abi, output, size, mechanicalsign::kId, "1.1.0");
}
