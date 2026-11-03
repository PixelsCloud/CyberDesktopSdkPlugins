#include <Windows.h>
#include <d2d1_1.h>
#include <d3d11_1.h>
#include <winrt/base.h>
#include <wrl/client.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <stdexcept>
#include <utility>

#include "LiquidGlass/CompositionMaterial.h"

namespace liquidglass {
namespace {
using Microsoft::WRL::ComPtr;

class ContextScope final {
public:
    ContextScope(ID3D11DeviceContext1& context, ComPtr<ID3DDeviceContextState>& state)
        : m_context(context), m_state(state) {
        m_context.SwapDeviceContextState(m_state.Get(), &m_previous);
    }

    ~ContextScope() {
        m_context.SwapDeviceContextState(m_previous.Get(), m_state.ReleaseAndGetAddressOf());
    }

    ContextScope(const ContextScope&) = delete;
    ContextScope& operator=(const ContextScope&) = delete;

private:
    ID3D11DeviceContext1& m_context;
    ComPtr<ID3DDeviceContextState>& m_state;
    ComPtr<ID3DDeviceContextState> m_previous;
};

CompositionRegion samplingRegion(CompositionRegion surface, const D3D11_TEXTURE2D_DESC& source,
                                 const Parameters& parameters, float dpi) {
    const int64_t right = int64_t(surface.x) + surface.width;
    const int64_t bottom = int64_t(surface.y) + surface.height;
    if (surface.width < 1 || surface.height < 1 || surface.width > 16384 || surface.height > 16384 || right <= 0 ||
        bottom <= 0 || surface.x >= int64_t(source.Width) || surface.y >= int64_t(source.Height) ||
        source.Width > INT_MAX || source.Height > INT_MAX || source.SampleDesc.Count != 1 || source.MipLevels != 1 ||
        source.ArraySize != 1) {
        throw std::invalid_argument("Composition surface must intersect a single-sample 2D background texture");
    }
    const double expansion = opticalExpansion(parameters, dpi);
    if (!std::isfinite(expansion) || expansion > INT_MAX) {
        throw std::invalid_argument("Composition sampling expansion is invalid");
    }
    const int64_t pad = static_cast<int64_t>(expansion);
    const auto left = std::max(int64_t(0), int64_t(surface.x) - pad);
    const auto top = std::max(int64_t(0), int64_t(surface.y) - pad);
    const auto clippedRight = std::min(int64_t(source.Width), right + pad);
    const auto clippedBottom = std::min(int64_t(source.Height), bottom + pad);
    return {int(left), int(top), int(clippedRight - left), int(clippedBottom - top)};
}

bool sameGeometry(const EffectGeometry& first, const EffectGeometry& second) {
    return first.width == second.width && first.height == second.height && first.sourceX == second.sourceX &&
           first.sourceY == second.sourceY && first.dpiScale == second.dpiScale && first.corner == second.corner &&
           first.attachedTop == second.attachedTop;
}

uint64_t pixelBytes(DXGI_FORMAT format) {
    switch (format) {
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM:
            return 4;
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
            return 8;
        default:
            throw std::invalid_argument("Unsupported composition background format");
    }
}
}  // namespace

float opticalExpansion(const Parameters& parameters, float dpiScale) {
    const double scale = std::max({1.0, double(parameters.scaleX), double(parameters.scaleY)});
    const double dispersion = std::max(1.0, .5 * (1 + .1 * parameters.aberrationIntensity));
    return float(
        std::ceil((3.0 * parameters.blurSigma() + std::abs(double(parameters.displacementScale)) * dispersion + 4) *
                  dpiScale * scale));
}

struct CompositionMaterial::Impl {
    ComPtr<ID3D11Device1> device;
    ComPtr<ID3D11DeviceContext1> context;
    ComPtr<ID3DDeviceContextState> state;
    ComPtr<ID2D1DeviceContext> drawing;
    ComPtr<ID3D11Texture2D> source;
    ComPtr<IUnknown> backgroundIdentity;
    std::unique_ptr<Effect> effect;
    std::unique_ptr<EffectTarget> target;
    CompositionRegion sourceBounds;
    EffectGeometry lastGeometry;
    Parameters lastParameters;
    CompositionMetrics counters;
    DXGI_FORMAT sourceFormat = DXGI_FORMAT_UNKNOWN;
    uint64_t lastRevision = 0, materialRevision = 0;
    bool valid = false, metricsEnabled = false;
};

CompositionMaterial::CompositionMaterial() : m_impl(std::make_unique<Impl>()) {}

CompositionMaterial::~CompositionMaterial() = default;

void CompositionMaterial::initialize(void* d3dDevice, void* d2dDevice) {
    if (!d3dDevice || !d2dDevice) {
        throw std::invalid_argument("Composition material needs D3D and D2D devices");
    }
    auto candidate = std::make_unique<Impl>();
    auto& value = *candidate;
    winrt::check_hresult(static_cast<ID3D11Device*>(d3dDevice)->QueryInterface(IID_PPV_ARGS(&value.device)));
    value.device->GetImmediateContext1(&value.context);
    const auto level = value.device->GetFeatureLevel();
    const UINT flags = value.device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED
                           ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED
                           : 0;
    D3D_FEATURE_LEVEL selected{};
    winrt::check_hresult(value.device->CreateDeviceContextState(flags, &level, 1, D3D11_SDK_VERSION,
                                                                __uuidof(ID3D11Device1), &selected, &value.state));
    winrt::check_hresult(
        static_cast<ID2D1Device*>(d2dDevice)->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &value.drawing));
    ContextScope scope(*value.context.Get(), value.state);
    value.effect = std::make_unique<Effect>();
    value.target = std::make_unique<EffectTarget>();
    value.effect->initialize(value.device.Get(), value.drawing.Get());
    m_impl = std::move(candidate);
}

