#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "LiquidGlass/Capture.h"

struct ID3D11Device;
struct ID2D1DeviceContext;
struct ID2D1Bitmap1;

namespace liquidglass::detail {
class FrameConsumer final {
public:
    static std::unique_ptr<FrameConsumer> find(uintptr_t root, Region output, std::function<void()> wake);
    ~FrameConsumer();
    void configure(const std::vector<Region>& regions);
    bool take(ID3D11Device& device, ID2D1DeviceContext& context);
    Region bounds() const;
    uint64_t bytes() const;
    std::string error() const;
    ID2D1Bitmap1* bitmap() const;

private:
    struct Impl;
    explicit FrameConsumer(std::unique_ptr<Impl> state);
    std::unique_ptr<Impl> m_impl;
};
}  // namespace liquidglass::detail
