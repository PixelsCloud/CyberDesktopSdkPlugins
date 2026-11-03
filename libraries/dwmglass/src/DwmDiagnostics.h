// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>

namespace cyberglass::dwm {
enum class Diagnostic : uint32_t {
    ClientBlur,
    Property,
    MappingFailure,
    Marker,
    Brush,
    Render,
    Error,
    Command,
    Solid,
    RawAlpha,
    Count
};
void sample(Diagnostic counter, uint64_t value = 0);
void recordFailure(const char* text);
}  // namespace cyberglass::dwm
