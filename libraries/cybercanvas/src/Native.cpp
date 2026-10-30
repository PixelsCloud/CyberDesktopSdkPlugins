#include "CyberCanvas/Native.h"

namespace cybercanvas {
void require(HRESULT result) {
    if (FAILED(result)) {
        throw std::runtime_error("Canvas rendering failed");
    }
}

D2D1_COLOR_F color(uint32_t rgba) {
    return D2D1::ColorF(float(rgba >> 24) / 255, float((rgba >> 16) & 255) / 255, float((rgba >> 8) & 255) / 255,
                        float(rgba & 255) / 255);
}
}  // namespace cybercanvas
