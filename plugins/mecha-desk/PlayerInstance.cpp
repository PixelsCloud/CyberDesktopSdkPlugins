#include <memory>
#include <stdexcept>

#include "plugins/mecha-desk/Player.h"

namespace aeris_player {
const PlayerSpec& playerSpec(std::string_view id) {
    for (const auto& spec : kPlayerCatalog) {
        if (id == spec.id) {
            return spec;
        }
    }
    throw std::invalid_argument("Unknown AERIS POLAR media player");
}

widgetplugin::Media pixelMedia(const cdsdk::MediaSnapshot& value, const widgetplugin::Media& previous) {
    return widgetplugin::media::fromSnapshot(value, previous);
}

cybercanvas::Rect PlayerRenderer::seekRegion(const PlayerSpec& spec, double width, double height) const {
    return timeline({0, 0, width, height});
}

PlayerInstance::PlayerInstance(const CdCreateContext& context)
    : widgetplugin::media::Controller(context, playerSpec(context.plugin_id),
                                      std::make_unique<PlayerRenderer>(context, playerSpec(context.plugin_id))) {}
}  // namespace aeris_player
