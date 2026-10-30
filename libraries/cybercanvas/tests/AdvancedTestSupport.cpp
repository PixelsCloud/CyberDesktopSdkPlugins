#include "tests/AdvancedTestSupport.h"

namespace canvas_testing {
Buffer::Buffer(double dpi)
    : pixels(size_t(96 * dpi) * size_t(80 * dpi) * 4),
      frame{uint32_t(96 * dpi), uint32_t(80 * dpi), uint32_t(96 * dpi) * 4, pixels.size(), pixels.data(), dpi} {}

std::array<uint8_t, 4> Buffer::pixel(int x, int y) const {
    const auto* p = pixels.data() + size_t(y) * frame.stride + x * 4;
    return {p[0], p[1], p[2], p[3]};
}
}  // namespace canvas_testing
