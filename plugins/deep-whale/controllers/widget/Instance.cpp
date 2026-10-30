#include "plugins/deep-whale/controllers/widget/Widget.h"

namespace deep_widget {
namespace {
std::unique_ptr<widgetplugin::themed::View> makeView(Spec) {
    return std::make_unique<Canvas>();
}
}  // namespace

CdResult create(Spec spec, const CdCreateContext* context, CdInstanceApi* out, uint32_t size) {
    return widgetplugin::themed::create(spec, context, out, size, makeView);
}
}  // namespace deep_widget
