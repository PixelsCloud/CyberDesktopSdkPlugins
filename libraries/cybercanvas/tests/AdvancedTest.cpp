#include <CyberCanvas/Canvas.h>

#include <array>
#include <iostream>
#include <limits>
#include <vector>

#include "tests/GradientTest.h"
#include "tests/ReadableSurfaceTest.h"
#include "tests/SurfaceTest.h"
#include "tests/TextInkTest.h"

int main() {
    using namespace canvas_testing;
    using namespace cybercanvas;
    bool passed = true;
    const auto check = [&](const char* name, bool value) {
        passed &= value;
        std::cout << (value ? "PASS " : "FAIL ") << name << '\n';
    };
    try {
        Canvas canvas;
        gradientTests(canvas, check);
        surfaceTests(canvas, check);
        readableSurfaceTests(canvas, check);
        textInkTests(canvas, check);
        Buffer output;
        check("drawing outside a frame reports a logic error", rejected([&] {
                  canvas.translate(1, 1);
              }));
        auto curved = canvas.path();
        curved.moveTo({5, 25})
            .cubicTo({5, 5}, {35, 5}, {35, 25})
            .lineTo({35, 35})
            .quadraticTo({20, 45}, {5, 35})
            .close();
        canvas.render(output.frame, {}, [&](auto& draw, auto) {
            draw.fill(curved, 0xFF0000FF);
        });
        check("cubic and quadratic curves fill their actual contour",
              output.pixel(20, 20)[0] == 255 && output.pixel(20, 37)[0] == 255 && output.pixel(20, 4)[3] == 0);
        check("drawing seals a retained path", rejected([&] {
                  curved.lineTo({0, 0});
              }));
        auto empty = canvas.path();
        check("segments require an initial move", rejected([&] {
                  empty.lineTo({0, 0});
              }));
        check("arcs require an initial move", rejected([&] {
                  empty.arcTo({20, 20}, {10, 10});
              }));
        auto circle = canvas.path();
        circle.moveTo({40, 20}).arcTo({40, 60}, {20, 20}).arcTo({40, 20}, {20, 20}).close();
        for (double dpi : {1., 1.5, 2.}) {
            Buffer scaled(dpi);
            canvas.render(scaled.frame, {}, [&](auto& draw, auto) {
                draw.fill(circle, 0xFFFFFFFF);
            });
            check("arc contours stay closed across DPI and target recreation",
                  scaled.pixel(int(40 * dpi), int(40 * dpi))[3] == 255 &&
                      scaled.pixel(int(15 * dpi), int(40 * dpi))[3] == 0);
        }
        for (auto sweep : {ArcSweep::Clockwise, ArcSweep::CounterClockwise}) {
            for (auto size : {ArcSize::Small, ArcSize::Large}) {
                for (double rotation : {0., 35.}) {
                    auto arc = canvas.path();
                    arc.moveTo({45, 15}).arcTo({65, 35}, {25, 17}, rotation, sweep, size).close();
                    ComPtr<ID2D1PathGeometry> native;
                    ComPtr<ID2D1GeometrySink> sink;
                    require(canvas.factory->CreatePathGeometry(native.GetAddressOf()));
                    require(native->Open(sink.GetAddressOf()));
                    sink->BeginFigure(D2D1::Point2F(45, 15), D2D1_FIGURE_BEGIN_FILLED);
                    sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(65, 35), D2D1::SizeF(25, 17), float(rotation),
                                                  sweep == ArcSweep::Clockwise ? D2D1_SWEEP_DIRECTION_CLOCKWISE
                                                                               : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
                                                  size == ArcSize::Small ? D2D1_ARC_SIZE_SMALL : D2D1_ARC_SIZE_LARGE));
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                    require(sink->Close());
                    Buffer expected, actual;
                    canvas.render(expected.frame, {}, [&](auto& draw, auto) {
                        draw.ink(0xFFFFFFFF);
                        draw.target->FillGeometry(native.Get(), draw.brush.Get());
                    });
                    canvas.render(actual.frame, {}, [&](auto& draw, auto) {
                        draw.fill(arc, 0xFFFFFFFF);
                    });
                    check("elliptical sweep, size and rotation preserve native rasterization",
                          expected.pixels == actual.pixels);
                }
            }
        }
        auto recoverable = canvas.path();
        recoverable.moveTo({10, 10}, false);
        for (auto radii : {Size{0, 10}, Size{-1, 10}, Size{10, std::numeric_limits<double>::infinity()}}) {
            check("invalid arc radii are rejected before modifying the path", rejected([&] {
                      recoverable.arcTo({30, 30}, radii);
                  }));
        }
        check("nonfinite arc rotation is rejected", rejected([&] {
                  recoverable.arcTo({30, 30}, {10, 10}, std::numeric_limits<double>::quiet_NaN());
              }));
        recoverable.lineTo({40, 10}).lineTo({40, 40}).lineTo({10, 40}).close();
        canvas.render(output.frame, {}, [&](auto& draw, auto) {
            draw.fill(recoverable, 0xFFFFFFFF);
            draw.stroke(recoverable, 0xFFFFFFFF, {.width = 2});
        });
        check("hollow figures can be stroked without filling their interior after a rejected arc",
              output.pixel(10, 20)[3] == 255 && output.pixel(20, 20)[3] == 0);
        canvas.render(output.frame, {}, [&](auto& draw, auto) {
            const auto layers = draw.stats().opacityLayers;
            draw.rectangle({0, 0, 20, 20}, 0xFF0000FF, .5);
            draw.rectangle({30, 0, 20, 20}, 0x00FF0080, .5);
            draw.box(60, 0, 20, 20, 0, 0x0000FFFF);
            check("primitive opacity does not allocate group layers", draw.stats().opacityLayers == layers);
            check("invalid primitive opacity is rejected", rejected([&] {
                      draw.rectangle({0, 0, 20, 20}, 0xFFFFFFFF, -1);
                  }));
        });
        check("primitive opacity multiplies color alpha and does not leak to the next box",
              output.pixel(5, 5) == std::array<uint8_t, 4>{128, 0, 0, 128} && output.pixel(35, 5)[3] == 64 &&
                  output.pixel(65, 5)[3] == 255);
        for (auto rule : {FillRule::Alternate, FillRule::Winding}) {
            auto rings = canvas.path(rule);
            rings.moveTo({5, 5}).lineTo({45, 5}).lineTo({45, 45}).lineTo({5, 45}).close();
            rings.moveTo({15, 15}).lineTo({35, 15}).lineTo({35, 35}).lineTo({15, 35}).close();
            canvas.render(output.frame, {}, [&](auto& draw, auto) {
                draw.fill(rings, 0xFFFFFFFF);
            });
            check("compound paths obey their fill rule",
                  output.pixel(25, 25)[3] == (rule == FillRule::Alternate ? 0 : 255));
        }
        auto segment = canvas.path();
        segment.moveTo({10, 10}).lineTo({30, 10});
        canvas.render(output.frame, {}, [&](auto& draw, auto) {
            draw.stroke(segment, 0xFFFFFFFF, {.width = 6, .cap = LineCap::Round});
            draw.strokeBox({50, 10, 30, 30}, 3, 0xFFFFFFFF, {.width = 2});
        });
        check("open paths preserve round caps and stroke width",
              output.pixel(8, 10)[3] > 200 && output.pixel(20, 20)[3] == 0);
        check("rectangle strokes leave their interior transparent",
              output.pixel(50, 25)[3] == 255 && output.pixel(65, 25)[3] == 0);
        canvas.render(output.frame, {5, 7, 80, 60}, [&](auto& draw, auto) {
            draw.saved([&] {
                draw.translate(20, 20);
                draw.rotate(90);
                draw.scale(2, 1);
                draw.box(0, 0, 4, 3, 0, 0xFF0000FF);
            });
            draw.box(0, 0, 4, 4, 0, 0x00FF00FF);
        });
        check("local transforms compose before the safe-area translation",
              output.pixel(24, 30)[0] == 255 && output.pixel(7, 9)[1] == 255 && output.pixel(20, 20)[3] == 0);
        canvas.render(output.frame, {5, 7, 80, 60}, [&](auto& draw, auto) {
            draw.posed({.translation = {20, 20}, .scale = {2, 1}, .rotation = 90, .opacity = .5}, [&] {
                draw.box(0, 0, 4, 3, 0, 0xFF0000FF);
            });
            draw.box(0, 0, 4, 4, 0, 0x00FF00FF);
        });
        check("poses apply ordered transforms, group opacity and scoped cleanup",
              output.pixel(24, 30) == std::array<uint8_t, 4>{128, 0, 0, 128} && output.pixel(7, 9)[1] == 255);
        canvas.render(output.frame, {}, [&](auto& draw, auto) {
            draw.withOpacity(.5, [&] {
                draw.box(0, 0, 30, 30, 0, 0xFF0000FF);
                draw.box(15, 0, 30, 30, 0, 0x0000FFFF);
            });
            draw.withOpacity(.5, [&] {
                draw.withOpacity(.5, [&] {
                    draw.box(50, 0, 20, 20, 0, 0x00FF00FF);
                });
            });
        });
        check("opacity is applied to the group rather than individual overlaps",
              output.pixel(5, 5) == std::array<uint8_t, 4>{128, 0, 0, 128} &&
                  output.pixel(20, 5) == std::array<uint8_t, 4>{0, 0, 128, 128});
        check("nested opacity multiplies", output.pixel(55, 5)[1] == 64 && output.pixel(55, 5)[3] == 64);
        canvas.render(output.frame, {}, [&](auto& draw, auto) {
            const auto nest = [&](auto&& self, int depth) -> void {
                if (depth > 0) {
                    draw.withOpacity(.9, [&] {
                        self(self, depth - 1);
                    });
                }
            };
            check("excessive state/layer nesting is bounded and recoverable", rejected([&] {
                      nest(nest, 40);
                  }));
            draw.box(0, 0, 10, 10, 0, 0xFFFFFFFF);
        });
        check("nested limit failures leave no opacity or clip behind", output.pixel(5, 5)[3] == 255);
        canvas.render(output.frame, {}, [&](auto& draw, auto) {
            draw.ink(0xFFFFFFFF);
            const bool caught = rejected([&] {
                draw.saved([&] {
                    draw.translate(50, 50);
                    draw.ink(0xFF0000FF);
                    draw.withOpacity(.3, [&] {
                        draw.clipped({0, 0, 10, 10}, [] {
                            throw std::runtime_error("fixture");
                        });
                    });
                });
            });
            const auto ink = draw.brush->GetColor();
            check("exception scopes restore the brush and unwind interleaved clips/layers",
                  caught && ink.g == 1 && ink.b == 1);
            draw.box(0, 0, 10, 10, 0, 0x00FF00FF);
        });
        check("drawing can continue in the same frame after a scoped exception", output.pixel(5, 5)[1] == 255);
        for (double dpi : {1., 1.5, 2.}) {
            Buffer scaled(dpi);
            canvas.render(scaled.frame, {}, [&](auto& draw, auto) {
                draw.withOpacity(.5, [&] {
                    draw.fill(curved, 0xFF0000FF);
                });
            });
            check("retained paths and opacity layers survive DPI/target recreation",
                  scaled.pixel(int(20 * dpi), int(20 * dpi))[3] == 128);
        }
        check("invalid opacity is rejected", rejected([&] {
                  canvas.withOpacity(std::numeric_limits<double>::quiet_NaN(), [] {
                  });
              }));
        Canvas other;
        check("paths cannot cross independent Canvas factories", rejected([&] {
                  other.render(output.frame, {}, [&](auto& draw, auto) {
                      draw.fill(curved, 0xFFFFFFFF);
                  });
              }));
        TextStyle text;
        text.size = 14;
        text.vertical = VerticalAlign::Top;
        const auto natural = canvas.measureText(L"Canvas ", text);
        check("measurement works without a frame and includes optional trailing space",
              natural.width > 0 && natural.height > 0 && natural.widthIncludingTrailingWhitespace > natural.width);
        text.wrap = true;
        const auto wide = canvas.measureText(L"A reusable drawing engine for desktop plugins", text, {400, 200});
        const auto narrow = canvas.measureText(L"A reusable drawing engine for desktop plugins", text, {80, 200});
        check("measurement reflows cached text at each width",
              narrow.height > wide.height && narrow.lineCount > wide.lineCount);
        const auto wideAgain = canvas.measureText(L"A reusable drawing engine for desktop plugins", text, {400, 200});
        check("measuring a narrow layout does not corrupt a later wide layout", wideAgain.height == wide.height);
        text.lineHeight = 23;
        text.baseline = 17;
        const auto lines = canvas.measureText(L"one\ntwo", text);
        check("explicit line height is reflected in measurement",
              lines.lineCount == 2 && std::abs(lines.height - 46) < .01);
        text.wrap = false;
        text.lineHeight = text.baseline = 0;
        Buffer automatic, rtl, ltr;
        const std::wstring arabic = L"2026 مرحبا";
        canvas.render(automatic.frame, {}, [&](auto& draw, auto) {
            draw.text(arabic, text, {0, 0, 96, 60}, 0xFFFFFFFF);
        });
        text.direction = TextDirection::RightToLeft;
        canvas.render(rtl.frame, {}, [&](auto& draw, auto) {
            draw.text(arabic, text, {0, 0, 96, 60}, 0xFFFFFFFF);
        });
        text.direction = TextDirection::LeftToRight;
        canvas.render(ltr.frame, {}, [&](auto& draw, auto) {
            draw.text(arabic, text, {0, 0, 96, 60}, 0xFFFFFFFF);
        });
        check("automatic paragraph direction ignores initial numbers",
              automatic.pixels == rtl.pixels && rtl.pixels != ltr.pixels);
        const auto padded = inset({10, 20, 100, 50}, {7, 5, 11, 9});
        check("padding preserves the original origin",
              padded.x == 17 && padded.y == 25 && padded.width == 82 && padded.height == 36);
        Stack row(padded, Axis::Horizontal, 4);
        const auto end = row.takeEnd(20), start = row.take(30), remaining = row.remaining();
        check("fixed edge slots leave a bounded flexible remainder",
              start.x == 17 && start.width == 30 && end.x == 79 && remaining.x == 51 && remaining.width == 24);
        const auto exhausted = row.take(200), zero = row.take(10);
        check("oversized layout slots clamp and exhaustion remains empty",
              exhausted.width == 24 && zero.width == 0 && zero.x <= 99);
        const auto center = aligned(padded, {20, 10});
        check("alignment centers the requested size within padding",
              center.x == 48 && center.y == 38 && center.width == 20);
        const auto slots = divide({3, 5, 100, 40}, Axis::Horizontal, 3, 7);
        check("fractional equal slots preserve gaps and the final edge",
              std::abs(slots[1].x - (slots[0].x + slots[0].width) - 7) < 1e-9 &&
                  std::abs(slots.back().x + slots.back().width - 103) < 1e-9);
        const auto tiny = divide({0, 0, 5, 0}, Axis::Horizontal, 3, 100);
        const auto overPadding = inset({0, 0, 5, 5}, {100, 100, 100, 100});
        check("tiny areas never produce negative sizes",
              tiny[0].width == 0 && tiny.back().x == 5 && overPadding.width == 0 && overPadding.x == 5);
        check("invalid layout inputs report errors", rejected([] {
                                                         Stack({0, 0, 10, 10}, Axis::Vertical, -1);
                                                     }) &&
                                                         rejected([] {
                                                             divide({}, Axis::Horizontal, 0);
                                                         }));
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return passed ? 0 : 1;
}
