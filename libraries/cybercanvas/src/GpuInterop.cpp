#include <CyberCanvas/GpuInterop.h>
#include <CyberCanvas/MemoryBudget.h>
#include <CyberCanvas/Native.h>
#include <CyberCanvas/ResourceAccount.h>
#include <d2d1_1.h>
#include <d3d11_1.h>
#include <dxgi.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <vector>

namespace cybercanvas {
namespace {
struct DrawingDevice {
    ComPtr<ID3D11Device> device;
    ComPtr<ID2D1Factory1> factory;
    ComPtr<ID2D1Device> d2d;
    ComPtr<ID2D1DeviceContext> commands;
    ComPtr<ID3D11DeviceContext1> immediate;
    ComPtr<ID3DDeviceContextState> drawState, previous;
    bool drawing = false;
    MemoryBudget outputs{512ULL * 1024 * 1024};

    ~DrawingDevice() {
        finish();
    }

    ID2D1DeviceContext* begin(ID2D1Bitmap1* target, ID3D11DeviceContext* context) {
        if (!immediate) {
            require(context->QueryInterface(IID_PPV_ARGS(&immediate)));
            ComPtr<ID3D11Device1> device1;
            require(device.As(&device1));
            const auto level = device->GetFeatureLevel();
            require(device1->CreateDeviceContextState(0, &level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device), nullptr,
                                                      &drawState));
        }
        if (!drawing) {
            immediate->SwapDeviceContextState(drawState.Get(), &previous);
            commands->SetTarget(target);
            commands->BeginDraw();
            drawing = true;
        } else {
            commands->SetTarget(target);
        }
        return commands.Get();
    }

    bool finish() noexcept {
        if (!drawing) {
            return true;
        }
        const auto status = commands->EndDraw();
        commands->SetTarget(nullptr);
        immediate->SwapDeviceContextState(previous.Get(), nullptr);
        previous.Reset();
        drawing = false;
        return SUCCEEDED(status);
    }
};

thread_local std::map<ID3D11Device*, unsigned> canvasBatchDepth;

std::shared_ptr<DrawingDevice> drawingDevice(ID3D11Device* device, bool create = true) {
    // UI-thread devices share the D2D resource domain and glyph cache. Weak
    // entries release removed devices after their last retained widget surface.
    static thread_local std::vector<std::weak_ptr<DrawingDevice>> devices;
    std::erase_if(devices, [](const auto& entry) {
        return entry.expired();
    });
    for (const auto& entry : devices) {
        if (auto value = entry.lock(); value && value->device.Get() == device) {
            return value;
        }
    }
    if (!create) {
        return {};
    }
    auto result = std::make_shared<DrawingDevice>();
    result->device = device;
    ComPtr<IDXGIDevice> dxgi;
    require(device->QueryInterface(IID_PPV_ARGS(&dxgi)));
    require(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, result->factory.GetAddressOf()));
    require(result->factory->CreateDevice(dxgi.Get(), &result->d2d));
    require(result->d2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &result->commands));
    devices.push_back(result);
    return result;
}

}  // namespace

struct GpuBatch::State {
    ComPtr<ID3D11Device> device;
    bool registered = true;
};

GpuBatch::GpuBatch(ID3D11Device* device, ID3D11DeviceContext*) noexcept {
    try {
        state_ = std::make_unique<State>();
        state_->device = device;
        ++canvasBatchDepth[device];
    } catch (...) {
        state_.reset();
    }
}

GpuBatch::~GpuBatch() {
    finish();
}

bool GpuBatch::finish() noexcept {
    if (!state_ || !state_->registered) {
        return true;
    }
    state_->registered = false;
    const auto found = canvasBatchDepth.find(state_->device.Get());
    if (found != canvasBatchDepth.end() && --found->second) {
        return true;
    }
    canvasBatchDepth.erase(state_->device.Get());
    const auto device = drawingDevice(state_->device.Get(), false);
    return !device || device->finish();
}

bool finishDrawingDevice(ID3D11Device* device) noexcept {
    const auto pending = drawingDevice(device, false);
    return !pending || pending->finish();
}

class GpuSurface::Impl {
public:
    void resourceAccount(ResourceAccount account) {
        if (account_.context != account.context || account_.reserve != account.reserve) {
            reset();
        }
        account_ = account;
    }

