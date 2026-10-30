#pragma once

#include <sdk/Images.h>

#include <algorithm>
#include <cmath>

namespace pixeldesk {
// Preserve the artwork's original colors and alpha. Decode to the requested
// physical size without palette replacement or an artificial coarse grid.
bool requestPixelImage(cdsdk::Images& images, const std::string& slot, const std::filesystem::path& path,
                       uint64_t revision, uint32_t actualEdge, double dpi = 1);
}  // namespace pixeldesk
