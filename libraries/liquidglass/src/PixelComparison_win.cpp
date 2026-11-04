#include "libraries/liquidglass/src/PixelComparison_win.h"

#include <Windows.h>
#include <d3d11_4.h>
#include <d3dcompiler.h>
#include <winrt/base.h>
#include <wrl/client.h>

#include <array>
#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>

#include "LiquidGlass/Capture.h"
#include "libraries/liquidglass/src/GpuTimer_win.h"

namespace liquidglass::detail {
using Microsoft::WRL::ComPtr;
constexpr char kCompareShader[] = R"(
cbuffer Region : register(b0) {
    uint2 Origin; uint2 Extent;

};
Texture2D<float4> Current : register(t0);
Texture2D<float4> Previous : register(t1);
RWByteAddressBuffer Difference : register(u0);
[numthreads(8, 8, 1)]
void Compare(uint3 threadId : SV_DispatchThreadID) {
    uint2 start = threadId.xy * 4;
    if (any(start >= Extent)) return;
    // Only one bit is needed. A thread examines a 4x4 tile and publishes at
    // most one atomic, instead of serializing every changed desktop pixel on
    // the same four-byte address. Unchanged frames still examine every pixel.
    [unroll] for (uint y = 0; y < 4; ++y) {
        [unroll] for (uint x = 0; x < 4; ++x) {
            uint2 pixel = start + uint2(x, y);
            if (any(pixel >= Extent)) continue;
            const float3 current = Current.Load(int3(pixel + Origin, 0)).rgb;
            const float3 previous = Previous.Load(int3(pixel, 0)).rgb;
            if (any(abs(current - previous) > 0.1 / 255.0)) {
                uint ignored;
                Difference.InterlockedOr(0, 1, ignored);
                return;
            }
        }
    }
}
)";

// Dirty rectangles can include excluded visuals whose captured pixels stayed
// unchanged. Only a four-byte change flag is read back, on this worker.
struct PixelComparison::Impl {
public:
    explicit Impl(ID3D11Device& device) {
        device.GetImmediateContext(&m_context);
        ComPtr<ID3D11Device5> modern;
        if (SUCCEEDED(device.QueryInterface(IID_PPV_ARGS(&modern))) && SUCCEEDED(m_context.As(&m_context4)) &&
            SUCCEEDED(modern->CreateFence(0, D3D11_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)))) {
            m_completed.attach(CreateEventW(nullptr, FALSE, FALSE, nullptr));
            winrt::check_bool(bool(m_completed));
        }
        ComPtr<ID3DBlob> shader, error;
        const auto compiled =
            D3DCompile(kCompareShader, sizeof(kCompareShader) - 1, "LiquidGlass.Compare", nullptr, nullptr, "Compare",
                       "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &shader, &error);
        if (FAILED(compiled)) {
            const auto detail =
                error ? std::string(static_cast<const char*>(error->GetBufferPointer()), error->GetBufferSize())
                      : "GPU comparison shader compilation failed";
            throw winrt::hresult_error(compiled, winrt::to_hstring(detail));
        }
        winrt::check_hresult(
            device.CreateComputeShader(shader->GetBufferPointer(), shader->GetBufferSize(), nullptr, &m_shader));
        D3D11_BUFFER_DESC description{};
        description.ByteWidth = 16;
        description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        winrt::check_hresult(device.CreateBuffer(&description, nullptr, &m_constants));
        description.ByteWidth = 4;
        description.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        description.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
        winrt::check_hresult(device.CreateBuffer(&description, nullptr, &m_flag));
        D3D11_UNORDERED_ACCESS_VIEW_DESC view{};
        view.Format = DXGI_FORMAT_R32_TYPELESS;
        view.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        view.Buffer.NumElements = 1;
        view.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
        winrt::check_hresult(device.CreateUnorderedAccessView(m_flag.Get(), &view, &m_view));
        description.BindFlags = description.MiscFlags = 0;
        description.Usage = D3D11_USAGE_STAGING;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        winrt::check_hresult(device.CreateBuffer(&description, nullptr, &m_readback));
    }

