#pragma once

#include <cstdint>
#include <memory>
#include <span>

namespace liquidglass {
class FilteredCaptureSession;

struct CaptureRectangle {
    int x = 0, y = 0, width = 0, height = 0;
};

// Thread-confined, local Magnification capture. Exclusions belong to this capture
// control; no display-affinity restriction is placed on the supplied windows.
// The platform callback supplies a local desktop DIB block. One bounded bulk copy and
// one texture upload are used; background optics never run on the CPU.
class FilteredRegionCapture final {
public:
    // Initialize on the capture thread before its controls and message loop.
    // Keep the returned session alive until all captures have been destroyed.
    static std::shared_ptr<FilteredCaptureSession> initializeSession();
    explicit FilteredRegionCapture(void* d3dDevice);
    ~FilteredRegionCapture();
    void capture(CaptureRectangle rectangle, std::span<const uintptr_t> excludedWindows);
    // Borrowed D3D11 BGRA texture, valid until the next capture or destruction.
    void* texture() const;
    uint64_t allocatedBytes() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}  // namespace liquidglass
