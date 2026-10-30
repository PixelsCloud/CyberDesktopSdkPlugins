#include "sdk/services/RenderOutput.h"

#include <CyberCanvas/ResourceAccount.h>
#include <sdk/RenderOutput.h>

#include <algorithm>
#include <cstring>
#include <tuple>
#include <vector>

#include "sdk/services/RenderMemory.h"

namespace cdsdk {
class OutputBinding::Impl {
public:
    void bind(const CdCreateContext& context) {
        context_ = context.host_context;
        api_ = {};
        memory_ = {};
        if (!context.host || context.host->struct_size < sizeof(CdServiceHostApi)) {
            return;
        }
        const auto* host = reinterpret_cast<const CdServiceHostApi*>(context.host);
        if (!host->query_service) {
            return;
        }
        if (host->query_service(context_, CD_SERVICE_RENDER_MEMORY, 1, &memory_, sizeof(memory_)) != CD_OK ||
            memory_.struct_size < sizeof(memory_) || memory_.version != 1 || !memory_.reserve) {
            memory_ = {};
        }
        if (host->query_service(context_, CD_SERVICE_RENDER_OUTPUT, 1, &api_, sizeof(api_)) == CD_OK &&
            api_.struct_size >= sizeof(api_) && api_.version == 1 && api_.configure && api_.session && api_.complete &&
            api_.configure(context_) == CD_OK) {
            return;
        }
        api_ = {};
    }

    CdRenderSession session(const CdFrame& frame) const {
        CdRenderSession value{sizeof(value), 1};
        if (api_.session && api_.session(context_, &frame, &value) == CD_OK && value.struct_size >= sizeof(value) &&
            value.version == 1 && value.backend <= CD_OUTPUT_GPU) {
            return value;
        }
        value = {sizeof(value), 1};
        return value;
    }

    void complete(const CdRenderSession& session, uint32_t result, uint64_t copies = 0, uint64_t batches = 0,
                  uint64_t instances = 0) const {
        if (api_.complete) {
            const CdRenderReport report{sizeof(report),       1,      result,  session.backend,
                                        session.output_epoch, copies, batches, instances};
            api_.complete(context_, &report);
        }
    }

    cybercanvas::ResourceAccount account() {
        if (!memory_.reserve) {
            return {};
        }
        return {memory_.resource_context, memory_.reserve};
    }

private:
    CdRenderOutputApi api_{};
    CdRenderMemoryApi memory_{};
    void* context_ = nullptr;
};

void OutputBinding::bind(const CdCreateContext& context) {
    return impl_->bind(context);
}

CdRenderSession OutputBinding::session(const CdFrame& frame) const {
    return impl_->session(frame);
}

void OutputBinding::complete(const CdRenderSession& session, uint32_t result, uint64_t copies, uint64_t batches,
                             uint64_t instances) const {
    return impl_->complete(session, result, copies, batches, instances);
}

cybercanvas::ResourceAccount OutputBinding::account() {
    return impl_->account();
}

OutputBinding::OutputBinding() : impl_(std::make_unique<Impl>()) {}

OutputBinding::~OutputBinding() = default;

class FrameCache::Impl {
public:
    void bind(const CdCreateContext& context) {
        output_.bind(context);
    }

    template <typename Paint>
    CdResult render(const CdFrame& frame, uint64_t content, Paint&& paint) {
        const auto session = output_.session(frame);
        const auto key =
            std::make_tuple(session.output_epoch, frame.width, frame.height, frame.device_pixel_ratio, content);
        if (session.backend == CD_OUTPUT_GPU) {
            if (!pixels_.empty()) {
                std::vector<uint8_t>().swap(pixels_);
                lease_.Reset();
                cpuValid_ = false;
            }
            if (gpuValid_ && gpuKey_ == key) {
                output_.complete(session, CD_OUTPUT_UNCHANGED);
                return CD_OK;
            }
            const auto status = paint(frame);
            gpuValid_ = status == CD_OK;
            if (gpuValid_) {
                gpuKey_ = key;
            }
            return status;
        }
        if (!cpuValid_ || cpuKey_ != key) {
            cpuValid_ = false;
            std::vector<uint8_t>{}.swap(pixels_);
            lease_.Reset();
            lease_ = output_.account().acquire(uint64_t(frame.width) * frame.height * 4);
            std::vector<uint8_t>(size_t(frame.width) * frame.height * 4, 0).swap(pixels_);
            auto target = frame;
            target.stride = frame.width * 4;
            target.capacity = pixels_.size();
            target.rgba = pixels_.data();
            const auto status = paint(target);
            if (status != CD_OK) {
                cpuValid_ = false;
                return status;
            }
            cpuKey_ = key;
            cpuValid_ = true;
        }
        for (uint32_t y = 0; y < frame.height; ++y) {
            std::memcpy(frame.rgba + size_t(y) * frame.stride, pixels_.data() + size_t(y) * frame.width * 4,
                        size_t(frame.width) * 4);
        }
        output_.complete(session, CD_OUTPUT_PAINTED, uint64_t(frame.width) * frame.height * 4);
        return CD_OK;
    }

    void invalidate() {
        cpuValid_ = gpuValid_ = false;
    }

private:
    using Key = std::tuple<uint64_t, uint32_t, uint32_t, double, uint64_t>;
    OutputBinding output_;
    Key cpuKey_{}, gpuKey_{};
    bool cpuValid_ = false, gpuValid_ = false;
    std::vector<uint8_t> pixels_;
    cybercanvas::ComPtr<IUnknown> lease_;
};

void FrameCache::bind(const CdCreateContext& context) {
    return impl_->bind(context);
}

CdResult FrameCache::renderImpl(const CdFrame& frame, uint64_t content, void* callbackContext,
                                CdResult (*paint)(void*, const CdFrame&)) {
    return impl_->render(frame, content, [&](const CdFrame& frame) -> CdResult {
        return paint(callbackContext, frame);
    });
}

void FrameCache::invalidate() {
    return impl_->invalidate();
}

FrameCache::FrameCache() : impl_(std::make_unique<Impl>()) {}

FrameCache::~FrameCache() = default;
}  // namespace cdsdk
