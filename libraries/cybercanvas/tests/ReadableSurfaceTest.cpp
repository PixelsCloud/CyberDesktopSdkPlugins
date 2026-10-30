#include "tests/ReadableSurfaceTest.h"

#include <CyberCanvas/Image.h>
#include <CyberCanvas/ReadableSurface.h>

#include <limits>

namespace canvas_testing {
void readableSurfaceTests(cybercanvas::Canvas& canvas, const Check& check) {
    using namespace cybercanvas;
    ReadableSurface surface;
    check("unrendered readable surface has no image", !surface.image());
    check("readable upload requires completed pixels", rejected([&] {
              surface.bitmap(nullptr);
          }));
    for (double dpi : {1., 1.25, 1.5, 2.}) {
        Buffer buffer(dpi);
        canvas.render(buffer.frame, {}, [&](auto& draw, auto) {
            const SurfaceSize size{uint32_t(24 * dpi), uint32_t(16 * dpi), dpi};
            const auto render = [&](auto&& action) {
                surface.render(draw.factory.Get(), draw.nativeImageFactory(), size,
                               std::forward<decltype(action)>(action));
            };
            ComPtr<ID2D1RenderTarget> first;
            render([&](auto* target) {
                first = target;
                check("readable target uses transparent text antialiasing",
                      target->GetTextAntialiasMode() == D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
                target->SetDpi(72, 72);
            });
            render([&](auto* target) {
                float xDpi = 0, yDpi = 0;
                target->GetDpi(&xDpi, &yDpi);
                check("readable surface reuses WIC target and restores requested DPI",
                      target == first.Get() && xDpi == float(96 * dpi) && yDpi == xDpi);
                target->Clear(color(0xFF000080));
                ComPtr<ID2D1SolidColorBrush> brush;
                require(target->CreateSolidColorBrush(color(0x0000FFFF), brush.GetAddressOf()));
                surface.clipped({0, 0, 12, 16}, [&] {
                    target->FillRectangle(D2D1::RectF(0, 0, 24, 16), brush.Get());
                });
            });
            const auto previous = surface.image();
            const auto left = (size_t(8 * dpi) * size.width + size_t(6 * dpi)) * 4;
            const auto right = (size_t(8 * dpi) * size.width + size_t(18 * dpi)) * 4;
            check("CPU readback preserves BGRA premultiplication and clipped placement",
                  previous->pixels()[left] == 255 && previous->pixels()[left + 3] == 255 &&
                      previous->pixels()[right + 2] == 128 && previous->pixels()[right + 3] == 128);
            const auto bitmap = surface.bitmap(draw.target.Get());
            float xDpi = 0, yDpi = 0;
            bitmap->GetDpi(&xDpi, &yDpi);
            check("readable bitmap caches upload and preserves logical source coordinates",
                  surface.bitmap(draw.target.Get()) == bitmap && xDpi == float(96 * dpi) && yDpi == xDpi &&
                      std::abs(bitmap->GetSize().width - 24) < .001);
            render([](auto* target) {
                target->Clear(color(0x00FF00FF));
            });
            check("new readback cannot modify a retained CPU snapshot",
                  surface.image()->id() != previous->id() && previous->pixels()[right + 2] == 128 &&
                      surface.image()->pixels()[right + 1] == 255);
            surface.reset();
            draw.target->DrawBitmap(bitmap.Get(), D2D1::RectF(0, 0, 24, 16));
        });
        check("retained readable bitmap survives rerender and reset",
              buffer.pixel(int(6 * dpi), int(8 * dpi)) == std::array<uint8_t, 4>{0, 0, 255, 255} &&
                  buffer.pixel(int(18 * dpi), int(8 * dpi)) == std::array<uint8_t, 4>{128, 0, 0, 128});
    }
    Buffer output;
    canvas.render(output.frame, {}, [&](auto& draw, auto) {
        const auto render = [&](auto&& action) {
            surface.render(draw.factory.Get(), draw.nativeImageFactory(), {24, 16, 1},
                           std::forward<decltype(action)>(action));
        };
        check("readable clipped exception propagates", rejected([&] {
                  render([&](auto*) {
                      surface.clipped({0, 0, 12, 16}, [] {
                          throw std::runtime_error("paint failed");
                      });
                  });
              }));
        check("failed readback exposes no partial snapshot", !surface.image());
        check("native EndDraw error invalidates readable surface", rejected([&] {
                                                                       render([](auto* target) {
                                                                           target->PopAxisAlignedClip();
                                                                       });
                                                                   }) &&
                                                                       !surface.image());
        render([&](auto* target) {
            check("active readable surface rejects reset and recursive rendering", rejected([&] {
                                                                                       surface.reset();
                                                                                   }) &&
                                                                                       rejected([&] {
                                                                                           render([](auto*) {
                                                                                           });
                                                                                       }));
            target->Clear(color(0xFF0000FF));
        });
        const auto previous = surface.image();
        check("invalid readable dimensions do not discard a good snapshot",
              rejected([&] {
                  surface.render(draw.factory.Get(), draw.nativeImageFactory(), {4097, 1, 1}, [](auto*) {
                  });
              }) &&
                  surface.image() == previous);
        Canvas other;
        Buffer otherOutput;
        other.render(otherOutput.frame, {}, [&](auto& foreign, auto) {
            foreign.target->DrawBitmap(surface.bitmap(foreign.target.Get()).Get());
        });
        check("CPU readback uploads independently to another Canvas", otherOutput.pixel(5, 5)[0] == 255);
        draw.target->DrawBitmap(surface.bitmap(draw.target.Get()).Get());
    });
    check("readable surface recovers after callback and EndDraw failures", output.pixel(5, 5)[0] == 255);
    check("readable clip requires an active render", rejected([&] {
              surface.clipped({0, 0, 1, 1}, [] {
              });
          }));
}
}  // namespace canvas_testing
