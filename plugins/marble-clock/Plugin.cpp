#include <sdk/cpp/Plugin.h>

#include "plugins/marble-clock/Clock.h"

extern "C" CD_PLUGIN_EXPORT CdResult CD_CALL cd_plugin_query(uint32_t abi, CdPluginApi* output, uint32_t size) {
    return cdsdk::PluginAdapter<marbleclock::Instance>::query(abi, output, size, CD_MARBLE_ID, "1.2.0");
}
