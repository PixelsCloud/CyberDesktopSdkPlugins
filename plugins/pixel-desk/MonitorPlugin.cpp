#include "plugins/pixel-desk/PixelMonitor.h"

extern "C" CD_PLUGIN_EXPORT CdResult CD_CALL cd_plugin_query(uint32_t abi, CdPluginApi* output, uint32_t size) {
    if (abi != CD_PLUGIN_ABI_VERSION) {
        return CD_ERROR_VERSION;
    }
    if (!output || size < sizeof(CdPluginApi)) {
        return CD_ERROR_BUFFER;
    }
    *output = {sizeof(CdPluginApi), CD_PLUGIN_ABI_VERSION, CD_PIXEL_DESK_ID, CD_PIXEL_DESK_VERSION,
               pixeldesk::createMonitor};
    return CD_OK;
}
