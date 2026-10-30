#include <CyberCanvas/Effects.h>
#include <CyberCanvas/Image.h>
#include <CyberCanvas/MemoryBudget.h>
#include <CyberCanvas/Native.h>
#include <d2d1_1.h>
#include <d2d1effects.h>
#include <d3d11.h>
#include <dxgi1_2.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>
#include <tuple>
#include <utility>

namespace cybercanvas {
#if defined(CYBERCANVAS_EFFECTS_FAULT_TEST)
// Only compiled into the fault-test executable, never the distributed library.
thread_local bool effectDeviceLossPending = false;
thread_local bool effectFullSurfaceForTest = false;

void injectEffectDeviceLossForTest() {
    effectDeviceLossPending = true;
}

void useFullEffectSurfaceForTest(bool enabled) {
    effectFullSurfaceForTest = enabled;
}
#endif
namespace {
bool same(const EffectStyle& a, const EffectStyle& b) {
    return std::tie(a.kind, a.sigma, a.offset.x, a.offset.y, a.color, a.includeSource) ==
           std::tie(b.kind, b.sigma, b.offset.x, b.offset.y, b.color, b.includeSource);
}

}  // namespace

struct EffectSurface::Impl {
    EffectBackend requested;
    bool smallCpuSurface = false;
    uint64_t maximumBytes;
    std::shared_ptr<MemoryBudget> budget;
    std::shared_ptr<MemoryBudget::Lease> deviceLease;
    std::thread::id owner = std::this_thread::get_id();
    ComPtr<ID3D11Device> d3d;
    ComPtr<ID2D1Factory1> factory;
    ComPtr<ID2D1Device> device;
    ComPtr<ID2D1DeviceContext> context;
    ComPtr<ID2D1Bitmap1> source, target, staging;
    uint32_t width = 0, height = 0, inputWidth = 0, inputHeight = 0;
    uint64_t inputId = 0;
    D2D1_RECT_F contentBounds{};
    std::vector<EffectStyle> styles;
    std::vector<ComPtr<ID2D1Effect>> graph;
    size_t nextEffect = 0;
    EffectResult output;
    EffectStats counters;

    Impl(EffectBackend backend, uint64_t bytes, std::shared_ptr<MemoryBudget> shared)
        : requested(backend),
          maximumBytes(bytes),
          budget(shared ? std::move(shared) : std::make_shared<MemoryBudget>(bytes)) {
        if (static_cast<unsigned>(backend) > 2 || bytes < 16 || bytes > (1ULL << 32)) {
            throw std::invalid_argument("Invalid effect surface options");
        }
    }

    void check() const {
        if (std::this_thread::get_id() != owner) {
            throw std::logic_error("Effect surface belongs to another thread");
        }
    }

    void reset() {
        graph.clear();
        nextEffect = 0;
        source.Reset();
        target.Reset();
        staging.Reset();
        context.Reset();
        device.Reset();
        factory.Reset();
        d3d.Reset();
        inputId = 0;
        inputWidth = inputHeight = width = height = 0;
        output = {};
        styles.clear();
        counters.explicitSurfaceBytes = 0;
        deviceLease.reset();
    }

