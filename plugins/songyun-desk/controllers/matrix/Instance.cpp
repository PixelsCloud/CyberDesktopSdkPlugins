#include <memory>

#include "plugins/songyun-desk/Skin.h"
#include "plugins/songyun-desk/controllers/matrix/Pixels.h"

namespace songyun_matrix {
Instance::Instance(const CdCreateContext& context)
    : mechanicalpixels::Instance(context, specification(context.plugin_id), std::make_unique<Renderer>(context)) {
    const auto kind = specification(context.plugin_id).kind;
    if (isClock(kind)) {
        const CdSettingValue colors[]{{sizeof(CdSettingValue), CD_SETTING_COLOR, "text_color", songyun::kJade},
                                      {sizeof(CdSettingValue), CD_SETTING_COLOR, "seconds_color", songyun::kJade}};
        settings.apply(colors, 2);
    } else {
        const CdSettingValue palette[]{
            {sizeof(CdSettingValue), CD_SETTING_COLOR, "color", songyun::kJade},
            {sizeof(CdSettingValue), CD_SETTING_COLOR, "color_top", 0xe5dfafff},
            {sizeof(CdSettingValue), CD_SETTING_COLOR, isRound(kind) ? "peak_color" : "color_middle",
             isRound(kind) ? songyun::kJade : 0xd3dda6ff}};
        settings.apply(palette, 3);
    }
}
}  // namespace songyun_matrix
