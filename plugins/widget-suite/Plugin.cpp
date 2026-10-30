#include "plugins/widget-suite/Widget.h"

namespace {
constexpr widgetplugin::Spec spec{CD_WIDGET_ID, widgetplugin::Kind(CD_WIDGET_KIND), CD_WIDGET_ROWS, CD_WIDGET_COLUMNS};

CdResult CD_CALL create(const CdCreateContext* context, CdInstanceApi* out, uint32_t size) {
    return widgetplugin::create(spec, context, out, size);
}
}  // namespace

extern "C" CD_PLUGIN_EXPORT CdResult CD_CALL cd_plugin_query(uint32_t abi, CdPluginApi* out, uint32_t size) {
    if (abi != CD_PLUGIN_ABI_VERSION)
        return CD_ERROR_VERSION;
    if (!out || size < sizeof(CdPluginApi))
        return CD_ERROR_BUFFER;
    *out = {sizeof(CdPluginApi), CD_PLUGIN_ABI_VERSION, CD_WIDGET_ID,
            spec.kind == widgetplugin::Kind::Player ? "1.0.1" : "1.0.0", create};
    return CD_OK;
}