    bool matches(ID3D11Device* device, uint32_t width, uint32_t height) const {
        return domain_ && domain_->device.Get() == device && width_ == width && height_ == height && view_;
    }

    ID3D11ShaderResourceView* view() const {
        return view_.Get();
    }

    template <typename Paint>
    ID3D11ShaderResourceView* render(ID3D11Device* device, ID3D11DeviceContext* context, uint32_t width,
                                     uint32_t height, double dpi, Paint&& paint) {
        if (!device || !context || !width || !height || width > 4096 || height > 4096 || !std::isfinite(dpi) ||
            dpi < .5 || dpi > 4) {
            return nullptr;
        }
        try {
            if (!matches(device, width, height)) {
                reset();
                domain_ = drawingDevice(device);
                ComPtr<IUnknown> accounting;
                if (account_.reserve) {
                    accounting = account_.acquire(uint64_t(width) * height * 4);
                } else {
                    lease_ = domain_->outputs.reserve(uint64_t(width) * height * 4);
                    accounting = makeMemoryLease(lease_);
                }
                D3D11_TEXTURE2D_DESC description{};
                description.Width = width;
                description.Height = height;
                description.MipLevels = description.ArraySize = description.SampleDesc.Count = 1;
                description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                description.Usage = D3D11_USAGE_DEFAULT;
                description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
                require(device->CreateTexture2D(&description, nullptr, &texture_));
                require(device->CreateShaderResourceView(texture_.Get(), nullptr, &view_));
                ComPtr<IDXGISurface> surface;
                require(texture_.As(&surface));
                const auto properties = D2D1::BitmapProperties1(
                    D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                    D2D1::PixelFormat(description.Format, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
                require(domain_->commands->CreateBitmapFromDxgiSurface(surface.Get(), &properties, &target_));
                retainTextureLease(target_.Get(), accounting.Get());
                width_ = width;
                height_ = height;
            }
            auto* commands = domain_->begin(target_.Get(), context);
            commands->SetUnitMode(D2D1_UNIT_MODE_DIPS);
            commands->SetDpi(float(96 * dpi), float(96 * dpi));
            if (!std::forward<Paint>(paint)(static_cast<ID2D1RenderTarget*>(commands), true)) {
                domain_->finish();
                return nullptr;
            }
            if (!canvasBatchDepth.contains(device) && !domain_->finish()) {
                reset();
                return nullptr;
            }
            return view_.Get();
        } catch (...) {
            if (domain_) {
                domain_->finish();
            }
            reset();
            return nullptr;
        }
    }

    void reset() {
        target_.Reset();
        view_.Reset();
        texture_.Reset();
        lease_.reset();
        domain_.reset();
        width_ = height_ = 0;
    }

private:
    std::shared_ptr<DrawingDevice> domain_;
    ResourceAccount account_;
    std::shared_ptr<MemoryBudget::Lease> lease_;
    ComPtr<ID3D11Texture2D> texture_;
    ComPtr<ID3D11ShaderResourceView> view_;
    ComPtr<ID2D1Bitmap1> target_;
    uint32_t width_ = 0, height_ = 0;
};

void GpuSurface::resourceAccount(ResourceAccount account) {
    return impl_->resourceAccount(account);
}

bool GpuSurface::matches(ID3D11Device* device, uint32_t width, uint32_t height) const {
    return impl_->matches(device, width, height);
}

ID3D11ShaderResourceView* GpuSurface::view() const {
    return impl_->view();
}

ID3D11ShaderResourceView* GpuSurface::renderImpl(ID3D11Device* device, ID3D11DeviceContext* context, uint32_t width,
                                                 uint32_t height, double dpi, void* callbackContext,
                                                 bool (*paint)(void*, ID2D1RenderTarget*, bool)) {
    return impl_->render(device, context, width, height, dpi, [&](ID2D1RenderTarget* target, bool batched) -> bool {
        return paint(callbackContext, target, batched);
    });
}

void GpuSurface::reset() {
    return impl_->reset();
}

GpuSurface::GpuSurface() : impl_(std::make_unique<Impl>()) {}

GpuSurface::~GpuSurface() = default;
}  // namespace cybercanvas
