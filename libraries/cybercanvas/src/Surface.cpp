#include <CyberCanvas/Native.h>
#include <CyberCanvas/ResourceAccount.h>
#include <CyberCanvas/Surface.h>
#include <CyberCanvas/Types.h>

#include <cmath>
#include <utility>
#include <vector>

namespace cybercanvas {
class GeometryMasks::Impl {
public:
    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    template <typename Paint>
    void with(ID2D1RenderTarget* target, ID2D1Geometry* geometry, Paint&& paint,
              D2D1_RECT_F bounds = D2D1::InfiniteRect()) {
        if (!target || !geometry || !std::isfinite(bounds.left) || !std::isfinite(bounds.top) ||
            !std::isfinite(bounds.right) || !std::isfinite(bounds.bottom) || bounds.left > bounds.right ||
            bounds.top > bounds.bottom) {
            throw std::invalid_argument("Invalid geometry mask");
        }
        ComPtr<ID2D1Factory> targetFactory, geometryFactory;
        target->GetFactory(targetFactory.GetAddressOf());
        geometry->GetFactory(geometryFactory.GetAddressOf());
        if (targetFactory != geometryFactory) {
            throw std::invalid_argument("Geometry mask belongs to another factory");
        }
        if (target_.Get() != target) {
            reset();
            target_ = target;
        }
        if (depth_ >= 32) {
            throw std::length_error("Geometry mask nesting limit reached");
        }
        if (layers_.size() == depth_) {
            ComPtr<ID2D1Layer> layer;
            require(target->CreateLayer(nullptr, layer.GetAddressOf()));
            layers_.push_back(std::move(layer));
        }
        target->PushLayer(D2D1::LayerParameters(bounds, geometry), layers_[depth_++].Get());

        struct Scope {
            Impl& masks;

            ~Scope() {
                masks.target_->PopLayer();
                --masks.depth_;
            }
        } scope{*this};

        std::forward<Paint>(paint)();
    }

    void reset() {
        if (depth_) {
            throw std::logic_error("Cannot reset active geometry masks");
        }
        layers_.clear();
        target_.Reset();
    }

private:
    ComPtr<ID2D1RenderTarget> target_;
    std::vector<ComPtr<ID2D1Layer>> layers_;
    size_t depth_ = 0;
};

GeometryMasks::GeometryMasks() : impl_(std::make_unique<Impl>()) {}

void GeometryMasks::withImpl(ID2D1RenderTarget* target, ID2D1Geometry* geometry, void* callbackContext,
                             void (*paint)(void*), D2D1_RECT_F bounds) {
    return impl_->with(
        target, geometry,
        [&]() -> void {
            return paint(callbackContext);
        },
        bounds);
}

void GeometryMasks::reset() {
    return impl_->reset();
}

GeometryMasks::~GeometryMasks() = default;

class OffscreenSurface::Impl {
public:
    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    template <typename Paint>
    void render(ID2D1RenderTarget* owner, SurfaceSize size, Paint&& paint, ResourceAccount account = {}) {
        if (drawing_) {
            throw std::logic_error("Offscreen surface is already drawing");
        }
        if (!owner || !size.width || !size.height || size.width > 4096 || size.height > 4096 ||
            !std::isfinite(size.devicePixelRatio) || size.devicePixelRatio < .5 || size.devicePixelRatio > 4) {
            throw std::invalid_argument("Invalid offscreen surface size");
        }
        if (owner_.Get() != owner || size_.width != size.width || size_.height != size.height ||
            size_.devicePixelRatio != size.devicePixelRatio) {
            reset();
            lease_ = account.acquire(uint64_t(size.width) * size.height * 4);
            require(owner->CreateCompatibleRenderTarget(
                D2D1::SizeF(float(size.width / size.devicePixelRatio), float(size.height / size.devicePixelRatio)),
                D2D1::SizeU(size.width, size.height),
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), target_.GetAddressOf()));
            owner_ = owner;
            size_ = size;
        }
        bitmap_.Reset();
        target_->BeginDraw();
        drawing_ = true;
        target_->SetTransform(D2D1::Matrix3x2F::Identity());
        target_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        target_->Clear(D2D1::ColorF(0, 0, 0, 0));
        try {
            std::forward<Paint>(paint)(target_.Get());
            drawing_ = false;
            require(target_->EndDraw());
            require(target_->GetBitmap(bitmap_.GetAddressOf()));
            retainTextureLease(bitmap_.Get(), lease_.Get());
        } catch (...) {
            if (drawing_) {
                drawing_ = false;
                target_->EndDraw();
            }
            reset();
            throw;
        }
    }