    void initialize(bool preferSmallCpuSurface) {
        if (context && (requested != EffectBackend::Automatic || preferSmallCpuSurface == smallCpuSurface)) {
            return;
        }
        if (context) {
            reset();
        }
        smallCpuSurface = preferSmallCpuSurface;
        // Small CPU snapshots are dominated by synchronous GPU readback latency.
        // Bound the software preference to one MiB of padded output pixels.
        auto driver =
            requested == EffectBackend::Warp || (requested == EffectBackend::Automatic && preferSmallCpuSurface)
                ? D3D_DRIVER_TYPE_WARP
                : D3D_DRIVER_TYPE_HARDWARE;
        auto result = D3D11CreateDevice(nullptr, driver, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                        D3D11_SDK_VERSION, &d3d, nullptr, nullptr);
        if (FAILED(result) && requested == EffectBackend::Automatic) {
            driver = driver == D3D_DRIVER_TYPE_WARP ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP;
            result = D3D11CreateDevice(nullptr, driver, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                       D3D11_SDK_VERSION, &d3d, nullptr, nullptr);
        }
        require(result);
        if (requested == EffectBackend::Automatic && !preferSmallCpuSurface && driver == D3D_DRIVER_TYPE_HARDWARE) {
            // This API publishes CPU snapshots. On UMA devices, synchronous hardware
            // readback can cost more than the small effect itself; prefer WARP there.
            // An explicit Hardware request still bypasses this policy.
            D3D11_FEATURE_DATA_D3D11_OPTIONS2 options{};
            if (SUCCEEDED(d3d->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS2, &options, sizeof(options))) &&
                options.UnifiedMemoryArchitecture) {
                ComPtr<ID3D11Device> software;
                if (SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                                                D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                                                &software, nullptr, nullptr))) {
                    d3d = std::move(software);
                    driver = D3D_DRIVER_TYPE_WARP;
                }
            }
        }
        counters.backend = driver == D3D_DRIVER_TYPE_WARP ? EffectBackend::Warp : EffectBackend::Hardware;
        ComPtr<IDXGIDevice> dxgi;
        require(d3d.As(&dxgi));
        require(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf()));
        require(factory->CreateDevice(dxgi.Get(), &device));
        require(device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &context));
        context->SetUnitMode(D2D1_UNIT_MODE_PIXELS);
        ++counters.deviceRebuilds;
    }

    ComPtr<ID2D1Effect> effect(REFCLSID id, ID2D1Image* input = nullptr) {
        if (nextEffect == graph.size()) {
            ComPtr<ID2D1Effect> created;
            require(context->CreateEffect(id, &created));
            graph.push_back(std::move(created));
        }
        auto result = graph[nextEffect++];
        if (input) {
            result->SetInput(0, input);
        }
        return result;
    }

    static ComPtr<ID2D1Image> image(ID2D1Effect* effect) {
        ComPtr<ID2D1Image> result;
        effect->GetOutput(&result);
        return result;
    }

    ComPtr<ID2D1Image> composite(ID2D1Image* destination, ID2D1Image* source, D2D1_COMPOSITE_MODE mode) {
        auto result = effect(CLSID_D2D1Composite, destination);
        result->SetInput(1, source);
        require(result->SetValue(D2D1_COMPOSITE_PROP_MODE, mode));
        return image(result.Get());
    }

    ComPtr<ID2D1Image> translated(ID2D1Image* input, Point offset) {
        auto result = effect(CLSID_D2D12DAffineTransform, input);
        require(result->SetValue(D2D1_2DAFFINETRANSFORM_PROP_TRANSFORM_MATRIX,
                                 D2D1::Matrix3x2F::Translation(float(offset.x), float(offset.y))));
        return image(result.Get());
    }

    ComPtr<ID2D1Image> apply(ID2D1Image* original, const EffectStyle& style, double dpi) {
        if (style.kind == EffectKind::Blur) {
            auto blur = effect(CLSID_D2D1GaussianBlur, original);
            require(blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, float(style.sigma * dpi)));
            require(blur->SetValue(D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION, D2D1_GAUSSIANBLUR_OPTIMIZATION_QUALITY));
            return image(blur.Get());
        }
        ComPtr<ID2D1Image> alpha = original;
        if (style.kind == EffectKind::InnerShadow) {
            auto flood = effect(CLSID_D2D1Flood);
            require(flood->SetValue(D2D1_FLOOD_PROP_COLOR, D2D1::Vector4F(0, 0, 0, 1)));
            auto solid = image(flood.Get());
            auto inverse = composite(solid.Get(), original, D2D1_COMPOSITE_MODE_DESTINATION_OUT);
            auto crop = effect(CLSID_D2D1Crop, inverse.Get());
            require(crop->SetValue(D2D1_CROP_PROP_RECT, D2D1::Vector4F(0, 0, float(width), float(height))));
            alpha = image(crop.Get());
        }
        auto shadow = effect(CLSID_D2D1Shadow, alpha.Get());
        require(shadow->SetValue(D2D1_SHADOW_PROP_BLUR_STANDARD_DEVIATION, float(style.sigma * dpi)));
        const auto tint = color(style.color);
        require(shadow->SetValue(D2D1_SHADOW_PROP_COLOR, D2D1::Vector4F(tint.r, tint.g, tint.b, tint.a)));
        require(shadow->SetValue(D2D1_SHADOW_PROP_OPTIMIZATION, D2D1_SHADOW_OPTIMIZATION_QUALITY));
        auto blurred = image(shadow.Get());
        const Point offset =
            style.kind == EffectKind::Glow ? Point{} : Point{style.offset.x * dpi, style.offset.y * dpi};
        auto shifted = translated(blurred.Get(), offset);
        if (style.kind == EffectKind::InnerShadow) {
            auto clipped = composite(original, shifted.Get(), D2D1_COMPOSITE_MODE_SOURCE_IN);
            return style.includeSource ? composite(original, clipped.Get(), D2D1_COMPOSITE_MODE_SOURCE_OVER) : clipped;
        }
        return style.includeSource ? composite(shifted.Get(), original, D2D1_COMPOSITE_MODE_SOURCE_OVER) : shifted;
    }
};

