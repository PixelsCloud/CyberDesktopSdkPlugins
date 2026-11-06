#include "sdk/ImageTracing.h"

#include <CyberCanvas/ImageTracing.h>

#include <atomic>

#include "sdk/services/ImageTrace.h"

namespace cdsdk {
namespace {
std::atomic<CdImageTraceObserve> hostObserver{nullptr};

void* observe(const cybercanvas::ImageTraceDescription& value) noexcept {
    const auto callback = hostObserver.load(std::memory_order_acquire);
    if (!callback) {
        return nullptr;
    }
    const CdImageTraceResource resource{sizeof(resource), value.domain, value.identity, value.bytes,
                                        value.width,      value.height, value.stride,   value.format,
                                        value.source,     value.file,   value.line};
    return callback(&resource);
}
}  // namespace

void bindImageTracing(const CdCreateContext& context) noexcept {
    if (!context.host || context.host->struct_size < sizeof(CdServiceHostApi)) {
        return;
    }
    const auto* host = reinterpret_cast<const CdServiceHostApi*>(context.host);
    CdImageTraceApi api{};
    if (host->query_service &&
        host->query_service(context.host_context, CD_SERVICE_IMAGE_TRACE, 1, &api, sizeof(api)) == CD_OK &&
        api.struct_size >= sizeof(api) && api.version == 1 && api.observe) {
        hostObserver.store(api.observe, std::memory_order_release);
        cybercanvas::configureImageTracing(observe);
    }
}
}  // namespace cdsdk
