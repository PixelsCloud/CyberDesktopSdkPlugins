#pragma once

#include <cstdint>

#include "sdk/services/RenderOutput.h"

namespace cdsdk {
class PreviewOutput {
public:
    CdResult configure();
    void begin(const CdFrame& frame);
    void end();
    CdResult session(const CdFrame* frame, CdRenderSession* output) const;
    CdResult complete(const CdRenderReport* report);

private:
    CdFrame frame_{};
    uintptr_t allocation_ = 0;
    uint32_t width_ = 0, height_ = 0;
    double dpr_ = 0;
    uint64_t epoch_ = 0, content_ = 0, layout_ = 0;
    bool configured_ = false, active_ = false, completed_ = false;
};
}  // namespace cdsdk
