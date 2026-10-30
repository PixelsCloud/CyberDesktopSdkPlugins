#pragma once
#include <memory>
#include <utility>

#include "CyberCanvas/ResourceAccount.h"
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;

namespace cybercanvas {
// Finish pending Canvas commands before entering an external D3D pass.
bool finishDrawingDevice(ID3D11Device* device) noexcept;

class GpuBatch final {
public:
    GpuBatch(ID3D11Device* device, ID3D11DeviceContext* context) noexcept;
    ~GpuBatch();
    bool finish() noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

class GpuSurface {
public:
    GpuSurface();

    ~GpuSurface();

    void resourceAccount(ResourceAccount account);

    bool matches(ID3D11Device* device, uint32_t width, uint32_t height) const;

    ID3D11ShaderResourceView* view() const;

    template <typename Paint>
    ID3D11ShaderResourceView* render(ID3D11Device* device, ID3D11DeviceContext* context, uint32_t width,
                                     uint32_t height, double dpi, Paint&& paint) {
        auto callback = [&](ID2D1RenderTarget* target, bool batched) -> bool {
            return std::forward<Paint>(paint)(target, batched);
        };
        return renderImpl(device, context, width, height, dpi, &callback,
                          [](void* callbackContext, ID2D1RenderTarget* target, bool batched) -> bool {
                              return (*static_cast<decltype(callback)*>(callbackContext))(target, batched);
                          });
    }

    void reset();

    GpuSurface(const GpuSurface&) = delete;

    GpuSurface& operator=(const GpuSurface&) = delete;

private:
    ID3D11ShaderResourceView* renderImpl(ID3D11Device* device, ID3D11DeviceContext* context, uint32_t width,
                                         uint32_t height, double dpi, void* callbackContext,
                                         bool (*paint)(void*, ID2D1RenderTarget*, bool));
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace cybercanvas
