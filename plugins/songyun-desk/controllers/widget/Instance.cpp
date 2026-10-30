#include "plugins/songyun-desk/controllers/widget/Widget.h"

namespace songyun_widget {
namespace {
std::unique_ptr<widgetplugin::themed::View> makeView(Spec) {
    return std::make_unique<Canvas>();
}
}  // namespace

CdResult create(Spec spec, const CdCreateContext* context, CdInstanceApi* out, uint32_t size) {
    return themed::create(spec, context, out, size, makeView, {0x11CADFFF, 0x46E9B1FF, false});
}

bool Canvas::photoHit(CdRect safe, const cdsdk::Settings& settings, const CdInput& input) const {
    return themed::albumPhotoHit(safe, settings.at("counter").integer != 0, input);
}

}  // namespace songyun_widget
