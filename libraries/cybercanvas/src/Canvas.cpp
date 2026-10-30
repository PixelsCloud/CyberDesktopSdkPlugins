#include <CyberCanvas/Canvas.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <tuple>
#include <vector>
#if defined(_M_X64) || defined(__x86_64__)
#include <emmintrin.h>
#endif
namespace cybercanvas {
struct Canvas::Impl {
    ComPtr<IDWriteFontCollection> fontCollection_;
    CanvasStats stats_;
    MonotonicTime frameStarted_{};
    uint64_t transientBytes_ = 0;

    struct Upload {
        SurfaceImage bitmap;
        uint64_t bytes = 0, age = 0;
    };

    std::map<uint64_t, Upload> uploads_;
    uint64_t uploadedBytes_ = 0, uploadClock_ = 0;
    static constexpr uint64_t uploadBudget_ = 64 * 1024 * 1024;
    bool com_ = false;
    bool external_ = false;
    ResourceAccount account_;
    bool hostDrawScope_ = false;
    ComPtr<ID2D1Factory> cpuFactory_;
    ComPtr<ID2D1SpriteBatch> spriteBatch_;
    bool drawing_ = false;
    std::vector<bool> scopes_;
    size_t stateDepth_ = 0, layerDepth_ = 0;
    uint32_t width_ = 0, height_ = 0;
    double dpi_ = 0;
    ComPtr<IWICImagingFactory> wic_;
    ComPtr<IWICBitmap> bitmap_;
    ComPtr<IUnknown> bitmapLease_;
    ComPtr<IDWriteFactory> write_;
    std::map<std::tuple<std::wstring, float, FontWeight, std::wstring, std::wstring, bool, bool, TextDirection, float,
                        float, bool, float, float, float, TextAlign, VerticalAlign>,
             ComPtr<IDWriteTextLayout>>
        labels_;
    std::map<std::wstring, ComPtr<IWICBitmap>> sources_;
    std::map<std::wstring, ComPtr<IUnknown>> sourceLeases_;
    std::map<std::wstring, SurfaceImage> images_;
    std::map<std::wstring, SurfaceImage> pixels_;
    GradientCache gradients_;
    GeometryMasks masks_;
    std::map<std::tuple<LineCap, LineJoin, DashStyle, float, float>, ComPtr<ID2D1StrokeStyle>> strokes_;
    std::vector<ComPtr<ID2D1DrawingStateBlock>> states_;
    std::vector<ComPtr<ID2D1Layer>> layers_;
    std::vector<std::unique_ptr<Canvas>> layerCanvases_;
    size_t activeLayers_ = 0;
};

class Canvas::Scope {
public:
    explicit Scope(Canvas& canvas) : canvas_(canvas) {}

    ~Scope() {
        canvas_.popScope();
    }

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    Canvas& canvas_;
};

class Canvas::DrawingState {
public:
    explicit DrawingState(Canvas& canvas) : canvas_(canvas) {
        if (canvas.impl_->stateDepth_ >= 32) {
            throw std::length_error("Canvas state nesting limit reached");
        }
        if (canvas.impl_->states_.size() == canvas.impl_->stateDepth_) {
            ComPtr<ID2D1DrawingStateBlock> state;
            require(canvas.factory->CreateDrawingStateBlock(state.GetAddressOf()));
            canvas.impl_->states_.push_back(std::move(state));
        }
        state_ = canvas.impl_->states_[canvas.impl_->stateDepth_++];
        canvas.target->SaveDrawingState(state_.Get());
        ink_ = canvas.brush->GetColor();
        opacity_ = canvas.brush->GetOpacity();
    }

    ~DrawingState() {
        canvas_.target->RestoreDrawingState(state_.Get());
        canvas_.brush->SetColor(ink_);
        canvas_.brush->SetOpacity(opacity_);
        --canvas_.impl_->stateDepth_;
    }

    DrawingState(const DrawingState&) = delete;
    DrawingState& operator=(const DrawingState&) = delete;

private:
    Canvas& canvas_;
    ComPtr<ID2D1DrawingStateBlock> state_;
    D2D1_COLOR_F ink_{};
    float opacity_ = 1;
};

void Canvas::renderLayerImpl(OffscreenSurface& surface, SurfaceSize size, void* context,
                             void (*paint)(void*, Canvas&)) {
    active();
    surface.render(
        target.Get(), size,
        [&](ID2D1RenderTarget* destination) {
            if (impl_->activeLayers_ == impl_->layerCanvases_.size()) {
                impl_->layerCanvases_.push_back(
                    std::unique_ptr<Canvas>(new Canvas(LayerTag{}, *this, destination, size)));
            }
            auto& child = *impl_->layerCanvases_[impl_->activeLayers_];
            child.factory = factory;
            child.target = destination;
            child.impl_->wic_ = impl_->wic_;
            child.impl_->write_ = impl_->write_;
            child.impl_->fontCollection_ = impl_->fontCollection_;
            child.impl_->account_ = impl_->account_;
            child.impl_->width_ = size.width;
            child.impl_->height_ = size.height;
            child.impl_->dpi_ = size.devicePixelRatio;
            child.impl_->hostDrawScope_ = true;
            if (!child.brush) {
                require(destination->CreateSolidColorBrush(color(0xffffffff), child.brush.GetAddressOf()));
            }
            child.impl_->drawing_ = true;
            ++impl_->activeLayers_;
            struct Guard {
                Canvas& owner;
                Canvas& child;
                ~Guard() {
                    child.cancel();
                    // Retain only the small implementation, never a surface or lease
                    // after its owning OffscreenSurface has been destroyed.
                    child.releaseResources();
                    --owner.impl_->activeLayers_;
                }
            } guard{*this, child};
            [&](Canvas& layer) {
                paint(context, layer);
            }(child);
            if (!child.impl_->scopes_.empty() || child.impl_->stateDepth_ || child.impl_->layerDepth_) {
                throw std::logic_error("Unbalanced layer drawing state");
            }
        },
        impl_->account_);
}

void Canvas::drawInImpl(Canvas& owner, Rect area, void* context, void (*paint)(void*, Canvas&)) {
    owner.active();
    if (&owner == this || impl_->drawing_ || area.width <= 0 || area.height <= 0) {
        throw std::invalid_argument("Invalid nested Canvas producer");
    }
    const FrameBuffer borrowed{owner.impl_->width_,
                               owner.impl_->height_,
                               owner.impl_->width_ * 4,
                               uint64_t(owner.impl_->width_) * owner.impl_->height_ * 4,
                               nullptr,
                               owner.impl_->dpi_};
    targetForFrame(owner.target.Get(), borrowed, true);
    impl_->account_ = owner.impl_->account_;
    impl_->drawing_ = true;
    try {
        saved([&] {
            clipped(area, [&] {
                translate(area.x, area.y);
                [&](Canvas& producer) {
                    paint(context, producer);
                }(*this);
            });
        });
        impl_->drawing_ = false;
    } catch (...) {
        impl_->drawing_ = false;
        throw;
    }
}

void Canvas::renderImpl(const FrameBuffer& frame, Rect safe, void* context, void (*paint)(void*, Canvas&, Rect)) {
    const Rect area = begin(frame, safe);
    try {
        [&](Canvas& canvas, Rect area) {
            paint(context, canvas, area);
        }(*this, area);
        end(frame);
    } catch (...) {
        cancel();
        throw;
    }
}

void Canvas::maskedImpl(ID2D1Geometry* geometry, void* context, void (*paint)(void*), D2D1_RECT_F bounds) {
    active();
    impl_->masks_.with(
        target.Get(), geometry,
        [&]() {
            paint(context);
        },
        bounds);
}

void Canvas::savedImpl(void* context, void (*paint)(void*)) {
    active();
    DrawingState state(*this);
    [&]() {
        paint(context);
    }();
}

void Canvas::posedImpl(const Pose& pose, void* context, void (*paint)(void*)) {
    if (!std::isfinite(pose.opacity)) {
        throw std::invalid_argument("Invalid pose opacity");
    }
    saved([&] {
        translate(pose.translation.x, pose.translation.y);
        rotate(pose.rotation, pose.pivot);
        scale(pose.scale.width, pose.scale.height, pose.pivot);
        if (pose.opacity < 1) {
            withOpacity(std::clamp(pose.opacity, 0., 1.), [&]() {
                paint(context);
            });
        } else {
            [&]() {
                paint(context);
            }();
        }
    });
}

void Canvas::withOpacityImpl(double opacity, void* context, void (*paint)(void*)) {
    if (!std::isfinite(opacity) || opacity < 0 || opacity > 1) {
        throw std::invalid_argument("Invalid Canvas opacity");
    }
    saved([&] {
        if (impl_->layers_.size() == impl_->layerDepth_) {
            ComPtr<ID2D1Layer> layer;
            require(target->CreateLayer(nullptr, layer.GetAddressOf()));
            impl_->layers_.push_back(std::move(layer));
        }
        impl_->scopes_.push_back(true);
        ++impl_->stats_.opacityLayers;
        target->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                                D2D1::Matrix3x2F::Identity(), float(opacity)),
                          impl_->layers_[impl_->layerDepth_++].Get());
        Scope scope(*this);
        [&]() {
            paint(context);
        }();
    });
}

