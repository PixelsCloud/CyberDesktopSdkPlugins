#include "plugins/clock-suite/Clock.h"
#ifndef CD_CLOCK_ID
#error Clock plugin identity is required
#endif
#ifndef CD_CLOCK_FLIP
#define CD_CLOCK_FLIP 0
#endif
#ifndef CD_CLOCK_DISPLAY
#define CD_CLOCK_DISPLAY 0
#endif
namespace {
constexpr clockplugin::Spec specification{CD_CLOCK_ID,        CD_CLOCK_ANALOG != 0,
                                          CD_CLOCK_ROWS,      CD_CLOCK_COLUMNS,
                                          CD_CLOCK_FLIP != 0, clockplugin::Display(CD_CLOCK_DISPLAY)};

CdResult CD_CALL create(const CdCreateContext* context, CdInstanceApi* output, uint32_t size) {
    return clockplugin::create(specification, context, output, size);
}
}  // namespace

extern "C" CD_PLUGIN_EXPORT CdResult CD_CALL cd_plugin_query(uint32_t abi, CdPluginApi* output, uint32_t size) {
    if (abi != CD_PLUGIN_ABI_VERSION)
        return CD_ERROR_VERSION;
    if (!output || size < sizeof(CdPluginApi))
        return CD_ERROR_BUFFER;
    *output = {sizeof(CdPluginApi), CD_PLUGIN_ABI_VERSION, CD_CLOCK_ID, "1.0.0", create};
    return CD_OK;
}
