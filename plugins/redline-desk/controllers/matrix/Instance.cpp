#include <memory>

#include "plugins/redline-desk/Skin.h"
#include "plugins/redline-desk/controllers/matrix/Pixels.h"

namespace redline_matrix {
Instance::Instance(const CdCreateContext& context)
    : mechanicalpixels::Instance(context, specification(context.plugin_id), std::make_unique<Renderer>(context)) {
    const auto kind = specification(context.plugin_id).kind;
    if (isClock(kind)) {
        const CdSettingValue colors[]{{sizeof(CdSettingValue), CD_SETTING_COLOR, "text_color", redline::kIvory},
                                      {sizeof(CdSettingValue), CD_SETTING_COLOR, "seconds_color", redline::kRuby}};
        settings.apply(colors, 2);
    } else {
        const CdSettingValue palette[]{
            {sizeof(CdSettingValue), CD_SETTING_COLOR, "color", redline::kIvory},
            {sizeof(CdSettingValue), CD_SETTING_COLOR, "color_top", redline::kIvory},
            {sizeof(CdSettingValue), CD_SETTING_COLOR, isRound(kind) ? "peak_color" : "color_middle",
             isRound(kind) ? redline::kRuby : redline::kIvory}};
        settings.apply(palette, 3);
    }
}
}  // namespace redline_matrix
