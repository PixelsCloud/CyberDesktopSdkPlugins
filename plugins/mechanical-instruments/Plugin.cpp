#if CD_MECHANICAL_CORE == 0
#include "Monitor.h"
namespace implementation = monitorplugin;
constexpr implementation::Spec kSpec{CD_MECHANICAL_ID, CD_MECHANICAL_ROWS, CD_MECHANICAL_COLUMNS, true};
#elif CD_MECHANICAL_CORE == 1
#include "Productivity.h"
namespace implementation = productivity;
constexpr implementation::Spec kSpec{CD_MECHANICAL_ID, implementation::Kind(CD_MECHANICAL_KIND), CD_MECHANICAL_ROWS,
                                     CD_MECHANICAL_COLUMNS, true};
#elif CD_MECHANICAL_CORE == 2
#include "Weather.h"
namespace implementation = weatherplugin;
constexpr implementation::Spec kSpec{CD_MECHANICAL_ID, CD_MECHANICAL_ROWS, CD_MECHANICAL_COLUMNS, true};
#elif CD_MECHANICAL_CORE == 3
#include "Widget.h"
namespace implementation = widgetplugin;
constexpr implementation::Spec kSpec{CD_MECHANICAL_ID, implementation::Kind(CD_MECHANICAL_KIND), CD_MECHANICAL_ROWS,
                                     CD_MECHANICAL_COLUMNS, true};
#elif CD_MECHANICAL_CORE == 4
#include "AirQuality.h"
namespace implementation = airplugin;
constexpr implementation::Spec kSpec{CD_MECHANICAL_ID, CD_MECHANICAL_ROWS, CD_MECHANICAL_COLUMNS, true};
#elif CD_MECHANICAL_CORE == 5
#include "Lunar.h"
namespace implementation = lunarplugin;
constexpr implementation::Spec kSpec{CD_MECHANICAL_ID, CD_MECHANICAL_ROWS, CD_MECHANICAL_COLUMNS, true};
#else
#error Mechanical instrument core is required
#endif

namespace {
CdResult CD_CALL create(const CdCreateContext* context, CdInstanceApi* output, uint32_t size) {
    return implementation::create(kSpec, context, output, size);
}
}  // namespace

extern "C" CD_PLUGIN_EXPORT CdResult CD_CALL cd_plugin_query(uint32_t abi, CdPluginApi* output, uint32_t size) {
    if (abi != CD_PLUGIN_ABI_VERSION) {
        return CD_ERROR_VERSION;
    }
    if (!output || size < sizeof(CdPluginApi)) {
        return CD_ERROR_BUFFER;
    }
    *output = {sizeof(CdPluginApi), CD_PLUGIN_ABI_VERSION, CD_MECHANICAL_ID, CD_MECHANICAL_VERSION, create};
    return CD_OK;
}