void Canvas::clippedImpl(Rect area, void* context, void (*paint)(void*)) {
    if (!impl_->drawing_ || !std::isfinite(area.x) || !std::isfinite(area.y) || !std::isfinite(area.width) ||
        !std::isfinite(area.height) || area.width < 0 || area.height < 0) {
        throw std::invalid_argument("Invalid Canvas clip");
    }
    impl_->scopes_.push_back(false);
    pushClip(area);
    Scope scope(*this);
    [&]() {
        paint(context);
    }();
}

Canvas::Canvas() : impl_(std::make_unique<Impl>()) {
    const auto result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    impl_->com_ = SUCCEEDED(result);
    if (FAILED(result) && result != RPC_E_CHANGED_MODE) {
        require(result);
    }
    try {
        require(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(impl_->wic_.GetAddressOf())));
        require(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf()));
        impl_->cpuFactory_ = factory;
        require(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                    reinterpret_cast<IUnknown**>(impl_->write_.GetAddressOf())));
    } catch (...) {
        releaseResources();
        if (impl_->com_) {
            CoUninitialize();
        }
        throw;
    }
}

Canvas::~Canvas() {
    const bool initializedCom = impl_->com_;
    cancel();
    releaseResources();
    impl_.reset();
    if (initializedCom) {
        CoUninitialize();
    }
}

IDWriteFactory* Canvas::nativeTextFactory() const {
    return impl_->write_.Get();
}

IDWriteFontCollection* Canvas::nativeFontCollection() const {
    return impl_->fontCollection_.Get();
}

IWICImagingFactory* Canvas::nativeImageFactory() const {
    return impl_->wic_.Get();
}

Canvas::Frame Canvas::frame(const FrameBuffer& output, Rect safe) {
    return Frame(*this, output, safe);
}

void Canvas::resourceAccount(ResourceAccount account) {
    impl_->account_ = account;
}

ResourceAccount Canvas::resourceAccount() const {
    return impl_->account_;
}

void Canvas::translate(double x, double y) {
    coordinate(x);
    coordinate(y);
    transform(D2D1::Matrix3x2F::Translation(float(x), float(y)));
}

void Canvas::rotate(double degrees, Point center) {
    coordinate(degrees);
    coordinate(center.x);
    coordinate(center.y);
    transform(D2D1::Matrix3x2F::Rotation(float(degrees), D2D1::Point2F(float(center.x), float(center.y))));
}

void Canvas::scale(double x, double y, Point center) {
    coordinate(x);
    coordinate(y);
    coordinate(center.x);
    coordinate(center.y);
    transform(D2D1::Matrix3x2F::Scale(float(x), float(y), D2D1::Point2F(float(center.x), float(center.y))));
}

