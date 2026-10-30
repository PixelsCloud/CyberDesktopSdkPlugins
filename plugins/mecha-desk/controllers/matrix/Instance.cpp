#include <memory>

#include "plugins/mecha-desk/Skin.h"
#include "plugins/mecha-desk/controllers/matrix/Pixels.h"

namespace aeris_matrix {
Instance::Instance(const CdCreateContext& context)
    : mechanicalpixels::Instance(context, specification(context.plugin_id), std::make_unique<Renderer>(context)) {
    const auto kind = specification(context.plugin_id).kind;
    if (isClock(kind)) {
        const CdSettingValue colors[]{{sizeof(CdSettingValue), CD_SETTING_COLOR, "text_color", 0xbde9dfff},
                                      {sizeof(CdSettingValue), CD_SETTING_COLOR, "seconds_color", 0xbde9dfff}};
        settings.apply(colors, 2);
    } else {
        const CdSettingValue palette[]{
            {sizeof(CdSettingValue), CD_SETTING_COLOR, "color", aerisskin::kIvory},
            {sizeof(CdSettingValue), CD_SETTING_COLOR, "color_top", aerisskin::kIvory},
            {sizeof(CdSettingValue), CD_SETTING_COLOR, isRound(kind) ? "peak_color" : "color_middle",
             isRound(kind) ? aerisskin::kRuby : aerisskin::kIvory}};
        settings.apply(palette, 3);
    }
}
}  // namespace aeris_matrix
