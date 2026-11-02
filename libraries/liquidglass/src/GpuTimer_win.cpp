#include "libraries/liquidglass/src/GpuTimer_win.h"

#include <Windows.h>
#include <d3d11.h>
#include <winrt/base.h>
#include <wrl/client.h>

#include <chrono>
#include <thread>

namespace liquidglass::detail {
using Microsoft::WRL::ComPtr;

struct GpuTimer::Impl {
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Query> disjoint, start, finish;

    explicit Impl(ID3D11Device& device) {
        device.GetImmediateContext(&context);
        D3D11_QUERY_DESC description{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
        winrt::check_hresult(device.CreateQuery(&description, &disjoint));
        description.Query = D3D11_QUERY_TIMESTAMP;
        winrt::check_hresult(device.CreateQuery(&description, &start));
        winrt::check_hresult(device.CreateQuery(&description, &finish));
    }
};

GpuTimer::GpuTimer(void* device) : m_impl(std::make_unique<Impl>(*static_cast<ID3D11Device*>(device))) {}

GpuTimer::~GpuTimer() = default;

void GpuTimer::begin() {
    m_impl->context->Begin(m_impl->disjoint.Get());
    m_impl->context->End(m_impl->start.Get());
}

void GpuTimer::seal() {
    auto& self = *m_impl;
    self.context->End(self.finish.Get());
    self.context->End(self.disjoint.Get());
}

GpuInterval GpuTimer::end() {
    seal();
    return resolve();
}

GpuInterval GpuTimer::resolve() {
    auto& self = *m_impl;
    self.context->Flush();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
    do {
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT information{};
        UINT64 start = 0, finish = 0;
        const auto ready = self.context->GetData(self.disjoint.Get(), &information, sizeof(information),
                                                 D3D11_ASYNC_GETDATA_DONOTFLUSH);
        winrt::check_hresult(ready);
        if (ready == S_OK) {
            if (information.Disjoint || !information.Frequency) {
                return {0, false, true, false};
            }
            const auto first =
                self.context->GetData(self.start.Get(), &start, sizeof(start), D3D11_ASYNC_GETDATA_DONOTFLUSH);
            const auto last =
                self.context->GetData(self.finish.Get(), &finish, sizeof(finish), D3D11_ASYNC_GETDATA_DONOTFLUSH);
            winrt::check_hresult(first);
            winrt::check_hresult(last);
            if (first == S_OK && last == S_OK) {
                if (finish < start) {
                    return {0, false, true, false};
                }
                const auto elapsed = uint64_t(double(finish - start) * 1e9 / double(information.Frequency));
                return {elapsed, true, false, false};
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    return {0, false, false, true};
}
}  // namespace liquidglass::detail
