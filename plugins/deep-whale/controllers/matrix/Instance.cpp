#include <memory>

#include "plugins/deep-whale/Skin.h"
#include "plugins/deep-whale/controllers/matrix/Pixels.h"

namespace deep_matrix {
Instance::Instance(const CdCreateContext& context)
    : mechanicalpixels::Instance(context, specification(context.plugin_id), std::make_unique<Renderer>(context)) {
    const auto kind = specification(context.plugin_id).kind;
    if (!isClock(kind)) {
        const CdSettingValue palette[]{
            {sizeof(CdSettingValue), CD_SETTING_COLOR, "color", whaleskin::kIvory},
            {sizeof(CdSettingValue), CD_SETTING_COLOR, "color_top", whaleskin::kRuby},
            {sizeof(CdSettingValue), CD_SETTING_COLOR, isRound(kind) ? "peak_color" : "color_middle",
             isRound(kind) ? whaleskin::kRuby : 0xffc68fff}};
        settings.apply(palette, 3);
    }
}
}  // namespace deep_matrix
