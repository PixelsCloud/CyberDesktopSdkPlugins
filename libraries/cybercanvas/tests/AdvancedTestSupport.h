#pragma once

#include <CyberCanvas/Canvas.h>

#include <array>
#include <functional>
#include <vector>

namespace canvas_testing {
using Check = std::function<void(const char*, bool)>;

struct Buffer {
    explicit Buffer(double dpi = 1);
    std::array<uint8_t, 4> pixel(int x, int y) const;
    std::vector<uint8_t> pixels;
    cybercanvas::FrameBuffer frame;
};

template <typename Action>
bool rejected(Action&& action) {
    try {
        action();
        return false;
    } catch (const std::exception&) {
        return true;
    }
}
}  // namespace canvas_testing
