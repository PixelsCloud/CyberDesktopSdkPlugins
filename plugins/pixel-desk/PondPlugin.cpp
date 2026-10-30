#include <sdk/cpp/Plugin.h>

#include "plugins/pixel-desk/PixelPond.h"

extern "C" CD_PLUGIN_EXPORT CdResult CD_CALL cd_plugin_query(uint32_t abi, CdPluginApi* output, uint32_t size) {
    return cdsdk::PluginAdapter<pixeldesk::PondInstance>::query(abi, output, size, CD_PIXEL_DESK_ID,
                                                                CD_PIXEL_DESK_VERSION);
}
