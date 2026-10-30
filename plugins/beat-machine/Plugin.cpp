#include <sdk/cpp/Plugin.h>

#include "plugins/beat-machine/Machine.h"

extern "C" CD_PLUGIN_EXPORT CdResult CD_CALL cd_plugin_query(uint32_t abi, CdPluginApi* output, uint32_t size) {
    return cdsdk::PluginAdapter<beatmachine::Instance>::query(abi, output, size, CD_BEAT_ID, "1.1.0");
}
