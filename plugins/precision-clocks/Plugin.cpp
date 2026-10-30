#include <sdk/cpp/Plugin.h>

#include "plugins/precision-clocks/Clock.h"

extern "C" CD_PLUGIN_EXPORT CdResult CD_CALL cd_plugin_query(uint32_t abi, CdPluginApi* output, uint32_t size) {
    return cdsdk::PluginAdapter<precisionclock::Instance>::query(
        abi, output, size, precisionclock::specification(precisionclock::Kind(CD_PRECISION_KIND)).id, "1.0.1");
}
