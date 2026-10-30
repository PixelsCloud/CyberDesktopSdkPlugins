#include <sdk/cpp/Plugin.h>

#include "plugins/steampunk-desk/MarbleClock.h"

extern "C" CD_PLUGIN_EXPORT CdResult CD_CALL cd_plugin_query(uint32_t abi, CdPluginApi* output, uint32_t size) {
    return cdsdk::PluginAdapter<steamdesk::MarbleInstance>::query(abi, output, size, CD_STEAM_DESK_ID, "1.0.0");
}