void Canvas::pushClip(Rect area) {
    const auto bounds =
        D2D1::RectF(float(area.x), float(area.y), float(area.x + area.width), float(area.y + area.height));
    D2D1::Matrix3x2F transform;
    target->GetTransform(&transform);
    float dpiX = 0, dpiY = 0;
    target->GetDpi(&dpiX, &dpiY);
    const auto aligned = [](float value) {
        return std::isfinite(value) && value == std::floor(value);
    };
    // Pixel-aligned edges have no fractional coverage. Only their clip can use
    // the cheaper integer path; primitive/text antialiasing remains unchanged.
    // Fractional edges, rotations and shears retain Direct2D's antialiased clip.
    const bool integer = transform._12 == 0 && transform._21 == 0 &&
                         aligned((bounds.left * transform._11 + transform._31) * dpiX / 96) &&
                         aligned((bounds.right * transform._11 + transform._31) * dpiX / 96) &&
                         aligned((bounds.top * transform._22 + transform._32) * dpiY / 96) &&
                         aligned((bounds.bottom * transform._22 + transform._32) * dpiY / 96);
    target->PushAxisAlignedClip(bounds, integer ? D2D1_ANTIALIAS_MODE_ALIASED : D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
}

void Canvas::active() const {
    if (!impl_->drawing_) {
        throw std::logic_error("Canvas needs an active frame");
    }
}

void Canvas::coordinate(double value) {
    if (!std::isfinite(value) || std::abs(value) > 1e7) {
        throw std::invalid_argument("Invalid Canvas coordinate");
    }
}

void Canvas::transform(const D2D1::Matrix3x2F& value) {
    active();
    D2D1::Matrix3x2F current;
    target->GetTransform(&current);
    target->SetTransform(value * current);
}

void Canvas::popScope() noexcept {
    if (impl_->scopes_.back()) {
        target->PopLayer();
        --impl_->layerDepth_;
    } else {
        target->PopAxisAlignedClip();
    }
    impl_->scopes_.pop_back();
}

void Canvas::releaseResources() noexcept {
    clearImageUploads();
    impl_->labels_.clear();
    impl_->images_.clear();
    impl_->sources_.clear();
    impl_->sourceLeases_.clear();
    impl_->pixels_.clear();
    impl_->stats_.pixelBytes = impl_->stats_.legacyBitmapBytes = impl_->stats_.legacySourceBytes =
        impl_->stats_.surfaceBytes = 0;
    impl_->gradients_.clear();
    impl_->masks_.reset();
    impl_->layers_.clear();
    impl_->states_.clear();
    impl_->strokes_.clear();
    brush.Reset();
    target.Reset();
    impl_->bitmap_.Reset();
    impl_->bitmapLease_.Reset();
    factory.Reset();
    impl_->cpuFactory_.Reset();
    impl_->spriteBatch_.Reset();
    impl_->write_.Reset();
    impl_->wic_.Reset();
}

bool Canvas::valid(const FrameBuffer& f) const {
    return f.rgba && f.width && f.height && f.width <= 4096 && f.height <= 4096 && f.stride >= uint64_t(f.width) * 4 &&
           f.capacity >= uint64_t(f.stride) * f.height &&
           uint64_t(f.stride) * f.height <= std::numeric_limits<UINT>::max() && std::isfinite(f.device_pixel_ratio) &&
           f.device_pixel_ratio >= .5 && f.device_pixel_ratio <= 4;
}

void Canvas::targetForFrame(ID2D1RenderTarget* external, const FrameBuffer& frame, bool hostDrawScope) {
    if (impl_->drawing_) {
        throw std::logic_error("Cannot switch an active Canvas target");
    }
    if ((!external && !impl_->external_) ||
        (external && target.Get() == external && impl_->width_ == frame.width && impl_->height_ == frame.height &&
         impl_->dpi_ == frame.device_pixel_ratio)) {
        impl_->hostDrawScope_ = external && hostDrawScope;
        return;
    }
    impl_->masks_.reset();
    impl_->spriteBatch_.Reset();
    impl_->states_.clear();
    impl_->strokes_.clear();
    impl_->images_.clear();
    impl_->pixels_.clear();
    impl_->gradients_.clear();
    impl_->layers_.clear();
    clearImageUploads();
    impl_->bitmap_.Reset();
    impl_->bitmapLease_.Reset();
    brush.Reset();
    target = external;
    impl_->external_ = external != nullptr;
    impl_->hostDrawScope_ = impl_->external_ && hostDrawScope;
    impl_->stats_.legacyBitmapBytes = impl_->stats_.pixelBytes = 0;
    if (external) {
        target->GetFactory(factory.ReleaseAndGetAddressOf());
        require(target->CreateSolidColorBrush(color(0xffffffff), brush.GetAddressOf()));
        impl_->width_ = frame.width;
        impl_->height_ = frame.height;
        impl_->dpi_ = frame.device_pixel_ratio;
        ++impl_->stats_.surfaceRebuilds;
        impl_->stats_.surfaceBytes = uint64_t(impl_->width_) * impl_->height_ * 4;
    } else {
        factory = impl_->cpuFactory_;
    }
}

Rect Canvas::begin(const FrameBuffer& f, Rect safe) {
    if (impl_->drawing_) {
        throw std::logic_error("Canvas frame already active");
    }
    if (!valid(f)) {
        throw std::invalid_argument("Invalid Canvas frame");
    }
    impl_->frameStarted_ = std::chrono::steady_clock::now();
    impl_->transientBytes_ = 0;
    if (!impl_->external_ &&
        (!target || impl_->width_ != f.width || impl_->height_ != f.height || impl_->dpi_ != f.device_pixel_ratio)) {
        auto lease = impl_->account_.acquire(uint64_t(f.width) * f.height * 4);
        ComPtr<IWICBitmap> bitmap;
        ComPtr<ID2D1RenderTarget> newTarget;
        ComPtr<ID2D1SolidColorBrush> newBrush;
        require(impl_->wic_->CreateBitmap(f.width, f.height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad,
                                          bitmap.GetAddressOf()));
        require(factory->CreateWicBitmapRenderTarget(
            bitmap.Get(),
            D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                                         D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                                         float(96 * f.device_pixel_ratio), float(96 * f.device_pixel_ratio)),
            newTarget.GetAddressOf()));
        newTarget->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        newTarget->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        require(newTarget->CreateSolidColorBrush(color(0xFFFFFFFF), newBrush.GetAddressOf()));
        impl_->images_.clear();
        impl_->stats_.legacyBitmapBytes = impl_->stats_.pixelBytes = 0;
        clearImageUploads();
        impl_->pixels_.clear();
        impl_->gradients_.clear();
        impl_->masks_.reset();
        impl_->layers_.clear();
        impl_->bitmap_ = std::move(bitmap);
        impl_->bitmapLease_ = std::move(lease);
        target = std::move(newTarget);
        brush = std::move(newBrush);
        impl_->width_ = f.width;
        impl_->height_ = f.height;
        impl_->dpi_ = f.device_pixel_ratio;
        ++impl_->stats_.surfaceRebuilds;
        impl_->stats_.surfaceBytes = uint64_t(impl_->width_) * impl_->height_ * 4;
        trackMemory();
    }
    const double w = f.width / impl_->dpi_, h = f.height / impl_->dpi_;
    if (!std::isfinite(safe.x) || !std::isfinite(safe.y) || !std::isfinite(safe.width) || !std::isfinite(safe.height) ||
        safe.width <= 0 || safe.height <= 0) {
        safe = {0, 0, w, h};
    }
    safe.x = std::clamp(safe.x, 0., w);
    safe.y = std::clamp(safe.y, 0., h);
    safe.width = std::clamp(safe.width, 0., w - safe.x);
    safe.height = std::clamp(safe.height, 0., h - safe.y);
    impl_->scopes_.push_back(false);
    if (!impl_->hostDrawScope_) {
        target->BeginDraw();
    }
    impl_->drawing_ = true;
    target->SetTransform(D2D1::Matrix3x2F::Identity());
    target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    brush->SetOpacity(1);
    target->Clear(D2D1::ColorF(0, 0, 0, 0));
    target->SetTransform(D2D1::Matrix3x2F::Translation(float(safe.x), float(safe.y)));
    pushClip({0, 0, safe.width, safe.height});
    return safe;
}

void Canvas::sprites(ID2D1Bitmap* atlas, std::span<const D2D1_RECT_F> destinations,
                     std::span<const D2D1_RECT_U> sources, std::span<const D2D1_MATRIX_3X2_F> transforms,
                     ImageSampling sampling, std::span<const float> opacities) {
    active();
    if (!atlas || destinations.size() != sources.size() || destinations.size() > 100000 ||
        (!transforms.empty() && transforms.size() != destinations.size()) ||
        (!opacities.empty() && opacities.size() != destinations.size()) ||
        std::any_of(opacities.begin(), opacities.end(), [](float value) {
            return !std::isfinite(value) || value < 0 || value > 1;
        })) {
        throw std::invalid_argument("Invalid sprite batch");
    }
    if (destinations.empty()) {
        return;
    }
    ComPtr<ID2D1DeviceContext3> context;
    const auto interpolation = sampling == ImageSampling::Nearest ? D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
                                                                  : D2D1_BITMAP_INTERPOLATION_MODE_LINEAR;
    if (SUCCEEDED(target.As(&context))) {
        if (!impl_->spriteBatch_) {
            require(context->CreateSpriteBatch(&impl_->spriteBatch_));
        }
        impl_->spriteBatch_->Clear();
        std::vector<D2D1_COLOR_F> colors;
        for (const float alpha : opacities) {
            colors.push_back(D2D1::ColorF(1, 1, 1, alpha));
        }
        require(impl_->spriteBatch_->AddSprites(UINT32(destinations.size()), destinations.data(), sources.data(),
                                                colors.empty() ? nullptr : colors.data(),
                                                transforms.empty() ? nullptr : transforms.data()));
        const auto antialias = context->GetAntialiasMode();
        context->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
        context->DrawSpriteBatch(impl_->spriteBatch_.Get(), 0, UINT32(destinations.size()), atlas, interpolation,
                                 D2D1_SPRITE_OPTIONS_CLAMP_TO_SOURCE_RECTANGLE);
        context->SetAntialiasMode(antialias);
        ++impl_->stats_.spriteBatches;
        impl_->stats_.spriteInstances += destinations.size();
        return;
    }
    const auto pixels = atlas->GetPixelSize();
    const auto logical = atlas->GetSize();
    for (size_t i = 0; i < destinations.size(); ++i) {
        const auto& s = sources[i];
        const auto draw = [&] {
            target->DrawBitmap(
                atlas, destinations[i], opacities.empty() ? 1 : opacities[i], interpolation,
                D2D1::RectF(s.left * logical.width / pixels.width, s.top * logical.height / pixels.height,
                            s.right * logical.width / pixels.width, s.bottom * logical.height / pixels.height));
        };
        if (transforms.empty() || (transforms[i]._11 == 1 && transforms[i]._12 == 0 && transforms[i]._21 == 0 &&
                                   transforms[i]._22 == 1 && transforms[i]._31 == 0 && transforms[i]._32 == 0)) {
            draw();
        } else {
            saved([&] {
                D2D1_MATRIX_3X2_F previous;
                target->GetTransform(&previous);
                target->SetTransform(transforms[i] * previous);
                draw();
            });
        }
    }
}

