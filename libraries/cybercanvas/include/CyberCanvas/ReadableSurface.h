#pragma once

#include <memory>
#include <utility>

#include "CyberCanvas/Surface.h"

namespace cybercanvas {
class ImageData;

// Owner-thread software raster surface. Published CPU snapshots remain immutable.
class ReadableSurface {
public:
    ReadableSurface();
    ~ReadableSurface();
    ReadableSurface(const ReadableSurface&) = delete;
    ReadableSurface& operator=(const ReadableSurface&) = delete;

    template <typename Paint>
    void render(ID2D1Factory* factory, IWICImagingFactory* wic, SurfaceSize size, Paint&& paint,
                ResourceAccount account = {}) {
        auto callback = [&](ID2D1RenderTarget* target) {
            std::forward<Paint>(paint)(target);
        };
        renderImpl(
            factory, wic, size, &callback,
            [](void* context, ID2D1RenderTarget* target) {
                (*static_cast<decltype(callback)*>(context))(target);
            },
            account);
    }

    template <typename Paint>
    void clipped(Rect area, Paint&& paint) {
        auto callback = [&]() {
            std::forward<Paint>(paint)();
        };
        clippedImpl(area, &callback, [](void* context) {
            (*static_cast<decltype(callback)*>(context))();
        });
    }

    std::shared_ptr<const ImageData> image() const;
    // One destination upload is cached; snapshots retain their texture accounting lease.
    SurfaceImage bitmap(ID2D1RenderTarget* destination);
    void reset();

private:
    void renderImpl(ID2D1Factory* factory, IWICImagingFactory* wic, SurfaceSize size, void* context,
                    void (*paint)(void*, ID2D1RenderTarget*), ResourceAccount account);
    void clippedImpl(Rect area, void* context, void (*paint)(void*));
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace cybercanvas
