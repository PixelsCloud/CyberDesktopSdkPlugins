#pragma once

#include <filesystem>
#include <functional>
#include <memory>

#include "sdk/services/Live2D.h"

namespace cdsdk {
class PreviewLive2D {
public:
    explicit PreviewLive2D(std::function<void()> redraw);
    ~PreviewLive2D();
    PreviewLive2D(const PreviewLive2D&) = delete;
    PreviewLive2D& operator=(const PreviewLive2D&) = delete;

    void root(const std::filesystem::path& directory);
    CdResult configure(const CdLive2DOptions* options);
    CdResult snapshot(CdLive2DStatus* status) const;
    CdResult perform(const char* motion, uint32_t priority);
    void pointer(double x, double y, uint32_t tap);
    void pause(bool paused);
    void tick();
    bool render(const CdFrame& frame);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace cdsdk
