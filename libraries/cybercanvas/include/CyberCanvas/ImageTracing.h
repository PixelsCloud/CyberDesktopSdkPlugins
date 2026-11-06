#pragma once

#include <cstdint>
#include <memory>
#include <source_location>

struct ID3D11Texture2D;
struct ID2D1Bitmap;

namespace cybercanvas {
struct ImageTraceDescription {
    uint32_t domain = 0;
    const void* identity = nullptr;
    uint64_t bytes = 0;
    uint32_t width = 0, height = 0, stride = 0, format = 0;
    const char* source = nullptr;
    const char* file = nullptr;
    uint32_t line = 0;
};

// The callback returns an AddRef'd IUnknown. It has process lifetime and may be
// called by decode workers; it must never retain the resource being observed.
using ImageTraceObserver = void* (*)(const ImageTraceDescription&) noexcept;

class ImageTraceLease {
public:
    virtual ~ImageTraceLease();
};

void configureImageTracing(ImageTraceObserver observer) noexcept;
bool imageTracingEnabled() noexcept;
std::unique_ptr<ImageTraceLease> traceImage(const void* pixels, uint64_t bytes, uint32_t width, uint32_t height,
                                            uint32_t stride, const char* source = nullptr,
                                            std::source_location location = std::source_location::current()) noexcept;
void traceImage(ID3D11Texture2D* texture, const char* source = nullptr,
                std::source_location location = std::source_location::current()) noexcept;
void traceImage(ID2D1Bitmap* bitmap, const char* source = nullptr,
                std::source_location location = std::source_location::current()) noexcept;
}  // namespace cybercanvas
