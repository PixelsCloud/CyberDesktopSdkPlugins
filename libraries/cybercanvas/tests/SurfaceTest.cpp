#include "tests/SurfaceTest.h"

#include <CyberCanvas/Image.h>
#include <CyberCanvas/Sprites.h>

#include <limits>

namespace canvas_testing {
void surfaceTests(cybercanvas::Canvas& canvas, const Check& check) {
    using namespace cybercanvas;
    Buffer output;
    OffscreenSurface surface;
    auto mask = canvas.path();
    mask.moveTo({8, 8}).lineTo({40, 8}).lineTo({40, 40}).lineTo({8, 40}).close();
    check("surface mask rejects drawing outside a render", rejected([&] {
              surface.masked(mask.geometry(canvas.factory.Get()), [] {
              });
          }));
    check("canvas mask rejects drawing outside a frame", rejected([&] {
              canvas.masked(mask, [] {
              });
          }));
    ComPtr<ID2D1BitmapRenderTarget> firstTarget;
    const auto paint = [&](auto* target) {
        ComPtr<ID2D1SolidColorBrush> brush;
        require(target->CreateSolidColorBrush(color(0xFF0000FF), brush.GetAddressOf()));
        surface.masked(mask.geometry(canvas.factory.Get()), [&] {
            target->FillRectangle(D2D1::RectF(0, 0, 96, 80), brush.Get());
        });
        check("offscreen text uses grayscale antialiasing",
              target->GetTextAntialiasMode() == D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    };
    for (double dpi : {1., 1.5, 2., 1.}) {
        Buffer scaled(dpi);
        canvas.render(scaled.frame, {}, [&](auto& draw, auto) {
            const SurfaceSize size{scaled.frame.width, scaled.frame.height, dpi};
            surface.render(draw.target.Get(), size, [&](auto* target) {
                firstTarget = target;
                paint(target);
            });
            check("surface matches its successful target, physical size and DPI",
                  surface.matches(draw.target.Get(), size));
            check("surface rejects a different physical size",
                  !surface.matches(draw.target.Get(), {size.width + 1, size.height, dpi}));
            check("surface rejects a different DPI",
                  !surface.matches(draw.target.Get(), {size.width, size.height, dpi + .25}));
            check("offscreen dimensions follow physical DPI",
                  surface.bitmap()->GetPixelSize().width == scaled.frame.width && surface.logicalSize().width == 96);
            surface.render(draw.target.Get(), size, [&](auto* target) {
                check("offscreen render reuses an unchanged target", target == firstTarget.Get());
                paint(target);
            });
            draw.target->DrawBitmap(surface.bitmap());
        });
        check("offscreen masks preserve alpha and placement at every DPI",
              scaled.pixel(int(20 * dpi), int(20 * dpi)) == std::array<uint8_t, 4>{255, 0, 0, 255} &&
                  scaled.pixel(int(50 * dpi), int(20 * dpi))[3] == 0);
    }
    canvas.render(output.frame, {}, [&](auto& draw, auto) {
        const SurfaceSize size{96, 80, 1};
        check("offscreen callback exception propagates", rejected([&] {
                  surface.render(draw.target.Get(), size, [&](auto*) {
                      surface.masked(mask.geometry(canvas.factory.Get()), [&] {
                          throw std::runtime_error("paint failed");
                      });
                  });
              }));
        check("failed offscreen render cannot expose partial pixels", !surface.bitmap());
        surface.render(draw.target.Get(), size, paint);
        check("offscreen frame recovers after masked exception", surface.bitmap() != nullptr);
        check("offscreen EndDraw failure propagates", rejected([&] {
                  surface.render(draw.target.Get(), size, [](auto* target) {
                      // Deliberate native extension error, reported by EndDraw.
                      target->PopAxisAlignedClip();
                  });
              }));
        check("EndDraw failure invalidates surface", !surface.bitmap());
        surface.render(draw.target.Get(), size, paint);
        surface.render(draw.target.Get(), size, [&](auto* target) {
            check("active surface rejects reset", rejected([&] {
                      surface.reset();
                  }));
            check("active surface rejects recursive render", rejected([&] {
                      surface.render(draw.target.Get(), size, paint);
                  }));
            paint(target);
        });
        const auto* cached = surface.bitmap();
        check("surface rejects invalid DPI", rejected([&] {
                  surface.render(draw.target.Get(), {96, 80, 0}, paint);
              }));
        check("surface rejects oversized allocation", rejected([&] {
                  surface.render(draw.target.Get(), {4097, 80, 1}, paint);
              }));
        check("invalid arguments preserve the last successful image", surface.bitmap() == cached);
        draw.target->DrawBitmap(surface.bitmap());
    });
    check("offscreen recovers from native target error", output.pixel(20, 20)[0] == 255);
    surface.invalidate();
    check("semantic invalidation hides the cached image", !surface.bitmap());

    canvas.render(output.frame, {}, [&](auto& draw, auto) {
        check("mask callback exception propagates", rejected([&] {
                  draw.masked(mask, [&] {
                      throw std::runtime_error("mask failed");
                  });
              }));
        draw.rectangle({60, 10, 10, 10}, 0x00FF00FF);
        draw.withOpacity(.5, [&] {
            draw.masked(mask, [&] {
                draw.clipped({10, 10, 15, 15}, [&] {
                    draw.rectangle({0, 0, 80, 80}, 0xFF0000FF);
                });
            });
        });
        check(
            "mask rejects another factory", rejected([&] {
                Canvas other;
                ComPtr<ID2D1EllipseGeometry> foreign;
                require(other.factory->CreateEllipseGeometry(D2D1::Ellipse({20, 20}, 10, 10), foreign.GetAddressOf()));
                draw.masked(foreign.Get(), [] {
                });
            }));
        const auto nested = [&](auto&& self, int remaining) -> void {
            draw.masked(mask, [&] {
                if (remaining) {
                    self(self, remaining - 1);
                }
            });
        };
        check("mask nesting is bounded", rejected([&] {
                  nested(nested, 32);
              }));
        draw.rectangle({70, 30, 10, 10}, 0x0000FFFF);
    });
    check("mask exception unwinds layers before subsequent drawing",
          output.pixel(65, 15)[1] == 255 && output.pixel(75, 35)[2] == 255);
    check("mask nests with rectangular clip and group opacity",
          output.pixel(15, 15)[3] >= 127 && output.pixel(15, 15)[3] <= 128 && output.pixel(30, 15)[3] == 0);
    // Another Canvas with the same pixel dimensions still needs a new compatible target.
    Canvas other;
    other.render(output.frame, {}, [&](auto& draw, auto) {
        check("surface rejects another target with identical dimensions",
              !surface.matches(draw.target.Get(), {96, 80, 1}));
        surface.render(draw.target.Get(), {96, 80, 1}, [&](auto* target) {
            check("surface recreates when owner changes", target != firstTarget.Get());
            target->Clear(color(0x0000FFFF));
        });
        draw.target->DrawBitmap(surface.bitmap());
    });
    check("surface can draw on a new owner", output.pixel(20, 20)[2] == 255);
    surface.reset();

    SpriteAtlas atlas;
    unsigned paints = 0;
    const auto renderAtlas = [&](Canvas& owner, SurfaceSize size, uint64_t revision) {
        owner.render(output.frame, {}, [&](auto& draw, auto) {
            atlas.prepare(draw, size, revision, [&](auto& layer) {
                ++paints;
                layer.box(0, 0, 12, 12, 0, revision == 1 ? 0xFF0000FF : 0x0000FFFF);
            });
            draw.target->DrawBitmap(atlas.bitmap());
        });
    };
    renderAtlas(canvas, {16, 16, 1}, 1);
    renderAtlas(canvas, {16, 16, 1}, 1);
    check("stable sprite atlas skips its paint callback", paints == 1);
    renderAtlas(canvas, {16, 16, 1}, 2);
    check("sprite dependency revision refreshes pixels", paints == 2 && output.pixel(4, 4)[2] == 255);
    renderAtlas(other, {16, 16, 1}, 2);
    renderAtlas(canvas, {16, 16, 1}, 2);
    check("sprite atlas follows target changes in both directions", paints == 4);
    renderAtlas(canvas, {24, 16, 1}, 2);
    renderAtlas(canvas, {24, 16, 2}, 2);
    check("sprite atlas rebuilds for size and DPI changes", paints == 6);
    atlas.reset();
    check("sprite reset releases visible cached bitmap", !atlas.bitmap());
    renderAtlas(canvas, {24, 16, 2}, 2);
    check("sprite reset permits a fresh render", paints == 7);
    canvas.render(output.frame, {}, [&](auto& draw, auto) {
        check("sprite paint failure propagates", rejected([&] {
                  atlas.prepare(draw, {24, 16, 2}, 3, [](auto&) {
                      throw std::runtime_error("injected atlas paint failure");
                  });
              }));
        check("sprite paint failure discards partial cached image", !atlas.bitmap());
    });
    renderAtlas(canvas, {24, 16, 2}, 3);
    check("sprite paint failure can recover", paints == 8);

    // Sprite caches retain the bitmap after the temporary compatible target dies.
    // Each row must stay independent when another surface is rendered afterward.
    for (double dpi : {1., 1.5, 2.}) {
        Buffer scaled(dpi);
        ComPtr<ID2D1Bitmap> red, blue;
        canvas.render(scaled.frame, {}, [&](auto& draw, auto) {
            const auto sprite = [&](uint32_t rgba) {
                OffscreenSurface temporary;
                temporary.render(draw.target.Get(), {uint32_t(12 * dpi), uint32_t(8 * dpi), dpi}, [&](auto* target) {
                    target->Clear(color(rgba));
                });
                return ComPtr<ID2D1Bitmap>(temporary.bitmap());
            };
            red = sprite(0xFF000080);
            blue = sprite(0x0000FFFF);
            check("retained sprite keeps its physical size and logical DPI",
                  red->GetPixelSize().width == uint32_t(12 * dpi) && std::abs(red->GetSize().width - 12) < .001);
        });
        canvas.render(scaled.frame, {}, [&](auto& draw, auto) {
            draw.target->DrawBitmap(red.Get(), D2D1::RectF(10, 10, 22, 18));
            draw.target->DrawBitmap(blue.Get(), D2D1::RectF(30, 10, 42, 18));
        });
        check("retained sprites survive temporary surface destruction and another frame",
              scaled.pixel(int(15 * dpi), int(14 * dpi)) == std::array<uint8_t, 4>{128, 0, 0, 128} &&
                  scaled.pixel(int(35 * dpi), int(14 * dpi)) == std::array<uint8_t, 4>{0, 0, 255, 255} &&
                  scaled.pixel(int(26 * dpi), int(14 * dpi))[3] == 0);
    }
}
}  // namespace canvas_testing
