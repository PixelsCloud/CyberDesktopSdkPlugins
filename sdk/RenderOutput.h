#pragma once
#include <CyberCanvas/ResourceAccount.h>

#include <memory>
#include <utility>

#include "services/RenderOutput.h"

namespace cdsdk {
template <typename Renderer>
void bindOutputIfSupported(Renderer& renderer, const CdCreateContext& context) {
    if constexpr (requires { renderer.bindOutput(context); }) {
        renderer.bindOutput(context);
    }
}

// The binding is per main producer. Nested frames never inherit its target.
class OutputBinding {
public:
    OutputBinding();

    ~OutputBinding();

    void bind(const CdCreateContext& context);

    CdRenderSession session(const CdFrame& frame) const;

    void complete(const CdRenderSession& session, uint32_t result, uint64_t copies = 0, uint64_t batches = 0,
                  uint64_t instances = 0) const;

    cybercanvas::ResourceAccount account();

    OutputBinding(const OutputBinding&) = delete;

    OutputBinding& operator=(const OutputBinding&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Cached CPU exports and retained GPU output use independent keys. A GPU draw
// neither allocates an RGBA cache nor copies compatibility scratch bytes.
class FrameCache {
public:
    FrameCache();

    ~FrameCache();

    void bind(const CdCreateContext& context);

    template <typename Paint>
    CdResult render(const CdFrame& frame, uint64_t content, Paint&& paint) {
        auto callback = [&](const CdFrame& frame) -> CdResult {
            return std::forward<Paint>(paint)(frame);
        };
        return renderImpl(frame, content, &callback, [](void* callbackContext, const CdFrame& frame) -> CdResult {
            return (*static_cast<decltype(callback)*>(callbackContext))(frame);
        });
    }

    void invalidate();

    FrameCache(const FrameCache&) = delete;

    FrameCache& operator=(const FrameCache&) = delete;

private:
    CdResult renderImpl(const CdFrame& frame, uint64_t content, void* callbackContext,
                        CdResult (*paint)(void*, const CdFrame&));
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace cdsdk
