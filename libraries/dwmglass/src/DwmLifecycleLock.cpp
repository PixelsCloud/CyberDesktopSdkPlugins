// SPDX-License-Identifier: GPL-3.0-only
#include "libraries/dwmglass/src/DwmLifecycleLock.h"

#include <Windows.h>

namespace cyberglass::dwm {
CompositorAccess::CompositorAccess(void* criticalSection) noexcept {
    if (criticalSection && TryEnterCriticalSection(static_cast<CRITICAL_SECTION*>(criticalSection))) {
        section_ = criticalSection;
    }
}

CompositorAccess::~CompositorAccess() {
    if (section_) {
        LeaveCriticalSection(static_cast<CRITICAL_SECTION*>(section_));
    }
}

CompositorAccess::operator bool() const noexcept {
    return section_ != nullptr;
}
}  // namespace cyberglass::dwm
