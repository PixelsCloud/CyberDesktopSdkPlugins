#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "LiquidGlass/Capture.h"

struct ID3D11Device;
struct ID3D11Texture2D;
struct ID3D11ShaderResourceView;
struct ID2D1DeviceContext;
struct ID2D1Bitmap1;

namespace liquidglass::detail {
class WindowStack final {
public:
    WindowStack(void* nativeDevice, Region bounds, std::function<void()> wake);
    ~WindowStack();
    void configure(const std::vector<Region>& regions, const std::vector<uintptr_t>& exclusions);
    bool take(ID2D1DeviceContext& context, ID3D11Device& device, bool consumeFrame = true,
              uint64_t maximumBytes = 256ull * 1024 * 1024);
    uint64_t bytes() const;
    std::string error() const;
    const char* backend() const;
    bool usesPublishedFrames() const;
    ID2D1Bitmap1* bitmap() const;
    ID3D11Texture2D* texture() const;
    ID3D11ShaderResourceView* view() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}  // namespace liquidglass::detail
