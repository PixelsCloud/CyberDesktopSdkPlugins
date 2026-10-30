#include <algorithm>

#include "plugins/redline-desk/controllers/widget/Widget.h"

namespace redline_widget {
namespace {
std::unique_ptr<widgetplugin::themed::View> makeView(Spec) {
    return std::make_unique<Canvas>();
}
}  // namespace

CdResult create(Spec spec, const CdCreateContext* context, CdInstanceApi* out, uint32_t size) {
    return themed::create(spec, context, out, size, makeView, {0x11CADFFF, 0x46E9B1FF, false});
}

bool Canvas::photoHit(CdRect safe, const cdsdk::Settings& settings, const CdInput& input) const {
    const auto photo = photoRegion(safe);
    return input.x >= photo.x && input.y >= photo.y && input.x < photo.x + photo.width &&
           input.y < photo.y + photo.height;
}

themed::PlayerPointer Canvas::playerPointer(Spec, CdRect safe, const cdsdk::Settings& settings, const Media& media,
                                            const CdInput& input) const {
    const auto timeline = displayRegion("progress", safe);
    const bool seekable = settings.at("show_progress").integer && !media.source.empty() &&
                          (media.controls & CD_MEDIA_CAN_SEEK) && media.duration > 0;
    const bool hit = seekable && input.x >= timeline.x && input.y >= timeline.y &&
                     input.x < timeline.x + timeline.width && input.y < timeline.y + timeline.height;
    return {hit ? 4 : 0, hit ? std::clamp((input.x - timeline.x) / timeline.width, 0.0, 1.0) * media.duration : 0};
}

}  // namespace redline_widget