void Canvas::end(const FrameBuffer& f) {
    try {
        if (!impl_->drawing_ || !valid(f) || f.width != impl_->width_ || f.height != impl_->height_ ||
            f.device_pixel_ratio != impl_->dpi_) {
            throw std::invalid_argument("Canvas output does not match the active frame");
        }
        while (!impl_->scopes_.empty()) {
            popScope();
        }
        impl_->drawing_ = false;
        {
            ScopedDuration duration(impl_->stats_.rasterNanoseconds);
            if (!impl_->hostDrawScope_) {
                require(target->EndDraw());
            }
        }
        // Read premultiplied bytes unchanged, then only reorder channels. WIC's
        // PBGRA -> PRGBA converter can unpremultiply/re-premultiply and lose a
        // color level at translucent edges. Row padding remains caller-owned.
        if (!impl_->external_) {
            ScopedDuration duration(impl_->stats_.pixelCopies.nanoseconds);
            ComPtr<IWICBitmapLock> lock;
            const WICRect area{0, 0, int(f.width), int(f.height)};
            UINT sourceStride = 0, sourceCapacity = 0;
            BYTE* source = nullptr;
            // GetDataPointer is documented for STA use. An already initialized MTA
            // retains CopyPixels; release unsuccessful locks before that fallback.
            const bool mapped = impl_->com_ && SUCCEEDED(impl_->bitmap_->Lock(&area, WICBitmapLockRead, &lock)) &&
                                SUCCEEDED(lock->GetStride(&sourceStride)) &&
                                SUCCEEDED(lock->GetDataPointer(&sourceCapacity, &source)) && source &&
                                sourceStride >= uint64_t(f.width) * 4 &&
                                sourceCapacity >= uint64_t(sourceStride) * (f.height - 1) + uint64_t(f.width) * 4;
            if (!mapped) {
                lock.Reset();
                require(impl_->bitmap_->CopyPixels(nullptr, f.stride, UINT(uint64_t(f.stride) * f.height), f.rgba));
                source = f.rgba;
                sourceStride = f.stride;
            }
            for (uint32_t y = 0; y < f.height; ++y) {
                auto* row = f.rgba + size_t(y) * f.stride;
                const auto* input = source + size_t(y) * sourceStride;
                uint32_t x = 0;
#if defined(_M_X64) || defined(__x86_64__)
                // SSE2 is part of the x64 baseline. Unaligned access supports arbitrary
                // caller buffers and strides without touching padding or alpha values.
                const auto blue = _mm_set1_epi32(0x000000ff);
                const auto greenAlpha = _mm_set1_epi32(static_cast<int>(0xff00ff00u));
                for (; x + 4 <= f.width; x += 4) {
                    auto* address = reinterpret_cast<__m128i*>(row + size_t(x) * 4);
                    const auto pixel = _mm_loadu_si128(reinterpret_cast<const __m128i*>(input + size_t(x) * 4));
                    const auto red = _mm_and_si128(_mm_srli_epi32(pixel, 16), blue);
                    const auto swapped = _mm_or_si128(red, _mm_slli_epi32(_mm_and_si128(pixel, blue), 16));
                    _mm_storeu_si128(address, _mm_or_si128(swapped, _mm_and_si128(pixel, greenAlpha)));
                }
#endif
                for (; x < f.width; ++x) {
                    const auto blueByte = input[x * 4];
                    row[x * 4] = input[x * 4 + 2];
                    row[x * 4 + 1] = input[x * 4 + 1];
                    row[x * 4 + 2] = blueByte;
                    row[x * 4 + 3] = input[x * 4 + 3];
                }
            }
            ++impl_->stats_.pixelCopies.count;
        }
        ++impl_->stats_.frames.count;
        impl_->stats_.frames.nanoseconds += nanosecondsSince(impl_->frameStarted_);
        impl_->transientBytes_ = 0;
    } catch (...) {
        impl_->transientBytes_ = 0;
        cancel();
        throw;
    }
}

void Canvas::cancel() noexcept {
    if (!impl_->drawing_) {
        return;
    }
    while (!impl_->scopes_.empty()) {
        popScope();
    }
    if (!impl_->hostDrawScope_) {
        target->EndDraw();
    }
    impl_->drawing_ = false;
    ++impl_->stats_.cancelledFrames;
    impl_->transientBytes_ = 0;
}

void Canvas::ink(uint32_t rgba) {
    active();
    brush->SetColor(color(rgba));
}

void Canvas::box(double x, double y, double w, double h, double r, uint32_t rgba) {
    ink(rgba);
    target->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(float(x), float(y), float(x + w), float(y + h)), float(r), float(r)),
        brush.Get());
}

void Canvas::rectangle(Rect area, uint32_t rgba, double opacity) {
    if (!std::isfinite(opacity) || opacity < 0 || opacity > 1) {
        throw std::invalid_argument("Invalid rectangle opacity");
    }
    active();
    auto tint = color(rgba);
    tint.a *= float(opacity);
    brush->SetColor(tint);
    target->FillRectangle(
        D2D1::RectF(float(area.x), float(area.y), float(area.x + area.width), float(area.y + area.height)),
        brush.Get());
}

void Canvas::gradientBox(Rect area, double radius, uint32_t top, uint32_t bottom) {
    const D2D1_GRADIENT_STOP stops[]{{0, color(top)}, {1, color(bottom)}};
    const auto gradient = linearGradient({area.x, area.y}, {area.x, area.y + area.height}, stops);
    target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(float(area.x), float(area.y), float(area.x + area.width),
                                                               float(area.y + area.height)),
                                                   float(radius), float(radius)),
                                 gradient.Get());
}

ComPtr<ID2D1LinearGradientBrush> Canvas::linearGradient(Point start, Point end,
                                                        std::span<const D2D1_GRADIENT_STOP> stops,
                                                        ID2D1RenderTarget* destination) {
    active();
    return impl_->gradients_.linear(destination ? destination : target.Get(), start, end, stops);
}

ComPtr<ID2D1RadialGradientBrush> Canvas::radialGradient(Point center, Size radii,
                                                        std::span<const D2D1_GRADIENT_STOP> stops, Point offset,
                                                        ID2D1RenderTarget* destination) {
    active();
    return impl_->gradients_.radial(destination ? destination : target.Get(), center, radii, stops, offset);
}

