#pragma once

#include <memory>

struct ID3D11Device;
struct ID3D11ShaderResourceView;

namespace liquidglass {
struct Region;

namespace detail {
class GpuTimer;
struct GpuInterval;

// Compares RGB on the GPU and reads only a four-byte flag. This render-thread
// service bounds its fence/readback wait to 50 ms; it never obtains desktop pixels.
class PixelComparison final {
public:
    explicit PixelComparison(ID3D11Device& device);
    ~PixelComparison();
    bool probe(ID3D11ShaderResourceView& current, ID3D11ShaderResourceView* previous, Region area,
               GpuTimer* timer = nullptr, GpuInterval* measured = nullptr);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}  // namespace detail
}  // namespace liquidglass
