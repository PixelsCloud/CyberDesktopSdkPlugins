#include "CyberCanvas/Diagnostics.h"

namespace cybercanvas {
uint64_t nanosecondsSince(MonotonicTime start) {
    return uint64_t(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count());
}

ScopedDuration::ScopedDuration(uint64_t& destination)
    : destination_(destination), start_(std::chrono::steady_clock::now()) {}

ScopedDuration::~ScopedDuration() {
    destination_ += nanosecondsSince(start_);
}
}  // namespace cybercanvas
