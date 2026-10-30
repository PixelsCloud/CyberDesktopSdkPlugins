#include <memory>

#include "plugins/songyun-desk/controllers/monitor/Monitor.h"

namespace songyun_monitor {
namespace {
std::unique_ptr<monitorplugin::themed::View> makeView() {
    return std::make_unique<Renderer>();
}
}  // namespace

CdResult create(Spec spec, const CdCreateContext* context, CdInstanceApi* out, uint32_t size) {
    return monitorplugin::themed::create(spec, context, out, size, makeView, true);
}
}  // namespace songyun_monitor