void Canvas::releaseGradientTarget(ID2D1RenderTarget* destination) noexcept {
    impl_->gradients_.forget(destination);
}

GradientCacheStats Canvas::gradientCacheStats() const {
    return impl_->gradients_.stats();
}

void Canvas::oval(double x, double y, double rx, double ry, uint32_t rgba, double stroke) {
    ink(rgba);
    const auto ellipse = D2D1::Ellipse(D2D1::Point2F(float(x), float(y)), float(rx), float(ry));
    if (stroke > 0) {
        target->DrawEllipse(ellipse, brush.Get(), float(stroke));
    } else {
        target->FillEllipse(ellipse, brush.Get());
    }
}

void Canvas::line(double x, double y, double xx, double yy, uint32_t rgba, double thickness) {
    ink(rgba);
    target->DrawLine(D2D1::Point2F(float(x), float(y)), D2D1::Point2F(float(xx), float(yy)), brush.Get(),
                     float(thickness));
}

double Canvas::devicePixelRatio() const {
    return impl_->dpi_ > 0 ? impl_->dpi_ : 1;
}

void Canvas::blit(const FrameBuffer& source, Rect area) {
    active();
    detail::layoutRect(area);
    if (!valid(source) || area.width <= 0 || area.height <= 0) {
        throw std::invalid_argument("Invalid frame blit");
    }
    const uint64_t bytes = uint64_t(source.width) * source.height * 4;
    if (bytes > impl_->uploadBudget_ - impl_->transientBytes_) {
        throw std::length_error("Frame transient upload budget exceeded");
    }
    ScopedDuration duration(impl_->stats_.uploads.nanoseconds);
    ComPtr<ID2D1Bitmap> image;
    auto lease = impl_->account_.acquire(bytes);
    require(target->CreateBitmap(
        D2D1::SizeU(source.width, source.height), source.rgba, source.stride,
        D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_R8G8B8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                               float(96 * source.device_pixel_ratio), float(96 * source.device_pixel_ratio)),
        image.GetAddressOf()));
    retainTextureLease(image.Get(), lease.Get());
    impl_->transientBytes_ += bytes;
    impl_->stats_.uploadedBytes += bytes;
    ++impl_->stats_.uploads.count;
    trackMemory();
    target->DrawBitmap(
        image.Get(), D2D1::RectF(float(area.x), float(area.y), float(area.x + area.width), float(area.y + area.height)),
        1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}

void Canvas::pixels(const std::wstring& key, const FrameBuffer& source, Rect area) {
    active();
    if (key.empty() || !valid(source) || source.width > 1024 || source.height > 1024 || !std::isfinite(area.x) ||
        !std::isfinite(area.y) || !std::isfinite(area.width) || !std::isfinite(area.height) || area.width <= 0 ||
        area.height <= 0) {
        throw std::invalid_argument("Invalid pixel image");
    }
    auto cached = impl_->pixels_.find(key);
    if (cached == impl_->pixels_.end()) {
        ScopedDuration duration(impl_->stats_.uploads.nanoseconds);
        ComPtr<ID2D1Bitmap> bitmap;
        const auto bytes = uint64_t(source.width) * source.height * 4;
        auto lease = impl_->account_.acquire(bytes);
        const auto properties =
            D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_R8G8B8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                                   float(96 * source.device_pixel_ratio), float(96 * source.device_pixel_ratio));
        require(target->CreateBitmap(D2D1::SizeU(source.width, source.height), source.rgba, source.stride, properties,
                                     bitmap.GetAddressOf()));
        if (impl_->pixels_.size() >= 64) {
            impl_->pixels_.clear();
            impl_->stats_.pixelBytes = 0;
        }
        retainTextureLease(bitmap.Get(), lease.Get());
        cached = impl_->pixels_.emplace(key, SurfaceImage(bitmap.Get(), lease.Get())).first;
        impl_->stats_.pixelBytes += bytes;
        impl_->stats_.uploadedBytes += bytes;
        ++impl_->stats_.uploads.count;
        trackMemory();
    } else {
        ++impl_->stats_.bitmapCacheHits;
    }
    target->DrawBitmap(
        cached->second.Get(),
        D2D1::RectF(float(area.x), float(area.y), float(area.x + area.width), float(area.y + area.height)), 1,
        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}

void Canvas::image(const std::wstring& filename, Rect area, uint32_t cell, uint32_t cells) {
    active();
    if (!cells || cell >= cells || area.width <= 0 || area.height <= 0) {
        throw std::invalid_argument("Invalid image placement");
    }
    auto cached = impl_->images_.find(filename);
    if (cached == impl_->images_.end()) {
        if (impl_->images_.size() >= 64) {
            impl_->images_.clear();
            impl_->stats_.legacyBitmapBytes = 0;
        }
        auto source = impl_->sources_.find(filename);
        if (source == impl_->sources_.end()) {
            ScopedDuration duration(impl_->stats_.legacyDecodes.nanoseconds);
            ++impl_->stats_.legacyDecodes.count;
            if (impl_->sources_.size() >= 64) {
                impl_->sources_.clear();
                impl_->sourceLeases_.clear();
                impl_->stats_.legacySourceBytes = 0;
            }
            ComPtr<IWICBitmapDecoder> decoder;
            require(impl_->wic_->CreateDecoderFromFilename(filename.c_str(), nullptr, GENERIC_READ,
                                                           WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf()));
            ComPtr<IWICBitmapFrameDecode> frame;
            require(decoder->GetFrame(0, frame.GetAddressOf()));
            ComPtr<IWICFormatConverter> converter;
            require(impl_->wic_->CreateFormatConverter(converter.GetAddressOf()));
            require(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr,
                                          0, WICBitmapPaletteTypeCustom));
            UINT width = 0, height = 0;
            require(converter->GetSize(&width, &height));
            ComPtr<IWICBitmapScaler> scaled;
            require(impl_->wic_->CreateBitmapScaler(scaled.GetAddressOf()));
            // Preserve source detail at normal and high DPI; bound oversized assets
            // by their longest edge rather than applying a widget-specific thumbnail.
            const double scale = std::min(1., 4096. / std::max(width, height));
            require(scaled->Initialize(converter.Get(), std::max(1u, UINT(std::lround(width * scale))),
                                       std::max(1u, UINT(std::lround(height * scale))),
                                       WICBitmapInterpolationModeFant));
            ComPtr<IWICBitmap> decoded;
            const auto scaledWidth = std::max(1u, UINT(std::lround(width * scale)));
            const auto scaledHeight = std::max(1u, UINT(std::lround(height * scale)));
            auto lease = impl_->account_.acquire(uint64_t(scaledWidth) * scaledHeight * 4);
            require(impl_->wic_->CreateBitmapFromSource(scaled.Get(), WICBitmapCacheOnLoad, decoded.GetAddressOf()));
            source = impl_->sources_.emplace(filename, std::move(decoded)).first;
            impl_->sourceLeases_[filename] = std::move(lease);
            UINT decodedWidth = 0, decodedHeight = 0;
            require(source->second->GetSize(&decodedWidth, &decodedHeight));
            impl_->stats_.legacySourceBytes += uint64_t(decodedWidth) * decodedHeight * 4;
            trackMemory();
        }
        ComPtr<ID2D1Bitmap> bitmap;
        ScopedDuration duration(impl_->stats_.uploads.nanoseconds);
        UINT sourceWidth = 0, sourceHeight = 0;
        require(source->second->GetSize(&sourceWidth, &sourceHeight));
        auto lease = impl_->account_.acquire(uint64_t(sourceWidth) * sourceHeight * 4);
        require(target->CreateBitmapFromWicBitmap(source->second.Get(), nullptr, bitmap.GetAddressOf()));
        retainTextureLease(bitmap.Get(), lease.Get());
        cached = impl_->images_.emplace(filename, SurfaceImage(bitmap.Get(), lease.Get())).first;
        const auto size = cached->second->GetPixelSize();
        const auto bytes = uint64_t(size.width) * size.height * 4;
        impl_->stats_.legacyBitmapBytes += bytes;
        impl_->stats_.uploadedBytes += bytes;
        ++impl_->stats_.uploads.count;
        trackMemory();
    } else {
        ++impl_->stats_.bitmapCacheHits;
    }
    const auto pixels = cached->second->GetSize();
    const double sourceWidth = pixels.width / cells, sourceHeight = pixels.height;
    const double scale = std::min(area.width / sourceWidth, area.height / sourceHeight);
    const double width = sourceWidth * scale, height = sourceHeight * scale;
    const auto destination =
        D2D1::RectF(float(area.x + (area.width - width) * .5), float(area.y + (area.height - height) * .5),
                    float(area.x + (area.width + width) * .5), float(area.y + (area.height + height) * .5));
    const auto source = D2D1::RectF(float(cell * sourceWidth), 0, float((cell + 1) * sourceWidth), float(sourceHeight));
    target->DrawBitmap(cached->second.Get(), destination, 1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, source);
}