    bool probe(ID3D11ShaderResourceView& current, ID3D11ShaderResourceView* previous, Region area,
               detail::GpuTimer* timer, detail::GpuInterval* measured) {
        if (timer) {
            timer->begin();
        }
        const std::array<UINT, 4> constants{UINT(area.x), UINT(area.y), UINT(area.width), UINT(area.height)};
        m_context->UpdateSubresource(m_constants.Get(), 0, nullptr, constants.data(), 0, 0);
        const UINT clear[4]{};
        m_context->ClearUnorderedAccessViewUint(m_view.Get(), clear);
        ID3D11Buffer* buffers[] = {m_constants.Get()};
        ID3D11ShaderResourceView* inputs[] = {&current, previous};
        ID3D11UnorderedAccessView* outputs[] = {m_view.Get()};
        m_context->CSSetShader(m_shader.Get(), nullptr, 0);
        m_context->CSSetConstantBuffers(0, 1, buffers);
        m_context->CSSetShaderResources(0, 2, inputs);
        m_context->CSSetUnorderedAccessViews(0, 1, outputs, nullptr);
        m_context->Dispatch((UINT(area.width) + 31) / 32, (UINT(area.height) + 31) / 32, 1);
        ID3D11ShaderResourceView* emptyInputs[2]{};
        ID3D11UnorderedAccessView* emptyOutputs[1]{};
        m_context->CSSetShaderResources(0, 2, emptyInputs);
        m_context->CSSetUnorderedAccessViews(0, 1, emptyOutputs, nullptr);
        m_context->CSSetShader(nullptr, nullptr, 0);
        m_context->CopyResource(m_readback.Get(), m_flag.Get());
        if (timer) {
            timer->seal();
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
        if (m_fence) {
            winrt::check_hresult(m_context4->Signal(m_fence.Get(), ++m_fenceValue));
            winrt::check_hresult(m_fence->SetEventOnCompletion(m_fenceValue, m_completed.get()));
        }
        m_context->Flush();
        if (m_fence) {
            const auto result = WaitForSingleObject(m_completed.get(), 50);
            if (result == WAIT_FAILED) {
                winrt::throw_last_error();
            }
            if (result != WAIT_OBJECT_0) {
                throw std::runtime_error("GPU change comparison exceeded its 50 ms readback deadline");
            }
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        while (true) {
            const auto result =
                m_context->Map(m_readback.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
            if (result != DXGI_ERROR_WAS_STILL_DRAWING) {
                winrt::check_hresult(result);
                break;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                throw std::runtime_error("GPU change comparison exceeded its 50 ms readback deadline");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const UINT flags = *static_cast<const UINT*>(mapped.pData);
        m_context->Unmap(m_readback.Get(), 0);
        if (timer && measured) {
            *measured = timer->resolve();
        }
        return bool(flags & 1);
    }

private:
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<ID3D11DeviceContext4> m_context4;
    ComPtr<ID3D11Fence> m_fence;
    winrt::handle m_completed;
    uint64_t m_fenceValue = 0;
    ComPtr<ID3D11ComputeShader> m_shader;
    ComPtr<ID3D11Buffer> m_constants, m_flag, m_readback;
    ComPtr<ID3D11UnorderedAccessView> m_view;
};

PixelComparison::PixelComparison(ID3D11Device& device) : m_impl(std::make_unique<Impl>(device)) {}

PixelComparison::~PixelComparison() = default;

bool PixelComparison::probe(ID3D11ShaderResourceView& current, ID3D11ShaderResourceView* previous, Region area,
                            GpuTimer* timer, GpuInterval* measured) {
    return m_impl->probe(current, previous, area, timer, measured);
}
}  // namespace liquidglass::detail
