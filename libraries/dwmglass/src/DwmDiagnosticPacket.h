// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstdint>

namespace cyberglass::dwm {
struct DiagnosticSurface {
    uint64_t window = 0;
    uint32_t process = 0, material = 0;
    int32_t width = 0, height = 0;
};

struct DiagnosticPacket {
    uint32_t magic = 0x43474431, version = 1, size = sizeof(DiagnosticPacket), enabled = 0;
    std::array<uint64_t, 17> values{};
    std::array<char, 192> error{};
    uint64_t residentLiquidMaterials = 0;
    uint32_t surfaceCount = 0;
    std::array<DiagnosticSurface, 128> surfaces{};
};
}  // namespace cyberglass::dwm
