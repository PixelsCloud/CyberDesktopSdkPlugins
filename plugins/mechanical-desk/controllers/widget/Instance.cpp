#include "plugins/mechanical-desk/controllers/widget/Widget.h"

namespace mechanicaldesk_widget {
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

cybercanvas::Rect Canvas::photoRegion(CdRect safe) const {
    return {0, 0, safe.width, safe.height};
}

}  // namespace mechanicaldesk_widget
