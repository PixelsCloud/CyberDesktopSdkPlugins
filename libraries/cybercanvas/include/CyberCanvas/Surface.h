#pragma once
#include <memory>
#include <utility>

#include "CyberCanvas/Native.h"
#include "CyberCanvas/ResourceAccount.h"
#include "CyberCanvas/Types.h"

namespace cybercanvas {
// Owner-thread geometry layers. Each stack belongs to one render target;
// compatible targets must use their own stack. Callbacks must not EndDraw.
class GeometryMasks {
public:
    GeometryMasks();

    ~GeometryMasks();

    GeometryMasks(const GeometryMasks&) = delete;

    GeometryMasks& operator=(const GeometryMasks&) = delete;

    template <typename Paint>
    void with(ID2D1RenderTarget* target, ID2D1Geometry* geometry, Paint&& paint,
              D2D1_RECT_F bounds = D2D1::InfiniteRect()) {
        auto callback = [&]() -> void {
            return std::forward<Paint>(paint)();
        };
        return withImpl(
            target, geometry, &callback,
            [](void* callbackContext) -> void {
                return (*static_cast<decltype(callback)*>(callbackContext))();
            },
            bounds);
    }

    void reset();

private:
    void withImpl(ID2D1RenderTarget* target, ID2D1Geometry* geometry, void* callbackContext, void (*paint)(void*),
                  D2D1_RECT_F bounds = D2D1::InfiniteRect());
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct SurfaceSize {
    uint32_t width = 0, height = 0;
    double devicePixelRatio = 1;
};

// A retained image keeps the surface's accounting lease as well as its COM
// bitmap. This also covers WIC images, which have no underlying DXGI texture.
class SurfaceImage : public ComPtr<ID2D1Bitmap> {
    using Base = ComPtr<ID2D1Bitmap>;

public:
    SurfaceImage() = default;

    SurfaceImage(ID2D1Bitmap* bitmap);

    SurfaceImage(ID2D1Bitmap* bitmap, IUnknown* lease);

    SurfaceImage(const SurfaceImage&) = default;
    SurfaceImage(SurfaceImage&&) = default;
    SurfaceImage& operator=(const SurfaceImage&) = default;
    SurfaceImage& operator=(SurfaceImage&&) = default;

    SurfaceImage& operator=(ID2D1Bitmap* bitmap);

    ULONG Reset();

private:
    ComPtr<IUnknown> lease_;
};

// A reusable transparent BGRA surface, measured in physical pixels. It keeps
// its last successful bitmap until invalidated/rendered/reset. Keep it inside
// the owning Canvas lifetime; no primary Canvas handle is replaced.
class OffscreenSurface {
public:
    OffscreenSurface();

    ~OffscreenSurface();

    OffscreenSurface(const OffscreenSurface&) = delete;

    OffscreenSurface& operator=(const OffscreenSurface&) = delete;

    template <typename Paint>
    void render(ID2D1RenderTarget* owner, SurfaceSize size, Paint&& paint, ResourceAccount account = {}) {
        auto callback = [&](ID2D1BitmapRenderTarget* target) -> void {
            return std::forward<Paint>(paint)(target);
        };
        return renderImpl(
            owner, size, &callback,
            [](void* callbackContext, ID2D1BitmapRenderTarget* target) -> void {
                return (*static_cast<decltype(callback)*>(callbackContext))(target);
            },
            account);
    }

    template <typename Paint>
    void masked(ID2D1Geometry* geometry, Paint&& paint, D2D1_RECT_F bounds = D2D1::InfiniteRect()) {
        auto callback = [&]() -> void {
            return std::forward<Paint>(paint)();
        };
        return maskedImpl(
            geometry, &callback,
            [](void* callbackContext) -> void {
                return (*static_cast<decltype(callback)*>(callbackContext))();
            },
            bounds);
    }

    ID2D1Bitmap* bitmap() const;

    bool matches(ID2D1RenderTarget* owner, SurfaceSize size) const;

    SurfaceImage snapshot() const;

    Size logicalSize() const;

    void invalidate();

    void reset();

private:
    void renderImpl(ID2D1RenderTarget* owner, SurfaceSize size, void* callbackContext,
                    void (*paint)(void*, ID2D1BitmapRenderTarget*), ResourceAccount account = {});
    void maskedImpl(ID2D1Geometry* geometry, void* callbackContext, void (*paint)(void*),
                    D2D1_RECT_F bounds = D2D1::InfiniteRect());
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace cybercanvas