void Canvas::image(const ImageData& image, Rect area, ImagePlacement placement) {
    active();
    detail::layoutRect(area);
    if (area.width <= 0 || area.height <= 0 || !placement.cells || placement.cell >= placement.cells ||
        placement.cells > image.width() || !image.id() || !std::isfinite(placement.opacity) || placement.opacity < 0 ||
        placement.opacity > 1 || static_cast<unsigned>(placement.fit) > static_cast<unsigned>(ImageFit::Stretch) ||
        static_cast<unsigned>(placement.sampling) > static_cast<unsigned>(ImageSampling::Nearest)) {
        throw std::invalid_argument("Invalid resource image placement");
    }
    auto found = impl_->uploads_.find(image.id());
    if (found == impl_->uploads_.end()) {
        const auto bytes = uint64_t(image.width()) * image.height() * 4;
        if (bytes > impl_->uploadBudget_) {
            throw std::length_error("Image exceeds the Canvas upload budget");
        }
        while (!impl_->uploads_.empty() &&
               (impl_->uploadedBytes_ + bytes > impl_->uploadBudget_ || impl_->uploads_.size() >= 64)) {
            const auto oldest =
                std::min_element(impl_->uploads_.begin(), impl_->uploads_.end(), [](const auto& a, const auto& b) {
                    return a.second.age < b.second.age;
                });
            impl_->uploadedBytes_ -= oldest->second.bytes;
            impl_->uploads_.erase(oldest);
        }
        ScopedDuration duration(impl_->stats_.uploads.nanoseconds);
        ComPtr<ID2D1Bitmap> bitmap;
        auto lease = impl_->account_.acquire(bytes);
        require(target->CreateBitmap(
            D2D1::SizeU(image.width(), image.height()), image.pixels().data(), image.stride(),
            D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
            bitmap.GetAddressOf()));
        retainTextureLease(bitmap.Get(), lease.Get());
        found =
            impl_->uploads_.emplace(image.id(), Impl::Upload{SurfaceImage(bitmap.Get(), lease.Get()), bytes, 0}).first;
        impl_->uploadedBytes_ += bytes;
        impl_->stats_.uploadedBytes += bytes;
        ++impl_->stats_.uploads.count;
        trackMemory();
    } else {
        ++impl_->stats_.bitmapCacheHits;
    }
    found->second.age = ++impl_->uploadClock_;
    const double sourceWidth = double(image.width()) / placement.cells, sourceHeight = image.height();
    double width = area.width, height = area.height;
    if (placement.fit != ImageFit::Stretch) {
        const double scale = placement.fit == ImageFit::Cover ? std::max(width / sourceWidth, height / sourceHeight)
                                                              : std::min(width / sourceWidth, height / sourceHeight);
        width = sourceWidth * scale;
        height = sourceHeight * scale;
    }
    const auto destination =
        D2D1::RectF(float(area.x + (area.width - width) * .5), float(area.y + (area.height - height) * .5),
                    float(area.x + (area.width + width) * .5), float(area.y + (area.height + height) * .5));
    const auto source = D2D1::RectF(float(placement.cell * sourceWidth), 0, float((placement.cell + 1) * sourceWidth),
                                    float(sourceHeight));
    const auto paint = [&] {
        target->DrawBitmap(found->second.bitmap.Get(), destination, float(placement.opacity),
                           placement.sampling == ImageSampling::Nearest
                               ? D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
                               : D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                           source);
    };
    if (placement.clip) {
        clipped(area, paint);
    } else {
        paint();
    }
}

uint64_t Canvas::imageUploadBytes() const {
    return impl_->uploadedBytes_;
}

CanvasStats Canvas::stats() const {
    auto result = impl_->stats_;
    result.resourceBytes = impl_->uploadedBytes_;
    result.transientBytes = impl_->transientBytes_;
    result.trackedBytes = trackedBytes();
    result.textEntries = impl_->labels_.size();
    result.bitmapEntries = impl_->uploads_.size() + impl_->images_.size() + impl_->pixels_.size();
    return result;
}

void Canvas::clearImageUploads() {
    impl_->uploads_.clear();
    impl_->uploadedBytes_ = 0;
}

void Canvas::fontCollection(IDWriteFontCollection* collection) {
    if (impl_->fontCollection_.Get() != collection) {
        impl_->fontCollection_ = collection;
        impl_->labels_.clear();
    }
}

ComPtr<IDWriteTextLayout> Canvas::nativeTextLayout(const std::wstring& value, const TextStyle& style, Size available) {
    return textLayout(value, style, available);
}

Rect Canvas::measureTextInk(const std::wstring& value, const TextStyle& style, Size available) {
    ScopedDuration duration(impl_->stats_.textMeasures.nanoseconds);
    ++impl_->stats_.textMeasures.count;
    auto layout = textLayout(value, style, available);
    DWRITE_OVERHANG_METRICS bounds{};
    require(layout->GetOverhangMetrics(&bounds));
    return {-bounds.left, -bounds.top, double(layout->GetMaxWidth()) + bounds.left + bounds.right,
            double(layout->GetMaxHeight()) + bounds.top + bounds.bottom};
}