EffectSurface::EffectSurface(EffectBackend backend, uint64_t maximumBytes, std::shared_ptr<MemoryBudget> sharedBudget)
    : impl_(std::make_unique<Impl>(backend, maximumBytes, std::move(sharedBudget))) {}

EffectSurface::~EffectSurface() = default;

EffectResult EffectSurface::render(const ImageData& input, std::span<const EffectStyle> effects, double dpi) {
    impl_->check();
    if (!std::isfinite(dpi) || dpi < .5 || dpi > 4 || effects.size() > 8) {
        throw std::invalid_argument("Invalid effect DPI or chain length");
    }
    double pad = 0;
    for (const auto& style : effects) {
        if (static_cast<unsigned>(style.kind) > 3 || !std::isfinite(style.sigma) || style.sigma < 0 ||
            style.sigma > 64 || !std::isfinite(style.offset.x) || !std::isfinite(style.offset.y) ||
            std::abs(style.offset.x) > 256 || std::abs(style.offset.y) > 256) {
            throw std::invalid_argument("Invalid effect parameters");
        }
        // Inner shadows need an outer inverse-alpha margin even though their
        // visible output is clipped to the original image.
        pad += std::ceil((style.sigma * 3 + std::max(std::abs(style.offset.x), std::abs(style.offset.y))) * dpi);
    }
    const auto padding = uint32_t(pad);
    const uint32_t width = input.width() + padding * 2, height = input.height() + padding * 2;
    const uint64_t bytes = uint64_t(width) * height * 4 * 3 + uint64_t(input.width()) * input.height() * 4;
    if (width > 4096 || height > 4096 || bytes > impl_->maximumBytes) {
        throw std::length_error("Effect surface budget exceeded");
    }
    if (impl_->output.image && input.id() == impl_->inputId && dpi == impl_->output.devicePixelRatio &&
        effects.size() == impl_->styles.size() &&
        std::equal(effects.begin(), effects.end(), impl_->styles.begin(), same)) {
        ++impl_->counters.cacheHits;
        return impl_->output;
    }
    try {
        impl_->initialize(uint64_t(width) * height <= 512ULL * 512);
        impl_->output = {};
        const bool sameTopology = effects.size() == impl_->styles.size() &&
                                  std::equal(effects.begin(), effects.end(), impl_->styles.begin(),
                                             [](const EffectStyle& a, const EffectStyle& b) {
                                                 return a.kind == b.kind && a.includeSource == b.includeSource;
                                             });
        if (!sameTopology) {
            impl_->graph.clear();
        }
        impl_->nextEffect = 0;
        if (width != impl_->width || height != impl_->height || input.width() != impl_->inputWidth ||
            input.height() != impl_->inputHeight) {
            impl_->graph.clear();
            impl_->context->SetTarget(nullptr);
            impl_->source.Reset();
            impl_->inputId = 0;
            impl_->target.Reset();
            impl_->staging.Reset();
            impl_->width = impl_->height = impl_->inputWidth = impl_->inputHeight = 0;
            impl_->deviceLease.reset();
            impl_->deviceLease = impl_->budget->reserve(bytes - uint64_t(width) * height * 4);
        }
        auto pixelLease = impl_->budget->reserve(uint64_t(width) * height * 4);
        const auto format = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
        if (input.width() != impl_->inputWidth || input.height() != impl_->inputHeight) {
            impl_->source.Reset();
            auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE, format);
            require(impl_->context->CreateBitmap(D2D1::SizeU(input.width(), input.height()), nullptr, 0, properties,
                                                 &impl_->source));
            impl_->inputWidth = input.width();
            impl_->inputHeight = input.height();
        }
        if (width != impl_->width || height != impl_->height) {
            impl_->context->SetTarget(nullptr);
            impl_->target.Reset();
            impl_->staging.Reset();
            auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, format);
            require(impl_->context->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0, properties, &impl_->target));
            properties.bitmapOptions = D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
            require(impl_->context->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0, properties, &impl_->staging));
            impl_->width = width;
            impl_->height = height;
        }
        if (input.id() != impl_->inputId) {
            uint32_t left = input.width(), top = input.height(), right = 0, bottom = 0;
            for (uint32_t y = 0; y < input.height(); ++y) {
                const auto* row = input.pixels().data() + size_t(y) * input.stride();
                for (uint32_t x = 0; x < input.width(); ++x) {
                    if (row[size_t(x) * 4 + 3]) {
                        left = std::min(left, x);
                        top = std::min(top, y);
                        right = std::max(right, x + 1);
                        bottom = std::max(bottom, y + 1);
                    }
                }
            }
            impl_->contentBounds = {float(left), float(top), float(right), float(bottom)};
            require(impl_->source->CopyFromMemory(nullptr, input.pixels().data(), input.stride()));
        }
        auto result = impl_->translated(impl_->source.Get(), {double(padding), double(padding)});
        for (const auto& effect : effects) {
            result = impl_->apply(result.Get(), effect, dpi);
        }
        impl_->context->SetTarget(impl_->target.Get());
        impl_->context->BeginDraw();
        impl_->context->Clear(D2D1::ColorF(0, 0));
        auto bounds = impl_->contentBounds;
