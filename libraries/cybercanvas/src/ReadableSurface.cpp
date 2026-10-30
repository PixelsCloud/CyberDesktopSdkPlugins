#include "CyberCanvas/ReadableSurface.h"

#include <cmath>
#include <stdexcept>
#include <vector>

#include "CyberCanvas/Image.h"
#include "CyberCanvas/Layout.h"

namespace cybercanvas {
struct ReadableSurface::Impl {
    ComPtr<ID2D1Factory> factory_;
    ComPtr<IWICImagingFactory> wic_;
    ComPtr<IWICBitmap> pixels_;
    ComPtr<ID2D1RenderTarget> target_, uploadOwner_;
    SurfaceImage uploaded_;
    ComPtr<IUnknown> pixelsLease_;
    ResourceAccount account_;
    std::shared_ptr<const ImageData> image_;
    SurfaceSize size_;
    size_t clipDepth_ = 0;
    bool drawing_ = false;
};

ReadableSurface::ReadableSurface() : impl_(std::make_unique<Impl>()) {}

ReadableSurface::~ReadableSurface() = default;

auto ReadableSurface::image() const -> std::shared_ptr<const ImageData> {
    return impl_->image_;
}

auto ReadableSurface::bitmap(ID2D1RenderTarget* destination) -> SurfaceImage {
    if (impl_->drawing_ || !impl_->image_ || !destination) {
        throw std::logic_error("Readable surface has no completed image or destination");
    }
    if (impl_->uploadOwner_.Get() != destination) {
        ComPtr<ID2D1Bitmap> bitmap;
        auto lease = impl_->account_.acquire(uint64_t(impl_->image_->width()) * impl_->image_->height() * 4);
        require(destination->CreateBitmap(
            D2D1::SizeU(impl_->image_->width(), impl_->image_->height()), impl_->image_->pixels().data(),
            impl_->image_->stride(),
            D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                                   float(96 * impl_->size_.devicePixelRatio),
                                   float(96 * impl_->size_.devicePixelRatio)),
            bitmap.GetAddressOf()));
        retainTextureLease(bitmap.Get(), lease.Get());
        impl_->uploaded_ = SurfaceImage(bitmap.Get(), lease.Get());
        impl_->uploadOwner_ = destination;
    }
    return impl_->uploaded_;
}

auto ReadableSurface::reset() -> void {
    if (impl_->drawing_) {
        throw std::logic_error("Cannot reset an active readable surface");
    }
    impl_->image_.reset();
    impl_->uploaded_.Reset();
    impl_->uploadOwner_.Reset();
    impl_->target_.Reset();
    impl_->pixels_.Reset();
    impl_->pixelsLease_.Reset();
    impl_->factory_.Reset();
    impl_->wic_.Reset();
    impl_->size_ = {};
}

void ReadableSurface::renderImpl(ID2D1Factory* factory, IWICImagingFactory* wic, SurfaceSize size, void* context,
                                 void (*paint)(void*, ID2D1RenderTarget*), ResourceAccount account) {
    if (impl_->drawing_) {
        throw std::logic_error("Readable surface is already drawing");
    }
    if (!factory || !wic || !size.width || !size.height || size.width > 4096 || size.height > 4096 ||
        !std::isfinite(size.devicePixelRatio) || size.devicePixelRatio < .5 || size.devicePixelRatio > 4) {
        throw std::invalid_argument("Invalid readable surface size");
    }
    if (impl_->factory_.Get() != factory || impl_->wic_.Get() != wic || impl_->size_.width != size.width ||
        impl_->size_.height != size.height || impl_->size_.devicePixelRatio != size.devicePixelRatio) {
        reset();
        impl_->account_ = account;
        auto lease = account.acquire(uint64_t(size.width) * size.height * 4);
        ComPtr<IWICBitmap> pixels;
        ComPtr<ID2D1RenderTarget> target;
        require(wic->CreateBitmap(size.width, size.height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad,
                                  pixels.GetAddressOf()));
        require(factory->CreateWicBitmapRenderTarget(
            pixels.Get(),
            D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                                         D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                                         float(96 * size.devicePixelRatio), float(96 * size.devicePixelRatio)),
            target.GetAddressOf()));
        impl_->factory_ = factory;
        impl_->wic_ = wic;
        impl_->pixels_ = std::move(pixels);
        impl_->pixelsLease_ = std::move(lease);
        impl_->target_ = std::move(target);
        impl_->size_ = size;
    }
    impl_->image_.reset();
    impl_->uploaded_.Reset();
    impl_->uploadOwner_.Reset();
    impl_->target_->SetDpi(float(96 * size.devicePixelRatio), float(96 * size.devicePixelRatio));
    impl_->target_->BeginDraw();
    impl_->drawing_ = true;
    impl_->target_->SetTransform(D2D1::Matrix3x2F::Identity());
    impl_->target_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    impl_->target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    impl_->target_->Clear(D2D1::ColorF(0, 0, 0, 0));
    try {
        paint(context, impl_->target_.Get());
        impl_->drawing_ = false;
        require(impl_->target_->EndDraw());
        auto lease = impl_->account_.acquire(uint64_t(size.width) * size.height * 4);
        std::vector<uint8_t> bytes(size_t(size.width) * size.height * 4);
        require(impl_->pixels_->CopyPixels(nullptr, size.width * 4, UINT(bytes.size()), bytes.data()));
        auto image = ImageData::fromPremultipliedBgra(size.width, size.height, std::move(bytes));
        const auto* pointer = image.get();

        struct Snapshot {
            std::shared_ptr<const ImageData> image;
            ComPtr<IUnknown> lease;
        };

        auto snapshot = std::make_shared<Snapshot>(Snapshot{std::move(image), std::move(lease)});
        impl_->image_ = std::shared_ptr<const ImageData>(std::move(snapshot), pointer);
    } catch (...) {
        if (impl_->drawing_) {
            impl_->drawing_ = false;
            impl_->target_->EndDraw();
        }
        reset();
        throw;
    }
}

void ReadableSurface::clippedImpl(Rect area, void* context, void (*paint)(void*)) {
    if (!impl_->drawing_) {
        throw std::logic_error("Readable clip needs an active render");
    }
    detail::layoutRect(area);
    if (impl_->clipDepth_ >= 32) {
        throw std::length_error("Readable clip nesting limit reached");
    }
    impl_->target_->PushAxisAlignedClip(
        D2D1::RectF(float(area.x), float(area.y), float(area.x + area.width), float(area.y + area.height)),
        D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    ++impl_->clipDepth_;

    struct Scope {
        ReadableSurface& surface;

        ~Scope() {
            surface.impl_->target_->PopAxisAlignedClip();
            --surface.impl_->clipDepth_;
        }
    } scope{*this};

    paint(context);
}
}  // namespace cybercanvas
