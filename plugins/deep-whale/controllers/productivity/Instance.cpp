#include <memory>

#include "plugins/deep-whale/controllers/productivity/Productivity.h"

namespace deep_productivity {
namespace {
std::unique_ptr<productivity::themed::View> makeView() {
    return std::make_unique<Renderer>();
}
}  // namespace

CdResult create(Spec spec, const CdCreateContext* context, CdInstanceApi* out, uint32_t size) {
    return productivity::themed::create(spec, context, out, size, makeView);
}
}  // namespace deep_productivity
