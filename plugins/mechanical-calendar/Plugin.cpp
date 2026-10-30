#include <sdk/cpp/Plugin.h>

#include "plugins/mechanical-calendar/Calendar.h"

extern "C" CD_PLUGIN_EXPORT CdResult CD_CALL cd_plugin_query(uint32_t abi, CdPluginApi* output, uint32_t size) {
    return cdsdk::PluginAdapter<mechanicalcalendar::Instance>::query(abi, output, size, mechanicalcalendar::kPluginId,
                                                                     "1.0.2");
}
