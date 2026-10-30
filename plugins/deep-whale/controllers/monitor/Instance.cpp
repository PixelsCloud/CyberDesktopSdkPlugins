#include <memory>

#include "plugins/deep-whale/controllers/monitor/Monitor.h"

namespace deep_monitor {
namespace {
std::unique_ptr<monitorplugin::themed::View> makeView() {
    return std::make_unique<Renderer>();
}
}  // namespace

CdResult create(Spec spec, const CdCreateContext* context, CdInstanceApi* out, uint32_t size) {
    return monitorplugin::themed::create(spec, context, out, size, makeView, true);
}
}  // namespace deep_monitor
