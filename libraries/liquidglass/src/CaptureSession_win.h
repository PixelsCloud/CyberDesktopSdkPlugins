#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>

struct ID3D11Device;
struct ID3D11Texture2D;
struct ID3D11ShaderResourceView;
struct ID2D1DeviceContext;
struct ID2D1Bitmap1;

#include "LiquidGlass/Capture.h"

namespace liquidglass::detail {
using SessionCapabilities = CaptureCapabilities;

class CaptureSizeChanged final : public std::runtime_error {
public:
    CaptureSizeChanged();
};

// Worker-owned session. Callbacks only replace a single pending frame and wake
// the worker; they never submit GPU work or retain an unbounded frame queue.
class CaptureSession final {
public:
    CaptureSession(void* nativeDevice, uintptr_t target, bool monitor, std::function<void()> wake,
                   bool capabilityProbe = false, uint64_t maximumPoolBytes = 256ull * 1024 * 1024);
    ~CaptureSession();
    void start(const std::vector<uintptr_t>& exclusions = {});
    bool take(ID2D1DeviceContext& context, ID3D11Device& device);
    SessionCapabilities capabilities() const;
    Region size() const;
    uint64_t bytes() const;
    const std::vector<Region>& changes() const;
    ID2D1Bitmap1* bitmap() const;
    ID3D11Texture2D* texture() const;
    ID3D11ShaderResourceView* view() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}  // namespace liquidglass::detail
