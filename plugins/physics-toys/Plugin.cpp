#include <sdk/cpp/Plugin.h>

#include "plugins/physics-toys/Toys.h"

extern "C" CD_PLUGIN_EXPORT CdResult CD_CALL cd_plugin_query(uint32_t abi, CdPluginApi* output, uint32_t size) {
    return cdsdk::PluginAdapter<toys::Plugin<CD_TOY_KIND, CD_TOY_MECHANICAL != 0>>::query(abi, output, size, CD_TOY_ID,
                                                                                          CD_TOY_VERSION);
}
