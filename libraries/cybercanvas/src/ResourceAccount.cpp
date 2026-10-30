#include <CyberCanvas/ResourceAccount.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/implements.h>

#include <new>
#include <utility>

namespace cybercanvas {
class MemoryLeaseUnknown final
    : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IUnknown> {
public:
    explicit MemoryLeaseUnknown(std::shared_ptr<void> owner) : owner_(std::move(owner)) {}

private:
    std::shared_ptr<void> owner_;
};

ComPtr<IUnknown> makeMemoryLease(std::shared_ptr<void> owner) {
    auto lease = Microsoft::WRL::Make<MemoryLeaseUnknown>(std::move(owner));
    if (!lease) {
        throw std::bad_alloc();
    }
    return lease;
}

ComPtr<IUnknown> ResourceAccount::acquire(uint64_t bytes) const {
    ComPtr<IUnknown> lease;
    if (reserve) {
        lease.Attach(static_cast<IUnknown*>(reserve(context, bytes)));
        if (!lease) {
            throw std::length_error("Rendering resource budget exhausted");
        }
    }
    return lease;
}

void retainTextureLease(ID2D1Bitmap* bitmap, IUnknown* lease) {
    if (!bitmap || !lease) {
        return;
    }
    ComPtr<ID2D1Bitmap1> gpuBitmap;
    ComPtr<IDXGISurface> surface;
    ComPtr<ID3D11Texture2D> texture;
    if (SUCCEEDED(bitmap->QueryInterface(IID_PPV_ARGS(&gpuBitmap))) && SUCCEEDED(gpuBitmap->GetSurface(&surface)) &&
        SUCCEEDED(surface.As(&texture))) {
        // Keeps accounting alive when a retained bitmap outlives its surface.
        static constexpr GUID key{0x28cf4ae1, 0x759b, 0x47d1, {0xa1, 0x97, 0x35, 0x73, 0x8c, 0x30, 0x55, 0x62}};
        require(texture->SetPrivateDataInterface(key, lease));
    }
}
}  // namespace cybercanvas
