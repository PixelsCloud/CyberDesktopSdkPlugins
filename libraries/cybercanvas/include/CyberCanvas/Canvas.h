#pragma once

#include <d2d1_3.h>

#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "CyberCanvas/Diagnostics.h"
#include "CyberCanvas/Gradients.h"
#include "CyberCanvas/Image.h"
#include "CyberCanvas/Layout.h"
#include "CyberCanvas/Native.h"
#include "CyberCanvas/Path.h"
#include "CyberCanvas/Surface.h"
#include "CyberCanvas/Types.h"

namespace cybercanvas {
class Canvas {
    class Scope;

    class DrawingState;

public:
    ComPtr<ID2D1Factory> factory;
    ComPtr<ID2D1RenderTarget> target;
    ComPtr<ID2D1SolidColorBrush> brush;

    Canvas();

    ~Canvas();

    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;
    Canvas(Canvas&&) = delete;
    Canvas& operator=(Canvas&&) = delete;

    // Borrowed owner-thread extensions. Keep custom resources inside the Canvas
    // lifetime. Canvas owns its primary BeginDraw/EndDraw, target recreation and
    // COM. Independent native offscreen caches must not replace these handles.
    IDWriteFactory* nativeTextFactory() const;

    IDWriteFontCollection* nativeFontCollection() const;

    IWICImagingFactory* nativeImageFactory() const;

    class Frame {
    public:
        Frame(Canvas& canvas, const FrameBuffer& output, Rect safe);

        ~Frame();

        Frame(const Frame&) = delete;
        Frame& operator=(const Frame&) = delete;

        Frame(Frame&& other) noexcept;

        Frame& operator=(Frame&&) = delete;

        Rect area() const;

        void finish();

    private:
        Canvas& canvas_;
        FrameBuffer output_;
        Rect area_;
        bool active_ = true;
    };

    Frame frame(const FrameBuffer& output, Rect safe = {});

    // Paint an independent compatible layer without replacing the primary
    // Canvas target. The child has its own state, masks, brushes and caches.
    template <typename Paint>
    void renderLayer(OffscreenSurface& surface, SurfaceSize size, Paint&& paint) {
        auto callback = [&](Canvas& layer) {
            std::forward<Paint>(paint)(layer);
        };
        renderLayerImpl(surface, size, &callback, [](void* context, Canvas& layer) {
            (*static_cast<decltype(callback)*>(context))(layer);
        });
    }

    void resourceAccount(ResourceAccount account);

    ResourceAccount resourceAccount() const;

    // Draw a reusable producer into an active caller's target without beginning,
    // clearing or finishing that output. The producer retains its own caches.
    template <typename Paint>
    void drawIn(Canvas& owner, Rect area, Paint&& paint) {
        auto callback = [&](Canvas& producer) {
            std::forward<Paint>(paint)(producer);
        };
        drawInImpl(owner, area, &callback, [](void* context, Canvas& producer) {
            (*static_cast<decltype(callback)*>(context))(producer);
        });
    }

    // Coordinates are logical pixels relative to the safe drawing area. Output
    // storage is borrowed; Canvas never retains its pointer between frames.
    template <typename Paint>
    void render(const FrameBuffer& frame, Rect safe, Paint&& paint) {
        auto callback = [&](Canvas& canvas, Rect area) {
            std::forward<Paint>(paint)(canvas, area);
        };
        renderImpl(frame, safe, &callback, [](void* context, Canvas& canvas, Rect area) {
            (*static_cast<decltype(callback)*>(context))(canvas, area);
        });
    }

    template <typename Paint>
    void masked(ID2D1Geometry* geometry, Paint&& paint, D2D1_RECT_F bounds = D2D1::InfiniteRect()) {
        auto callback = [&]() {
            std::forward<Paint>(paint)();
        };
        maskedImpl(
            geometry, &callback,
            [](void* context) {
                (*static_cast<decltype(callback)*>(context))();
            },
            bounds);
    }

