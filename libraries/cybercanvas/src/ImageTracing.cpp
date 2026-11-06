#include "CyberCanvas/ImageTracing.h"

#include <d2d1_1.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>

namespace cybercanvas {
namespace {
using Microsoft::WRL::ComPtr;
std::atomic<ImageTraceObserver> observer{nullptr};
constexpr GUID kTextureTrace = {0x18f133c5, 0x7824, 0x4d0d, {0xae, 0x5d, 0xa1, 0x70, 0x2b, 0xf4, 0xf9, 0x31}};

class Lease final : public ImageTraceLease {
public:
    explicit Lease(ComPtr<IUnknown> value) : value_(std::move(value)) {}

private:
    ComPtr<IUnknown> value_;
};
}  // namespace

ImageTraceLease::~ImageTraceLease() = default;

void configureImageTracing(ImageTraceObserver value) noexcept {
    observer.store(value, std::memory_order_release);
}

bool imageTracingEnabled() noexcept {
    return observer.load(std::memory_order_acquire) != nullptr;
}

std::unique_ptr<ImageTraceLease> traceImage(const void* pixels, uint64_t bytes, uint32_t width, uint32_t height,
                                            uint32_t stride, const char* source,
                                            std::source_location location) noexcept {
    try {
        const auto callback = observer.load(std::memory_order_acquire);
        if (!callback || !pixels || !bytes) {
            return {};
        }
        const ImageTraceDescription description{
            0, pixels, bytes, width, height, stride, 0, source, location.file_name(), location.line()};
        ComPtr<IUnknown> lease;
        lease.Attach(static_cast<IUnknown*>(callback(description)));
        return lease ? std::make_unique<Lease>(std::move(lease)) : nullptr;
    } catch (...) {
        return {};
    }
}

void traceImage(ID3D11Texture2D* texture, const char* source, std::source_location location) noexcept {
    const auto callback = observer.load(std::memory_order_acquire);
    if (!callback || !texture) {
        return;
    }
    D3D11_TEXTURE2D_DESC value{};
    texture->GetDesc(&value);
    unsigned pixelBytes = 0;
    switch (value.Format) {
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_R32_FLOAT:
        case DXGI_FORMAT_R32_TYPELESS:
        case DXGI_FORMAT_R32_UINT:
        case DXGI_FORMAT_R16G16_UNORM:
        case DXGI_FORMAT_D32_FLOAT:
            pixelBytes = 4;
            break;
        case DXGI_FORMAT_R8_UNORM:
            pixelBytes = 1;
            break;
        case DXGI_FORMAT_R16_FLOAT:
        case DXGI_FORMAT_R16_TYPELESS:
        case DXGI_FORMAT_D16_UNORM:
        case DXGI_FORMAT_R16_UNORM:
        case DXGI_FORMAT_R8G8_UNORM:
            pixelBytes = 2;
            break;
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
        case DXGI_FORMAT_R16G16B16A16_UNORM:
            pixelBytes = 8;
            break;
        case DXGI_FORMAT_R32G32B32A32_FLOAT:
            pixelBytes = 16;
            break;
        default:
            break;
    }
    uint64_t bytes = 0;
    auto width = value.Width, height = value.Height;
    for (UINT mip = 0; mip < value.MipLevels; ++mip) {
        if (value.Format == DXGI_FORMAT_NV12 || value.Format == DXGI_FORMAT_P010 || value.Format == DXGI_FORMAT_P016) {
            bytes +=
                uint64_t((width + 1) & ~1u) * (height + (height + 1) / 2) * (value.Format == DXGI_FORMAT_NV12 ? 1 : 2);
        } else {
            bytes += uint64_t(width) * height * pixelBytes;
        }
        width = std::max(1u, width / 2);
        height = std::max(1u, height / 2);
    }
    bytes *= uint64_t(value.ArraySize) * value.SampleDesc.Count;
    const ImageTraceDescription description{1,
                                            texture,
                                            bytes,
                                            value.Width,
                                            value.Height,
                                            value.Width * pixelBytes,
                                            uint32_t(value.Format),
                                            source,
                                            location.file_name(),
                                            location.line()};
    ComPtr<IUnknown> lease;
    lease.Attach(static_cast<IUnknown*>(callback(description)));
    if (lease) {
        texture->SetPrivateDataInterface(kTextureTrace, lease.Get());
    }
}

void traceImage(ID2D1Bitmap* bitmap, const char* source, std::source_location location) noexcept {
    if (!imageTracingEnabled() || !bitmap) {
        return;
    }
    ComPtr<ID2D1Bitmap1> bitmap1;
    ComPtr<IDXGISurface> surface;
    ComPtr<ID3D11Texture2D> texture;
    if (SUCCEEDED(bitmap->QueryInterface(IID_PPV_ARGS(&bitmap1))) && SUCCEEDED(bitmap1->GetSurface(&surface)) &&
        SUCCEEDED(surface->QueryInterface(IID_PPV_ARGS(&texture)))) {
        traceImage(texture.Get(), source, location);
    }
}
}  // namespace cybercanvas
