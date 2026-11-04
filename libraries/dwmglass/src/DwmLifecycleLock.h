// SPDX-License-Identifier: GPL-3.0-only
#pragma once

namespace cyberglass::dwm {
// Acquire the compositor lock BEFORE constructing any hook transaction. A
// rendering callback can hold this lock while entering a lazy transaction.
class CompositorAccess final {
public:
    explicit CompositorAccess(void* criticalSection) noexcept;
    ~CompositorAccess();
    CompositorAccess(const CompositorAccess&) = delete;
    CompositorAccess& operator=(const CompositorAccess&) = delete;
    explicit operator bool() const noexcept;

private:
    void* section_ = nullptr;
};
}  // namespace cyberglass::dwm