    template <typename Paint>
    void masked(Path& path, Paint&& paint) {
        active();
        masked(path.geometry(factory.Get()), std::forward<Paint>(paint));
    }

    // Saves Direct2D drawing state plus the shared brush. Clip/layer lifetimes are
    // managed by clipped()/withOpacity(); never finish a frame inside a callback.
    template <typename Paint>
    void saved(Paint&& paint) {
        auto callback = [&]() {
            std::forward<Paint>(paint)();
        };
        savedImpl(&callback, [](void* context) {
            (*static_cast<decltype(callback)*>(context))();
        });
    }

    void translate(double x, double y);

    void rotate(double degrees, Point center = {});

    void scale(double x, double y, Point center = {});

    template <typename Paint>
    void posed(const Pose& pose, Paint&& paint) {
        auto callback = [&]() {
            std::forward<Paint>(paint)();
        };
        posedImpl(pose, &callback, [](void* context) {
            (*static_cast<decltype(callback)*>(context))();
        });
    }

    // Opacity is composited once for the whole group, including overlapping
    // primitives. Cached Direct2D layers are bounded and rebuilt with the target.
    template <typename Paint>
    void withOpacity(double opacity, Paint&& paint) {
        auto callback = [&]() {
            std::forward<Paint>(paint)();
        };
        withOpacityImpl(opacity, &callback, [](void* context) {
            (*static_cast<decltype(callback)*>(context))();
        });
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

private:
    void pushClip(Rect area);

    void active() const;

    static void coordinate(double value);

    void transform(const D2D1::Matrix3x2F& value);

    void popScope() noexcept;

    void releaseResources() noexcept;

public:
    bool valid(const FrameBuffer& f) const;

    // Optional host-owned GPU target on the same render thread. Switching the
    // resource domain releases target-bound caches; steady frames retain them.
    void targetForFrame(ID2D1RenderTarget* external, const FrameBuffer& frame, bool hostDrawScope = false);

    Rect begin(const FrameBuffer& f, Rect safe);

    // A single atlas submission on D2D 1.3 hardware contexts. The portable WIC
    // renderer keeps equivalent source rectangles for export and previews.
    void sprites(ID2D1Bitmap* atlas, std::span<const D2D1_RECT_F> destinations, std::span<const D2D1_RECT_U> sources,
                 std::span<const D2D1_MATRIX_3X2_F> transforms = {}, ImageSampling sampling = ImageSampling::Smooth,
                 std::span<const float> opacities = {});

    void end(const FrameBuffer& f);

    void cancel() noexcept;

    void ink(uint32_t rgba);

    void box(double x, double y, double w, double h, double r, uint32_t rgba);

    // Per-primitive alpha does not allocate a group-opacity layer.
    void rectangle(Rect area, uint32_t rgba, double opacity = 1);

    void gradientBox(Rect area, double radius, uint32_t top, uint32_t bottom);

    // Read-only shared native brushes; explicit targets support compatible offscreen surfaces.
    ComPtr<ID2D1LinearGradientBrush> linearGradient(Point start, Point end, std::span<const D2D1_GRADIENT_STOP> stops,
                                                    ID2D1RenderTarget* destination = nullptr);

    ComPtr<ID2D1RadialGradientBrush> radialGradient(Point center, Size radii, std::span<const D2D1_GRADIENT_STOP> stops,
                                                    Point offset = {}, ID2D1RenderTarget* destination = nullptr);

    void releaseGradientTarget(ID2D1RenderTarget* destination) noexcept;

    GradientCacheStats gradientCacheStats() const;

    void oval(double x, double y, double rx, double ry, uint32_t rgba, double stroke = 0);

    void line(double x, double y, double xx, double yy, uint32_t rgba, double thickness = 1);

    double devicePixelRatio() const;

    // One-frame premultiplied RGBA transfer. The source is caller-owned and
    // read only during this call. Transient uploads are bounded to 64 MiB/frame.
    void blit(const FrameBuffer& source, Rect area);

    // Immutable premultiplied RGBA content; change key when its pixels change.
    // Uploads are target-bound and cached (64 entries), including DPI rebuilds.
    void pixels(const std::wstring& key, const FrameBuffer& source, Rect area);

    void image(const std::wstring& filename, Rect area, uint32_t cell = 0, uint32_t cells = 1);

    // Decoded resources upload on this Canvas's thread; no file I/O or decoding.
    void image(const ImageData& image, Rect area, ImagePlacement placement = {});

    uint64_t imageUploadBytes() const;

    CanvasStats stats() const;

    void clearImageUploads();

    // Retains the supplied collection; nullptr restores system fonts. Layouts
    // cannot be reused across collections even when family names are identical.
    void fontCollection(IDWriteFontCollection* collection);

    // Native extension for baseline/relief painters. The cached layout is shared;
    // callers may measure/draw it but must not mutate it or retain across frames.
    ComPtr<IDWriteTextLayout> nativeTextLayout(const std::wstring& value, const TextStyle& style, Size available);

    struct InkText {
        // Read-only shared layout; draw before the next text request on this Canvas.
        ComPtr<IDWriteTextLayout> layout;
        Point origin;
        Rect bounds;
    };

    // Visible ink relative to the layout origin, including glyph overhangs.
    Rect measureTextInk(const std::wstring& value, const TextStyle& style, Size available = {1000, 1000});

    // Fit single-line visible ink without enlarging it. References share the fit
    // size (e.g. clock digits); each value still aligns by its own visible glyphs.
    // A margin below 1 adds breathing room when shrinking. It also avoids tiny
    // size changes near the boundary. No cached layout is mutated for font size.
    InkText fitTextInk(const std::wstring& value, TextStyle style, Rect area,
                       std::span<const std::wstring> references = {}, double shrinkMargin = .995);

private:
    ComPtr<IDWriteTextLayout> textLayout(const std::wstring& value, const TextStyle& style, Size available);

    ID2D1StrokeStyle* strokeStyle(const StrokeStyle& style);

public:
    TextMetrics measureText(const std::wstring& value, const TextStyle& style, Size available = {16384, 16384});

    std::vector<TextLineMetrics> textLines(const std::wstring& value, const TextStyle& style, double width);

    void text(const std::wstring& value, const TextStyle& style, Rect area, uint32_t rgba);

    Path path(FillRule rule = FillRule::Alternate);

    void fill(Path& path, uint32_t rgba);

    void stroke(Path& path, uint32_t rgba, const StrokeStyle& style = {});

    void strokeBox(Rect area, double radius, uint32_t rgba, const StrokeStyle& style = {});

    void shape(const std::vector<D2D1_POINT_2F>& points, uint32_t fill, uint32_t outline = 0, double thickness = 1);

    bool save(const std::wstring& filename, const FrameBuffer& f);

private:
    struct LayerTag {};

    Canvas(LayerTag, Canvas& owner, ID2D1RenderTarget* destination, SurfaceSize size);

    uint64_t trackedBytes() const;

    void trackMemory();

    void renderLayerImpl(OffscreenSurface& surface, SurfaceSize size, void* context, void (*paint)(void*, Canvas&));
    void drawInImpl(Canvas& owner, Rect area, void* context, void (*paint)(void*, Canvas&));
    void renderImpl(const FrameBuffer& frame, Rect safe, void* context, void (*paint)(void*, Canvas&, Rect));
    void maskedImpl(ID2D1Geometry* geometry, void* context, void (*paint)(void*),
                    D2D1_RECT_F bounds = D2D1::InfiniteRect());
    void savedImpl(void* context, void (*paint)(void*));
    void posedImpl(const Pose& pose, void* context, void (*paint)(void*));
    void withOpacityImpl(double opacity, void* context, void (*paint)(void*));
    void clippedImpl(Rect area, void* context, void (*paint)(void*));

    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace cybercanvas