Canvas::InkText Canvas::fitTextInk(const std::wstring& value, TextStyle style, Rect area,
                                   std::span<const std::wstring> references, double shrinkMargin) {
    detail::layoutRect(area);
    if (area.width <= 0 || area.height <= 0 || !std::isfinite(shrinkMargin) || shrinkMargin <= 0 || shrinkMargin > 1 ||
        references.size() > 128) {
        throw std::invalid_argument("Invalid ink text fit");
    }
    const auto align = style.align;
    const auto vertical = style.vertical;
    style.align = TextAlign::Start;
    style.vertical = VerticalAlign::Top;
    style.wrap = false;
    style.ellipsis = false;
    auto ink = measureTextInk(value, style);
    Size reference{ink.width, ink.height};
    for (const auto& sample : references) {
        const auto bounds = measureTextInk(sample, style);
        reference.width = std::max(reference.width, bounds.width);
        reference.height = std::max(reference.height, bounds.height);
    }
    const double fit =
        std::min({1., area.width / std::max(.01, reference.width), area.height / std::max(.01, reference.height)});
    if (fit < shrinkMargin) {
        style.size = float(style.size * fit * shrinkMargin);
        ink = measureTextInk(value, style);
    }
    const double x = area.x + (area.width - ink.width) * (align == TextAlign::Center ? .5
                                                          : align == TextAlign::End  ? 1
                                                                                     : 0);
    const double y = area.y + (area.height - ink.height) * (vertical == VerticalAlign::Center   ? .5
                                                            : vertical == VerticalAlign::Bottom ? 1
                                                                                                : 0);
    const Point origin{float(x - ink.x), float(y - ink.y)};
    return {
        textLayout(value, style, {1000, 1000}), origin, {origin.x + ink.x, origin.y + ink.y, ink.width, ink.height}};
}

ComPtr<IDWriteTextLayout> Canvas::textLayout(const std::wstring& value, const TextStyle& style, Size available) {
    if (!std::isfinite(style.size) || style.size <= 0 || style.size > 4096 ||
        value.size() > std::numeric_limits<int>::max() || !std::isfinite(style.lineHeight) ||
        !std::isfinite(style.baseline) || style.lineHeight < 0 || style.lineHeight > 4096 || style.baseline < 0 ||
        style.baseline > style.lineHeight || !std::isfinite(style.characterSpacing) ||
        std::abs(style.characterSpacing) > 4096) {
        throw std::invalid_argument("Invalid Canvas text");
    }
    detail::layoutExtent(available.width);
    detail::layoutExtent(available.height);
    coordinate(available.width);
    coordinate(available.height);
    const auto weight = style.bold ? FontWeight::Bold : style.weight;
    const auto key = std::make_tuple(
        value, float(style.size), weight, style.family, style.locale, style.wrap, style.ellipsis, style.direction,
        float(style.lineHeight), float(style.baseline), style.tabularFigures, float(style.characterSpacing),
        float(std::max(1., available.width)), float(std::max(1., available.height)), style.align, style.vertical);
    auto found = impl_->labels_.find(key);
    if (found == impl_->labels_.end()) {
        ScopedDuration duration(impl_->stats_.textLayouts.nanoseconds);
        ++impl_->stats_.textLayouts.count;
        if (impl_->labels_.size() >= 384) {
            impl_->labels_.clear();
        }
        ComPtr<IDWriteTextFormat> format;
        require(impl_->write_->CreateTextFormat(
            style.family.c_str(), impl_->fontCollection_.Get(), DWRITE_FONT_WEIGHT(weight), DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, float(style.size), style.locale.c_str(), format.GetAddressOf()));
        require(format->SetWordWrapping(style.wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP));
        if (style.lineHeight > 0) {
            require(format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, float(style.lineHeight),
                                           float(style.baseline > 0 ? style.baseline : style.lineHeight * .8)));
        }
        auto direction = style.direction;
        if (direction == TextDirection::Auto) {
            direction = TextDirection::LeftToRight;
            std::vector<WORD> types(value.size());
            if (!value.empty() && GetStringTypeW(CT_CTYPE2, value.data(), int(value.size()), types.data())) {
                for (auto type : types) {
                    if (type == C2_RIGHTTOLEFT || type == C2_LEFTTORIGHT) {
                        direction = type == C2_RIGHTTOLEFT ? TextDirection::RightToLeft : TextDirection::LeftToRight;
                        break;
                    }
                }
            }
        }
        require(format->SetReadingDirection(direction == TextDirection::RightToLeft
                                                ? DWRITE_READING_DIRECTION_RIGHT_TO_LEFT
                                                : DWRITE_READING_DIRECTION_LEFT_TO_RIGHT));
        if (style.ellipsis) {
            ComPtr<IDWriteInlineObject> ellipsis;
            require(impl_->write_->CreateEllipsisTrimmingSign(format.Get(), ellipsis.GetAddressOf()));
            const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            require(format->SetTrimming(&trimming, ellipsis.Get()));
        }
        ComPtr<IDWriteTextLayout> layout;
        require(impl_->write_->CreateTextLayout(value.data(), UINT32(value.size()), format.Get(), 2000, 1000,
                                                layout.GetAddressOf()));
        if (style.characterSpacing != 0) {
            ComPtr<IDWriteTextLayout1> spacing;
            require(layout.As(&spacing));
            require(spacing->SetCharacterSpacing(float(style.characterSpacing), float(style.characterSpacing), 0,
                                                 {0, UINT32(value.size())}));
        }
        if (style.tabularFigures) {
            ComPtr<IDWriteTypography> typography;
            require(impl_->write_->CreateTypography(typography.GetAddressOf()));
            require(typography->AddFontFeature({DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES, 1}));
            require(layout->SetTypography(typography.Get(), {0, UINT32(value.size())}));
        }
        require(layout->SetMaxWidth(float(std::max(1., available.width))));
        require(layout->SetMaxHeight(float(std::max(1., available.height))));
        require(layout->SetTextAlignment(style.align == TextAlign::Center ? DWRITE_TEXT_ALIGNMENT_CENTER
                                         : style.align == TextAlign::End  ? DWRITE_TEXT_ALIGNMENT_TRAILING
                                                                          : DWRITE_TEXT_ALIGNMENT_LEADING));
        require(layout->SetParagraphAlignment(style.vertical == VerticalAlign::Top ? DWRITE_PARAGRAPH_ALIGNMENT_NEAR
                                              : style.vertical == VerticalAlign::Bottom
                                                  ? DWRITE_PARAGRAPH_ALIGNMENT_FAR
                                                  : DWRITE_PARAGRAPH_ALIGNMENT_CENTER));
        found = impl_->labels_.emplace(key, layout).first;
    } else {
        ++impl_->stats_.textCacheHits;
    }
    return found->second;
}

ID2D1StrokeStyle* Canvas::strokeStyle(const StrokeStyle& style) {
    coordinate(style.dashOffset);
    if (!std::isfinite(style.width) || style.width <= 0 || style.width > 4096 || !std::isfinite(style.miterLimit) ||
        style.miterLimit < 1 || style.miterLimit > 4096 || !std::isfinite(style.dashOffset)) {
        throw std::invalid_argument("Invalid Canvas stroke");
    }
    const auto key =
        std::make_tuple(style.cap, style.join, style.dash, float(style.miterLimit), float(style.dashOffset));
    auto found = impl_->strokes_.find(key);
    if (found == impl_->strokes_.end()) {
        if (impl_->strokes_.size() >= 32) {
            impl_->strokes_.clear();
        }
        const auto cap = style.cap == LineCap::Round    ? D2D1_CAP_STYLE_ROUND
                         : style.cap == LineCap::Square ? D2D1_CAP_STYLE_SQUARE
                                                        : D2D1_CAP_STYLE_FLAT;
        const auto join = style.join == LineJoin::Round   ? D2D1_LINE_JOIN_ROUND
                          : style.join == LineJoin::Bevel ? D2D1_LINE_JOIN_BEVEL
                                                          : D2D1_LINE_JOIN_MITER;
        const auto dash = style.dash == DashStyle::Dash      ? D2D1_DASH_STYLE_DASH
                          : style.dash == DashStyle::Dot     ? D2D1_DASH_STYLE_DOT
                          : style.dash == DashStyle::DashDot ? D2D1_DASH_STYLE_DASH_DOT
                                                             : D2D1_DASH_STYLE_SOLID;
        ComPtr<ID2D1StrokeStyle> stroke;
        require(factory->CreateStrokeStyle(
            D2D1::StrokeStyleProperties(cap, cap, cap, join, float(style.miterLimit), dash, float(style.dashOffset)),
            nullptr, 0, stroke.GetAddressOf()));
        found = impl_->strokes_.emplace(key, std::move(stroke)).first;
    }
    return found->second.Get();
}

