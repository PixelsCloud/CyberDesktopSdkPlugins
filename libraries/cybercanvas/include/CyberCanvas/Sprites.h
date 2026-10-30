#pragma once
#include <memory>
#include <span>
#include <utility>

#include "CyberCanvas/Image.h"
#include "CyberCanvas/Surface.h"

namespace cybercanvas {
class Canvas;

// Instances preserve submission order and inherit the batch's saved transform.
// One atlas/material and sampling mode per submission; no global layer sorting.
class SpriteList {
public:
    SpriteList();

    ~SpriteList();

    void begin(Canvas& canvas);

    D2D1_MATRIX_3X2_F pose(Canvas& canvas) const;

    void add(Rect destination, D2D1_RECT_U source, D2D1_MATRIX_3X2_F pose = D2D1::Matrix3x2F::Identity(),
             float opacity = 1);

    void flush(Canvas& canvas, ID2D1Bitmap* atlas, ImageSampling sampling = ImageSampling::Smooth);

    void vertical(Rect destination, D2D1_RECT_U source, uint32_t capPixels, double capLength,
                  D2D1_MATRIX_3X2_F pose = D2D1::Matrix3x2F::Identity());

    SpriteList(const SpriteList&) = delete;

    SpriteList& operator=(const SpriteList&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Retained small material atlas. DPI/size, dependency revision and resource
// domain changes invalidate only this surface; pixel data is never read back.
class SpriteAtlas {
public:
    SpriteAtlas();

    ~SpriteAtlas();

    template <typename Paint>
    void prepare(Canvas& canvas, SurfaceSize size, uint64_t revision, Paint&& paint) {
        auto callback = [&](Canvas& layer) -> void {
            return std::forward<Paint>(paint)(layer);
        };
        return prepareImpl(canvas, size, revision, &callback, [](void* callbackContext, Canvas& layer) -> void {
            return (*static_cast<decltype(callback)*>(callbackContext))(layer);
        });
    }

    ID2D1Bitmap* bitmap() const;

    void reset();

    SpriteAtlas(const SpriteAtlas&) = delete;

    SpriteAtlas& operator=(const SpriteAtlas&) = delete;

private:
    void prepareImpl(Canvas& canvas, SurfaceSize size, uint64_t revision, void* callbackContext,
                     void (*paint)(void*, Canvas&));
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class SpritePalette {
public:
    SpritePalette();

    ~SpritePalette();

    void build(Canvas& canvas, std::span<const SurfaceImage> materials, ImageSampling sampling = ImageSampling::Smooth);

    void begin(Canvas& canvas);

    void add(unsigned material, Rect destination, float opacity = 1);

    void flush(Canvas& canvas);

    SpritePalette(const SpritePalette&) = delete;

    SpritePalette& operator=(const SpritePalette&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Small repeated vector materials share the existing atlas/surface/budget
// machinery. Geometry callbacks run only for a new material, never per dot.
class PrimitiveSprites {
public:
    PrimitiveSprites();

    ~PrimitiveSprites();

    void begin(Canvas& canvas, double size, double style = 0);

    template <typename Paint>
    void add(Canvas& canvas, uint64_t key, Point center, Paint&& paint) {
        auto callback = [&](Canvas& layer) -> void {
            return std::forward<Paint>(paint)(layer);
        };
        return addImpl(canvas, key, center, &callback, [](void* callbackContext, Canvas& layer) -> void {
            return (*static_cast<decltype(callback)*>(callbackContext))(layer);
        });
    }

    void flush(Canvas& canvas);

    PrimitiveSprites(const PrimitiveSprites&) = delete;

    PrimitiveSprites& operator=(const PrimitiveSprites&) = delete;

private:
    void addImpl(Canvas& canvas, uint64_t key, Point center, void* callbackContext, void (*paint)(void*, Canvas&));
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace cybercanvas
