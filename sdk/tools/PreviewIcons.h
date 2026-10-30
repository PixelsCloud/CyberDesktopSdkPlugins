#pragma once
#include <memory>

#include "sdk/PluginApi.h"

namespace cdsdk {
class PreviewIcons {
public:
    PreviewIcons();
    ~PreviewIcons();
    CdResult raster(const char* name, uint32_t color, const CdFrame* output);

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
}  // namespace cdsdk