TextMetrics Canvas::measureText(const std::wstring& value, const TextStyle& style, Size available) {
    ScopedDuration duration(impl_->stats_.textMeasures.nanoseconds);
    ++impl_->stats_.textMeasures.count;
    auto layout = textLayout(value, style, available);
    DWRITE_TEXT_METRICS metrics{};
    require(layout->GetMetrics(&metrics));
    return {metrics.width, metrics.height, metrics.widthIncludingTrailingWhitespace, metrics.lineCount};
}

std::vector<TextLineMetrics> Canvas::textLines(const std::wstring& value, const TextStyle& style, double width) {
    auto layout = textLayout(value, style, {width, 16384});
    uint32_t count = 0;
    layout->GetLineMetrics(nullptr, 0, &count);
    std::vector<DWRITE_LINE_METRICS> lines(count);
    require(layout->GetLineMetrics(lines.data(), count, &count));
    std::vector<TextLineMetrics> result;
    result.reserve(count);
    uint32_t offset = 0;
    for (const auto& line : lines) {
        result.push_back({offset, line.length, line.height});
        offset += line.length;
    }
    return result;
}

void Canvas::text(const std::wstring& value, const TextStyle& style, Rect area, uint32_t rgba) {
    auto layout = textLayout(value, style, {area.width, area.height});
    ink(rgba);
    ++impl_->stats_.textDraws;
    target->DrawTextLayout(D2D1::Point2F(float(area.x), float(area.y)), layout.Get(), brush.Get(),
                           D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

Path Canvas::path(FillRule rule) {
    return Path(factory.Get(), rule);
}

void Canvas::fill(Path& path, uint32_t rgba) {
    ink(rgba);
    target->FillGeometry(path.geometry(factory.Get()), brush.Get());
}

void Canvas::stroke(Path& path, uint32_t rgba, const StrokeStyle& style) {
    active();
    auto geometry = path.geometry(factory.Get());
    auto stroke = strokeStyle(style);
    ink(rgba);
    target->DrawGeometry(geometry, brush.Get(), float(style.width), stroke);
}

void Canvas::strokeBox(Rect area, double radius, uint32_t rgba, const StrokeStyle& style) {
    active();
    auto stroke = strokeStyle(style);
    ink(rgba);
    target->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(float(area.x), float(area.y), float(area.x + area.width),
                                                               float(area.y + area.height)),
                                                   float(radius), float(radius)),
                                 brush.Get(), float(style.width), stroke);
}

void Canvas::shape(const std::vector<D2D1_POINT_2F>& points, uint32_t fill, uint32_t outline, double thickness) {
    if (points.size() < 3) {
        return;
    }
    ComPtr<ID2D1PathGeometry> path;
    ComPtr<ID2D1GeometrySink> sink;
    require(factory->CreatePathGeometry(path.GetAddressOf()));
    require(path->Open(sink.GetAddressOf()));
    sink->BeginFigure(points.front(), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLines(points.data() + 1, UINT32(points.size() - 1));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    require(sink->Close());
    ink(fill);
    target->FillGeometry(path.Get(), brush.Get());
    if (outline & 255) {
        ink(outline);
        target->DrawGeometry(path.Get(), brush.Get(), float(thickness));
    }
}

bool Canvas::save(const std::wstring& filename, const FrameBuffer& f) {
    try {
        if (!valid(f)) {
            return false;
        }
        ComPtr<IWICBitmap> source;
        require(impl_->wic_->CreateBitmapFromMemory(f.width, f.height, GUID_WICPixelFormat32bppPRGBA, f.stride,
                                                    UINT(uint64_t(f.stride) * f.height), f.rgba,
                                                    source.GetAddressOf()));
        ComPtr<IWICFormatConverter> straight;
        require(impl_->wic_->CreateFormatConverter(straight.GetAddressOf()));
        require(straight->Initialize(source.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                     WICBitmapPaletteTypeCustom));
        ComPtr<IWICStream> stream;
        require(impl_->wic_->CreateStream(stream.GetAddressOf()));
        require(stream->InitializeFromFilename(filename.c_str(), GENERIC_WRITE));
        ComPtr<IWICBitmapEncoder> encoder;
        require(impl_->wic_->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf()));
        require(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
        ComPtr<IWICBitmapFrameEncode> frame;
        require(encoder->CreateNewFrame(frame.GetAddressOf(), nullptr));
        require(frame->Initialize(nullptr));
        require(frame->SetSize(f.width, f.height));
        auto format = GUID_WICPixelFormat32bppBGRA;
        require(frame->SetPixelFormat(&format));
        require(frame->WriteSource(straight.Get(), nullptr));
        require(frame->Commit());
        require(encoder->Commit());
        return true;
    } catch (...) {
        return false;
    }
}

Canvas::Canvas(LayerTag, Canvas& owner, ID2D1RenderTarget* destination, SurfaceSize size)

    : factory(owner.factory), target(destination), impl_(std::make_unique<Impl>()) {
    impl_->width_ = size.width;
    impl_->height_ = size.height;
    impl_->dpi_ = size.devicePixelRatio;
    impl_->wic_ = owner.impl_->wic_;
    impl_->write_ = owner.impl_->write_;
    impl_->account_ = owner.impl_->account_;
    impl_->fontCollection_ = owner.impl_->fontCollection_;
    require(target->CreateSolidColorBrush(color(0xffffffff), brush.GetAddressOf()));
    impl_->drawing_ = true;
}

uint64_t Canvas::trackedBytes() const {
    return impl_->stats_.surfaceBytes + impl_->uploadedBytes_ + impl_->stats_.pixelBytes +
           impl_->stats_.legacyBitmapBytes + impl_->stats_.legacySourceBytes + impl_->transientBytes_;
}

void Canvas::trackMemory() {
    impl_->stats_.peakTrackedBytes = std::max(impl_->stats_.peakTrackedBytes, trackedBytes());
}

Canvas::Frame::Frame(Canvas& canvas, const FrameBuffer& output, Rect safe)
    : canvas_(canvas), output_(output), area_(canvas.begin(output, safe)) {}

Canvas::Frame::~Frame() {
    if (active_) {
        canvas_.cancel();
    }
}

Canvas::Frame::Frame(Frame&& other) noexcept
    : canvas_(other.canvas_),
      output_(other.output_),
      area_(other.area_),
      active_(std::exchange(other.active_, false)) {}

Rect Canvas::Frame::area() const {
    return area_;
}

void Canvas::Frame::finish() {
    if (!active_) {
        throw std::logic_error("Canvas frame already finished");
    }
    canvas_.end(output_);
    active_ = false;
}
}  // namespace cybercanvas
