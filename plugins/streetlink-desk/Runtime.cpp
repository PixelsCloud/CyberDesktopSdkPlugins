#include "plugins/streetlink-desk/Runtime.h"

#include <sdk/cpp/Plugin.h>

#include "plugins/mechanical-pixels/Pixels.h"
#include "plugins/streetlink-desk/MatrixFace.h"
#include "plugins/streetlink-desk/Physics.h"

namespace {
const mechanicalpixels::Spec& source(std::string_view id) {
    if (id == "com.cyberdesktop.streetlink.clock.square.4x1") {
        return mechanicalpixels::kCatalog[0];
    }
    if (id == "com.cyberdesktop.streetlink.clock.round.4x1") {
        return mechanicalpixels::kCatalog[1];
    }
    throw std::invalid_argument("Unknown STREETLINK plugin ID");
}

class Instance final : public mechanicalpixels::Instance {
public:
    explicit Instance(const CdCreateContext& context)
        : mechanicalpixels::Instance(context, source(context.plugin_id),
                                     std::make_unique<streetlink::MatrixFace>(context)) {
        const CdSettingValue colors[]{{sizeof(CdSettingValue), CD_SETTING_COLOR, "text_color", 0xffffffff},
                                      {sizeof(CdSettingValue), CD_SETTING_COLOR, "seconds_color", 0xffffffff},
                                      {sizeof(CdSettingValue), CD_SETTING_NUMBER, "glow", 0, 0}};
        if (applySettings(colors, uint32_t(std::size(colors))) != CD_OK) {
            throw std::runtime_error("STREETLINK clock defaults rejected");
        }
    }
};
}  // namespace

extern "C" CD_STREETLINK_API CdResult CD_CALL cd_streetlink_create(const CdCreateContext* context,
                                                                   CdInstanceApi* output, uint32_t size) {
    if (!context || context->struct_size < sizeof(*context) || !context->plugin_id || !context->host ||
        !context->plugin_directory || !output || size < sizeof(*output)) {
        return CD_ERROR_ARGUMENT;
    }
    CdPluginApi api{};
    const std::string_view id(context->plugin_id);
    if (id.find("clock.marble") != std::string_view::npos || id.find("beat-machine") != std::string_view::npos) {
        const auto result = cdsdk::PluginAdapter<streetlink::Physics>::query(CD_PLUGIN_ABI_VERSION, &api, sizeof(api),
                                                                             context->plugin_id, "1.0.0");
        return result == CD_OK ? api.create(context, output, size) : result;
    }
    const auto result =
        cdsdk::PluginAdapter<Instance>::query(CD_PLUGIN_ABI_VERSION, &api, sizeof(api), context->plugin_id, "1.0.0");
    return result == CD_OK ? api.create(context, output, size) : result;
}
