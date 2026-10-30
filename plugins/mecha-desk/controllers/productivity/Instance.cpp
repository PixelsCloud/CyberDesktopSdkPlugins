#include <memory>

#include "plugins/mecha-desk/controllers/productivity/Productivity.h"

namespace aeris_productivity {
namespace {
std::unique_ptr<productivity::themed::View> makeView() {
    return std::make_unique<Renderer>();
}
}  // namespace

CdResult create(Spec spec, const CdCreateContext* context, CdInstanceApi* out, uint32_t size) {
    return productivity::themed::create(spec, context, out, size, makeView);
}
}  // namespace aeris_productivity