void CompositionMaterial::render(void* backgroundTexture, CompositionRegion surface, EffectGeometry geometry,
                                 Parameters parameters, uint64_t backgroundRevision,
                                 std::span<const CompositionRegion> updatedBackground) {
    auto& value = *m_impl;
    if (!value.effect || !backgroundTexture) {
        throw std::invalid_argument("Composition material must be initialized with a background texture");
    }
    if (geometry.width != surface.width || geometry.height != surface.height || !std::isfinite(geometry.dpiScale) ||
        geometry.dpiScale < .25f || geometry.dpiScale > 8.f) {
        throw std::invalid_argument("Composition material geometry must match the surface in physical pixels");
    }
    parameters.normalize();
    auto& background = *static_cast<ID3D11Texture2D*>(backgroundTexture);
    ComPtr<ID3D11Device> owner;
    background.GetDevice(&owner);
    ComPtr<IUnknown> ownerIdentity, deviceIdentity, backgroundIdentity;
    winrt::check_hresult(owner.As(&ownerIdentity));
    winrt::check_hresult(value.device.As(&deviceIdentity));
    winrt::check_hresult(background.QueryInterface(IID_PPV_ARGS(&backgroundIdentity)));
    if (ownerIdentity.Get() != deviceIdentity.Get()) {
        throw std::invalid_argument("Composition background must belong to the compositor device");
    }
    D3D11_TEXTURE2D_DESC description{};
    background.GetDesc(&description);
    const auto bytes = pixelBytes(description.Format);
    const auto bounds = samplingRegion(surface, description, parameters, geometry.dpiScale);
    geometry.sourceX = float(surface.x - bounds.x);
    geometry.sourceY = float(surface.y - bounds.y);
    const bool resized = !value.source || value.sourceBounds.width != bounds.width ||
                         value.sourceBounds.height != bounds.height || value.sourceFormat != description.Format;
    const bool copy = !value.valid || resized || value.sourceBounds != bounds ||
                      value.backgroundIdentity.Get() != backgroundIdentity.Get() ||
                      value.lastRevision != backgroundRevision;
    if (!copy && sameGeometry(value.lastGeometry, geometry) && value.lastParameters == parameters) {
        return;
    }
    value.valid = false;
    ContextScope scope(*value.context.Get(), value.state);
    if (resized) {
        D3D11_TEXTURE2D_DESC scratch{};
        scratch.Width = UINT(bounds.width);
        scratch.Height = UINT(bounds.height);
        scratch.MipLevels = scratch.ArraySize = scratch.SampleDesc.Count = 1;
        scratch.Format = description.Format;
        scratch.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> source;
        winrt::check_hresult(value.device->CreateTexture2D(&scratch, nullptr, &source));
        value.source = std::move(source);
        value.sourceFormat = description.Format;
        value.counters.sourceAllocatedBytes = uint64_t(bounds.width) * bounds.height * bytes;
        if (!updatedBackground.empty()) {
            ComPtr<ID3D11RenderTargetView> clearView;
            winrt::check_hresult(value.device->CreateRenderTargetView(value.source.Get(), nullptr, &clearView));
            constexpr float transparent[] = {0, 0, 0, 0};
            value.context->ClearRenderTargetView(clearView.Get(), transparent);
        }
    }
    if (copy) {
        const auto copyRegion = [&](CompositionRegion region) {
            const int64_t left = std::max(int64_t(bounds.x), int64_t(region.x));
            const int64_t top = std::max(int64_t(bounds.y), int64_t(region.y));
            const int64_t right = std::min(int64_t(bounds.x) + bounds.width, int64_t(region.x) + region.width);
            const int64_t bottom = std::min(int64_t(bounds.y) + bounds.height, int64_t(region.y) + region.height);
            if (region.width < 1 || region.height < 1 || right <= left || bottom <= top) {
                return;
            }
            const D3D11_BOX area{UINT(left), UINT(top), 0, UINT(right), UINT(bottom), 1};
            value.context->CopySubresourceRegion(value.source.Get(), 0, UINT(left - bounds.x), UINT(top - bounds.y), 0,
                                                 &background, 0, &area);
            if (value.metricsEnabled) {
                ++value.counters.sourceCopies;
                value.counters.sourceCopyBytes += uint64_t(right - left) * (bottom - top) * bytes;
            }
        };
        if (updatedBackground.empty()) {
            copyRegion(bounds);
        } else {
            for (const auto& region : updatedBackground) {
                copyRegion(region);
            }
        }
        ++value.materialRevision;
    }
    value.effect->render(*value.target, value.source.Get(), geometry, parameters, value.materialRevision, false,
                         react::LightBackgroundBlend::Adaptive);
    if (value.metricsEnabled) {
        ++value.counters.materialRenders;
        value.counters.assetReadbackBytes += value.target->assetReadbackBytes();
    }
    value.counters.materialAllocatedBytes = value.target->allocatedBytes();
    value.sourceBounds = bounds;
    value.backgroundIdentity = std::move(backgroundIdentity);
    value.lastRevision = backgroundRevision;
    value.lastGeometry = geometry;
    value.lastParameters = parameters;
    value.valid = true;
}

void CompositionMaterial::setMetricsEnabled(bool enabled) {
    auto& value = *m_impl;
    if (enabled && !value.metricsEnabled) {
        value.counters.sourceCopies = value.counters.sourceCopyBytes = value.counters.materialRenders =
            value.counters.assetReadbackBytes = 0;
    }
    value.metricsEnabled = enabled;
}

void* CompositionMaterial::bitmap() const {
    return m_impl->valid ? m_impl->target->bitmap() : nullptr;
}

void* CompositionMaterial::texture() const {
    return m_impl->valid ? m_impl->target->texture() : nullptr;
}

CompositionRegion CompositionMaterial::sourceRegion() const {
    return m_impl->sourceBounds;
}

CompositionMetrics CompositionMaterial::metrics() const {
    return m_impl->counters;
}

}  // namespace liquidglass
