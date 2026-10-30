#include <sdk/cpp/Plugin.h>

#include "plugins/mechanical-hourglass/MechanicalHourglass.h"

extern "C" CD_PLUGIN_EXPORT CdResult CD_CALL cd_plugin_query(uint32_t abi, CdPluginApi* output, uint32_t size) {
    return cdsdk::PluginAdapter<mechanicalhourglass::Instance>::query(abi, output, size, CD_MECHANICAL_HOURGLASS_ID,
                                                                      "1.0.0");
}
