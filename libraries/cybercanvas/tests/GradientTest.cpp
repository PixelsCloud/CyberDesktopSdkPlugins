#include "tests/GradientTest.h"

#include <CyberCanvas/Image.h>

#include <limits>

namespace canvas_testing {
void gradientTests(cybercanvas::Canvas& canvas, const Check& check) {
    using namespace cybercanvas;
    const D2D1_GRADIENT_STOP stops[]{
        {0, color(0xF9C57F80)}, {.4f, color(0x397BD0CC)}, {.4f, color(0x91CF7099)}, {1, color(0x28416A00)}};
    check("gradient acquisition requires an active frame", rejected([&] {
              canvas.linearGradient({}, {20, 20}, stops);
          }));
    // Compare with the existing native renderer: repeated offsets, translucent
    // colors and shifted radial origins must retain their exact raster behavior.
    for (bool radial : {false, true}) {
        for (double dpi : {1., 1.5, 2.}) {
            Buffer expected(dpi), actual(dpi);
            canvas.render(expected.frame, {}, [&](auto& draw, auto) {
                ComPtr<ID2D1GradientStopCollection> collection;
                require(draw.target->CreateGradientStopCollection(stops, 4, collection.GetAddressOf()));
                ComPtr<ID2D1Brush> brush;
                if (radial) {
                    ComPtr<ID2D1RadialGradientBrush> fill;
                    require(draw.target->CreateRadialGradientBrush(
                        D2D1::RadialGradientBrushProperties(D2D1::Point2F(40, 30), D2D1::Point2F(4, -3), 35, 22),
                        collection.Get(), fill.GetAddressOf()));
                    brush = fill;
                } else {
                    ComPtr<ID2D1LinearGradientBrush> fill;
                    require(draw.target->CreateLinearGradientBrush(
                        D2D1::LinearGradientBrushProperties(D2D1::Point2F(5, 3), D2D1::Point2F(80, 65)),
                        collection.Get(), fill.GetAddressOf()));
                    brush = fill;
                }
                draw.target->FillRectangle(D2D1::RectF(0, 0, 96, 80), brush.Get());
            });
            canvas.render(actual.frame, {}, [&](auto& draw, auto) {
                ComPtr<ID2D1Brush> brush;
                if (radial) {
                    brush = draw.radialGradient({40, 30}, {35, 22}, stops, {4, -3});
                } else {
                    brush = draw.linearGradient({5, 3}, {80, 65}, stops);
                }
                draw.target->FillRectangle(D2D1::RectF(0, 0, 96, 80), brush.Get());
            });
            check("cached linear/radial gradients preserve native translucent pixels at each DPI",
                  expected.pixels == actual.pixels);
        }
    }
    Buffer output;
    canvas.render(output.frame, {}, [&](auto& draw, auto) {
        const auto before = draw.gradientCacheStats();
        auto first = draw.linearGradient({}, {60, 60}, stops);
        auto again = draw.linearGradient({}, {60, 60}, stops);
        auto shifted = draw.linearGradient({10, 10}, {70, 70}, stops);
        auto radial = draw.radialGradient({30, 30}, {20, 20}, stops);
        const auto after = draw.gradientCacheStats();
        check("identical gradients reuse a brush while moving geometry reuses stop collections",
              first == again && first != shifted && after.brushCreates - before.brushCreates == 3 &&
                  after.stopCreates - before.stopCreates == 1 && after.brushHits > before.brushHits &&
                  after.stopHits - before.stopHits == 2);
        check("simultaneously held gradients keep independent endpoints",
              first->GetEndPoint().x == 60 && shifted->GetStartPoint().x == 10);
        auto shiftedRadial = draw.radialGradient({30, 30}, {20, 20}, stops, {3, 2});
        check("radial origin offsets participate in the brush key",
              radial != shiftedRadial && radial->GetGradientOriginOffset().x == 0 &&
                  shiftedRadial->GetGradientOriginOffset().x == 3);

        ComPtr<ID2D1BitmapRenderTarget> offscreen;
        require(draw.target->CreateCompatibleRenderTarget(D2D1::SizeF(96, 80), offscreen.GetAddressOf()));
        auto isolated = draw.linearGradient({}, {60, 60}, stops, offscreen.Get());
        check("compatible targets never share target-bound brushes",
              isolated != first && draw.gradientCacheStats().targets == 2);
        offscreen->BeginDraw();
        offscreen->Clear(D2D1::ColorF(0, 0, 0, 0));
        offscreen->FillRectangle(D2D1::RectF(0, 0, 96, 80), isolated.Get());
        require(offscreen->EndDraw());
        draw.releaseGradientTarget(offscreen.Get());
        check("explicit offscreen release preserves the main target cache",
              draw.gradientCacheStats().targets == 1 && draw.linearGradient({}, {60, 60}, stops) == first);

        for (int i = 0; i < 100; ++i) {
            draw.linearGradient({}, {double(i + 100), 60}, stops);
        }
        check("animated geometry has a bounded brush cache and held brushes survive eviction",
              draw.gradientCacheStats().brushes == GradientCache::kBrushesPerTarget && first->GetEndPoint().x == 60);
        draw.target->FillRectangle(D2D1::RectF(0, 0, 96, 80), first.Get());
        for (int i = 0; i < 100; ++i) {
            const D2D1_GRADIENT_STOP colors[]{{0, D2D1::ColorF(float(i) / 100, 0, 0, .5f)}, {1, color(0xFFFFFFFF)}};
            draw.linearGradient({}, {60, 60}, colors);
        }
        check("continuous color transitions bound both stop and brush indexes",
              draw.gradientCacheStats().stopCollections == GradientCache::kStopsPerTarget &&
                  draw.gradientCacheStats().brushes == GradientCache::kBrushesPerTarget);
        const D2D1_GRADIENT_STOP descending[]{{.9f, color(0xFFFFFFFF)}, {.1f, color(0xFFFFFFFF)}};
        const D2D1_GRADIENT_STOP invalid[]{{0, {0, 0, 0, std::numeric_limits<float>::quiet_NaN()}},
                                           {1, color(0xFFFFFFFF)}};
        check("invalid stops are rejected without poisoning the cache",
              rejected([&] {
                  draw.linearGradient({}, {60, 60}, descending);
              }) &&
                  rejected([&] {
                      draw.linearGradient({}, {60, 60}, invalid);
                  }) &&
                  rejected([&] {
                      draw.linearGradient({}, {60, 60}, {});
                  }));
        std::array<D2D1_GRADIENT_STOP, 33> tooMany{};
        check("excessive stops and nonfinite geometry are rejected",
              rejected([&] {
                  draw.linearGradient({}, {60, 60}, tooMany);
              }) &&
                  rejected([&] {
                      draw.linearGradient({}, {std::numeric_limits<double>::infinity(), 0}, stops);
                  }) &&
                  rejected([&] {
                      draw.radialGradient({}, {0, 20}, stops);
                  }));

        std::vector<ComPtr<ID2D1BitmapRenderTarget>> targets;
        for (int i = 0; i < 6; ++i) {
            ComPtr<ID2D1BitmapRenderTarget> child;
            require(draw.target->CreateCompatibleRenderTarget(D2D1::SizeF(16, 16), child.GetAddressOf()));
            draw.linearGradient({}, {16, 16}, stops, child.Get());
            targets.push_back(std::move(child));
        }
        check("transient render targets are retained only up to the cache limit",
              draw.gradientCacheStats().targets == GradientCache::kTargets);
        for (const auto& child : targets) {
            draw.releaseGradientTarget(child.Get());
        }
        check("explicit release removes all transient target resources", draw.gradientCacheStats().targets == 0);
        draw.gradientBox({0, 0, 20, 20}, 3, 0xFFFFFFFF, 0x000000FF);
        const auto stable = draw.gradientCacheStats();
        draw.gradientBox({0, 0, 20, 20}, 3, 0xFFFFFFFF, 0x000000FF);
        check("existing gradientBox uses the same shared cache",
              draw.gradientCacheStats().brushCreates == stable.brushCreates &&
                  draw.gradientCacheStats().brushHits == stable.brushHits + 1);
    });
    Buffer resized(2);
    canvas.render(resized.frame, {}, [&](auto& draw, auto) {
        check("DPI and surface recreation discard target-bound gradient resources",
              draw.gradientCacheStats().targets == 0);
        draw.gradientBox({0, 0, 20, 20}, 3, 0xFFFFFFFF, 0x000000FF);
    });
}
}  // namespace canvas_testing