#if defined(CYBERCANVAS_EFFECTS_FAULT_TEST)
        if (effectFullSurfaceForTest) {
            bounds = {0, 0, float(input.width()), float(input.height())};
        }
#endif
        if (bounds.right > bounds.left && bounds.bottom > bounds.top) {
            // Keep the original effect input and kernel. Limit only output work,
            // conservatively including 3*sigma, offsets and a rasterization margin.
            // Input is translated by padding, and effect support expands by padding.
            const float margin = float(effects.size() * 2);
            impl_->context->PushAxisAlignedClip(
                D2D1::RectF(std::max(0.f, bounds.left - margin), std::max(0.f, bounds.top - margin),
                            std::min(float(width), bounds.right + float(padding * 2) + margin),
                            std::min(float(height), bounds.bottom + float(padding * 2) + margin)),
                D2D1_ANTIALIAS_MODE_ALIASED);
            impl_->context->DrawImage(result.Get());
            impl_->context->PopAxisAlignedClip();
        }
        require(impl_->context->EndDraw());
#if defined(CYBERCANVAS_EFFECTS_FAULT_TEST)
        if (std::exchange(effectDeviceLossPending, false)) {
            require(DXGI_ERROR_DEVICE_REMOVED);
        }
#endif
        require(impl_->staging->CopyFromBitmap(nullptr, impl_->target.Get(), nullptr));
        std::vector<uint8_t> pixels(size_t(width) * height * 4);
        D2D1_MAPPED_RECT mapped{};
        require(impl_->staging->Map(D2D1_MAP_OPTIONS_READ, &mapped));
        for (uint32_t y = 0; y < height; ++y) {
            std::memcpy(pixels.data() + size_t(y) * width * 4, mapped.bits + size_t(y) * mapped.pitch, width * 4);
        }
        require(impl_->staging->Unmap());
        impl_->output = {MemoryBudget::attach(ImageData::fromPremultipliedBgra(width, height, std::move(pixels)),
                                              std::move(pixelLease)),
                         {-double(padding) / dpi, -double(padding) / dpi},
                         dpi};
        impl_->inputId = input.id();
        impl_->styles.assign(effects.begin(), effects.end());
        impl_->counters.explicitSurfaceBytes = bytes;
        ++impl_->counters.renders;
        return impl_->output;
    } catch (...) {
        impl_->reset();
        throw;
    }
}

EffectStats EffectSurface::stats() const {
    impl_->check();
    return impl_->counters;
}

void EffectSurface::reset() {
    impl_->check();
    impl_->reset();
}
}  // namespace cybercanvas
