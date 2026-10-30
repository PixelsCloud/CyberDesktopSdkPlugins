#pragma once
#include <memory>

#include "CyberCanvas/Native.h"

namespace cybercanvas {
ComPtr<IUnknown> makeMemoryLease(std::shared_ptr<void> owner);

// The allocator returns an AddRef'd COM lease; only COM ownership crosses DLLs.
struct ResourceAccount {
    void* context = nullptr;
    void* (*reserve)(void*, uint64_t) = nullptr;
    ComPtr<IUnknown> acquire(uint64_t bytes) const;
};

void retainTextureLease(ID2D1Bitmap* bitmap, IUnknown* lease);
}  // namespace cybercanvas
