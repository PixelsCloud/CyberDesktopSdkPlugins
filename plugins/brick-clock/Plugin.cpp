#include <sdk/cpp/Plugin.h>

#include "plugins/brick-clock/Clock.h"

extern "C" CD_PLUGIN_EXPORT CdResult CD_CALL cd_plugin_query(uint32_t abi, CdPluginApi* output, uint32_t size) {
    return cdsdk::PluginAdapter<brickclock::Instance>::query(abi, output, size, "com.cyberdesktop.clock.brick.1x4",
                                                             "1.0.1");
}
