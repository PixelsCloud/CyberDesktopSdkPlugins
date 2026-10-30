#include <memory>

#include "plugins/vampire-alice/Skin.h"
#include "plugins/vampire-alice/controllers/matrix/Pixels.h"

namespace alice_matrix {
Instance::Instance(const CdCreateContext& context)
    : mechanicalpixels::Instance(context, specification(context.plugin_id), std::make_unique<Renderer>(context)) {
    const auto kind = specification(context.plugin_id).kind;
    if (!isClock(kind)) {
        const CdSettingValue palette[]{
            {sizeof(CdSettingValue), CD_SETTING_COLOR, "color", alice::kIvory},
            {sizeof(CdSettingValue), CD_SETTING_COLOR, "color_top", alice::kRuby},
            {sizeof(CdSettingValue), CD_SETTING_COLOR, isRound(kind) ? "peak_color" : "color_middle",
             isRound(kind) ? alice::kRuby : 0xffc68fff}};
        settings.apply(palette, 3);
    }
}
}  // namespace alice_matrix