    template <typename Paint>
    void masked(ID2D1Geometry* geometry, Paint&& paint, D2D1_RECT_F bounds = D2D1::InfiniteRect()) {
        if (!drawing_) {
            throw std::logic_error("Offscreen mask needs an active render");
        }
        masks_.with(target_.Get(), geometry, std::forward<Paint>(paint), bounds);
    }

    ID2D1Bitmap* bitmap() const {
        return bitmap_.Get();
    }

    bool matches(ID2D1RenderTarget* owner, SurfaceSize size) const {
        return bitmap_ && owner_.Get() == owner && size_.width == size.width && size_.height == size.height &&
               size_.devicePixelRatio == size.devicePixelRatio;
    }

    SurfaceImage snapshot() const {
        return {bitmap_.Get(), lease_.Get()};
    }

    Size logicalSize() const {
        return {size_.width / size_.devicePixelRatio, size_.height / size_.devicePixelRatio};
    }

    // Callers own semantic invalidation (text/theme/scene changes).
    void invalidate() {
        if (drawing_) {
            throw std::logic_error("Cannot invalidate an active offscreen surface");
        }
        bitmap_.Reset();
    }

    void reset() {
        if (drawing_) {
            throw std::logic_error("Cannot reset an active offscreen surface");
        }
        masks_.reset();
        bitmap_.Reset();
        target_.Reset();
        lease_.Reset();
        owner_.Reset();
        size_ = {};
    }

private:
    ComPtr<ID2D1RenderTarget> owner_;
    ComPtr<IUnknown> lease_;
    ComPtr<ID2D1BitmapRenderTarget> target_;
    ComPtr<ID2D1Bitmap> bitmap_;
    GeometryMasks masks_;
    SurfaceSize size_;
    bool drawing_ = false;
};

OffscreenSurface::OffscreenSurface() : impl_(std::make_unique<Impl>()) {}

void OffscreenSurface::renderImpl(ID2D1RenderTarget* owner, SurfaceSize size, void* callbackContext,
                                  void (*paint)(void*, ID2D1BitmapRenderTarget*), ResourceAccount account) {
    return impl_->render(
        owner, size,
        [&](ID2D1BitmapRenderTarget* target) -> void {
            return paint(callbackContext, target);
        },
        account);
}

void OffscreenSurface::maskedImpl(ID2D1Geometry* geometry, void* callbackContext, void (*paint)(void*),
                                  D2D1_RECT_F bounds) {
    return impl_->masked(
        geometry,
        [&]() -> void {
            return paint(callbackContext);
        },
        bounds);
}

ID2D1Bitmap* OffscreenSurface::bitmap() const {
    return impl_->bitmap();
}

bool OffscreenSurface::matches(ID2D1RenderTarget* owner, SurfaceSize size) const {
    return impl_->matches(owner, size);
}

SurfaceImage OffscreenSurface::snapshot() const {
    return impl_->snapshot();
}

Size OffscreenSurface::logicalSize() const {
    return impl_->logicalSize();
}

void OffscreenSurface::invalidate() {
    return impl_->invalidate();
}

void OffscreenSurface::reset() {
    return impl_->reset();
}

OffscreenSurface::~OffscreenSurface() = default;
}  // namespace cybercanvas

namespace cybercanvas {
SurfaceImage::SurfaceImage(ID2D1Bitmap* bitmap) : Base(bitmap) {}

SurfaceImage::SurfaceImage(ID2D1Bitmap* bitmap, IUnknown* lease) : Base(bitmap), lease_(lease) {}

auto SurfaceImage::operator=(ID2D1Bitmap* bitmap) -> SurfaceImage& {
    Base::operator=(bitmap);
    lease_.Reset();
    return *this;
}

auto SurfaceImage::Reset() -> ULONG {
    const auto remaining = Base::Reset();
    lease_.Reset();
    return remaining;
}
}  // namespace cybercanvas
