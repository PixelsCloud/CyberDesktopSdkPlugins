#pragma once

#include <cstdint>
#include <vector>

namespace liquidglass {
// Generated once per shape/DPI: a small Gaussian mask, never desktop pixels.
struct ShadowImage {
    int width = 0, height = 0, bodyWidth = 0, bodyHeight = 0;
    int padding = 0, capX = 0, capY = 0;
    float dpi = 1, offsetY = 12, opacity = .25f;
    std::vector<uint8_t> bgra;
};
}  // namespace liquidglass
