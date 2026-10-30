#include <CyberCanvas/Widgets.h>

#include <iostream>
#include <limits>

int main() {
    using namespace cybercanvas;
    bool passed = true;
    const auto check = [&](const char* name, bool value) {
        passed &= value;
        std::cout << (value ? "PASS " : "FAIL ") << name << '\n';
    };
    const auto rejected = [](auto&& action) {
        try {
            action();
        } catch (const std::exception&) {
            return true;
        }
        return false;
    };
    try {
        Canvas canvas;
        InputRouter router;
        WidgetState state;
        Button button(1, {10, 10, 100, 28}, L"Reset", ButtonStyle::Filled);
        Slider slider(2, {10, 70, 180, 4}, -50, 50, 5);
        Toggle toggle(3, {150, 10, 36, 20});
        router.regions({button.region(), slider.region(), toggle.region()});
        check("button draw bounds define its hit area", router.hit({15, 15}) == 1 && router.hit({5, 5}) == 0);
        router.dispatch({PointerKind::Press, {15, 15}, true});
        state.sync(router);
        check("controls expose immediate pressed state", state.visual(1).pressed && state.active());
        state.advance(.07);
        check("pressed feedback advances independently of draw calls", state.visual(1).feedback > .8);
        const auto click = router.dispatch({PointerKind::Release, {15, 15}});
        check("button reports one completed click", button.clicked(click));
        state.sync(router);
        state.advance(.14);
        check("hover feedback settles without permanent frames", !state.active() && state.visual(1).feedback == .55);
        router.dispatch({PointerKind::Leave});
        state.sync(router);
        state.advance(.14);
        check("leave removes settled feedback storage", state.size() == 0);
        button.enabled = false;
        router.regions({button.region()});
        check("disabled buttons do not activate", !router.hit({15, 15}) && !button.clicked(click));
        button.enabled = true;
        router.regions({slider.region({30, 40})});
        auto drag = router.dispatch({PointerKind::Press, {130, 114}, true});
        check("slider hit slop and origin use one geometry", drag.drag == DragPhase::Begin && slider.change(drag) == 0);
        drag = router.dispatch({PointerKind::Move, {500, 114}, true});
        check("captured sliders clamp past the far edge", slider.change(drag) == 50);
        drag = router.dispatch({PointerKind::Move, {-20, 114}, true});
        check("captured sliders clamp past the near edge", slider.change(drag) == -50);
        drag = router.dispatch({PointerKind::Cancel});
        check("cancel never commits a new slider value", !slider.change(drag));
        router.regions({slider.region()});
        drag = router.dispatch({PointerKind::Press, {61, 72}, true});
        check("slider steps are relative to the minimum", slider.change(drag) == -20);
        router.dispatch({PointerKind::Cancel});
        auto rotated = slider.region();
        rotated.shape =
            rotated.shape.transformed(D2D1::Matrix3x2F::Rotation(90) * D2D1::Matrix3x2F::Translation(200, 0));
        router.regions({rotated});
        drag = router.dispatch({PointerKind::Press, {128, 100}, true});
        check("rotated sliders use inverse local coordinates", slider.change(drag) == 0);
        router.dispatch({PointerKind::Cancel});
        router.regions({toggle.region()});
        router.dispatch({PointerKind::Press, {160, 15}, true});
        check("toggle returns the next controlled value",
              toggle.change(router.dispatch({PointerKind::Release, {160, 15}}), false) == true);
        toggle.enabled = false;
        check("disabled toggle ignores click results", !toggle.change({3, 3}, false));
        ProgressBar progress{{10, 120, 180, 6}, 20, 80};
        check("progress supports clamped custom ranges",
              progress.fraction(0) == 0 && progress.fraction(50) == .5 && progress.fraction(100) == 1);
        check("zero width sliders are inert", !Slider(8, {0, 0, 0, 4}).change({8, 0, DragPhase::Begin}));
        check("invalid ranges are rejected", rejected([&] {
                  ProgressBar{{}, 2, 2}.fraction(2);
              }));
        check("nonfinite values are rejected", rejected([&] {
                  progress.fraction(std::numeric_limits<double>::quiet_NaN());
              }));
        Slider invalid(9, {0, 0, 10, 4}, 0, 1, -1);
        check("negative steps are rejected", rejected([&] {
                  invalid.change({9, 0, DragPhase::Begin});
              }));
        check("maximum remains reachable with uneven steps",
              Slider(9, {0, 0, 10, 4}, 0, 10, 3).change({9, 0, DragPhase::Update, {10, 2}}) == 10);
        auto day = WidgetTheme::light(), night = WidgetTheme::dark();
        check("day and night themes expose contrasting tokens",
              day.foreground != night.foreground && day.surface != night.surface);
        std::vector<uint8_t> dayPixels;
        int iconCalls = 0;
        for (double dpr : {1., 1.5, 2., 3.}) {
            for (bool dark : {false, true}) {
                std::vector<uint8_t> pixels(size_t(220 * dpr) * size_t(160 * dpr) * 4);
                FrameBuffer frame{uint32_t(220 * dpr), uint32_t(160 * dpr), uint32_t(220 * dpr) * 4,
                                  pixels.size(),       pixels.data(),       dpr};
                auto theme = dark ? night : day;
                canvas.render(frame, {}, [&](auto& draw, auto) {
                    button.draw(draw, theme, {true, true, .8});
                    slider.draw(draw, theme, 25, {true, false, .55});
                    progress.draw(draw, theme, 50);
                    toggle.draw(draw, theme, true);
                    Button disabled(7, {10, 40, 100, 22}, L"Disabled");
                    disabled.enabled = false;
                    disabled.draw(draw, theme);
                    IconButton icon(8, {190, 10, 20, 20}, "test", L"I");
                    icon.draw(draw, theme, [&](auto&, Rect bounds, std::string_view, uint32_t) {
                        ++iconCalls;
                        return bounds.width == 16 && bounds.height == 16;
                    });
                    IconButton fallback(9, {190, 40, 20, 20}, "missing", L"?");
                    fallback.draw(draw, theme, {});
                });
                check("controls render visible pixels at actual DPI",
                      std::any_of(pixels.begin(), pixels.end(), [](auto p) {
                          return p != 0;
                      }));
                bool premultiplied = true;
                for (size_t i = 0; i < pixels.size(); i += 4) {
                    premultiplied &=
                        pixels[i] <= pixels[i + 3] && pixels[i + 1] <= pixels[i + 3] && pixels[i + 2] <= pixels[i + 3];
                }
                check("controls preserve transparent premultiplied output", premultiplied);
                if (dpr == 1) {
                    if (!dark) {
                        dayPixels = pixels;
                    } else {
                        check("theme changes affect rendered controls", dayPixels != pixels);
                    }
                }
            }
        }
        check("icon content is delegated to the provider", iconCalls == 8);
        std::vector<uint8_t> disabledPixels(120 * 40 * 4);
        FrameBuffer disabledFrame{120, 40, 480, disabledPixels.size(), disabledPixels.data(), 1};
        canvas.render(disabledFrame, {}, [&](auto& draw, auto) {
            Button disabled(10, {0, 0, 120, 40}, L"Disabled", ButtonStyle::Filled);
            disabled.enabled = false;
            disabled.draw(draw, night);
        });
        uint8_t alpha = 0;
        for (size_t i = 3; i < disabledPixels.size(); i += 4) {
            alpha = std::max(alpha, disabledPixels[i]);
        }
        check("disabled content uses one opacity group", alpha >= 100 && alpha <= 103);
        for (bool enabled : {true, false}) {
            const auto layers = canvas.stats().opacityLayers;
            canvas.render(disabledFrame, {}, [&](auto& draw, auto) {
                Button control(10, {10, 4, 100, 32}, L"Control", ButtonStyle::Outline);
                control.enabled = enabled;
                control.draw(draw, night);
            });
            check("idle controls allocate only the disabled opacity group",
                  canvas.stats().opacityLayers - layers == (enabled ? 0 : 1));
        }
        const auto feedbackLayers = canvas.stats().opacityLayers;
        canvas.render(disabledFrame, {}, [&](auto& draw, auto) {
            Button control(10, {10, 4, 100, 32}, L"Control", ButtonStyle::Outline);
            control.draw(draw, night, {true, false, .55});
        });
        check("fractional hover feedback retains its compositing group",
              canvas.stats().opacityLayers == feedbackLayers + 1);
        const auto externalLayers = canvas.stats().opacityLayers;
        bool externalCalled = false;
        canvas.render(disabledFrame, {}, [&](auto& draw, auto) {
            IconButton control(10, {10, 4, 100, 32}, "fixture");
            control.draw(draw, night, [&](auto& painter, Rect area, auto, uint32_t color) {
                externalCalled = true;
                painter.box(area.x, area.y, area.width, area.height, 0, color);
                return true;
            });
        });
        check("custom icon providers retain their original layer scope",
              externalCalled && canvas.stats().opacityLayers == externalLayers + 1);
        const auto beforeZero = canvas.stats().opacityLayers;
        bool zeroCalled = false;
        canvas.render(disabledFrame, {}, [&](auto& draw, auto) {
            draw.withOpacity(0, [&] {
                zeroCalled = true;
                draw.box(0, 0, 120, 40, 0, 0xFFFFFFFF);
            });
        });
        check("public zero-opacity callbacks still execute and remain invisible",
              zeroCalled && canvas.stats().opacityLayers == beforeZero + 1 &&
                  std::all_of(disabledPixels.begin(), disabledPixels.end(), [](uint8_t value) {
                      return value == 0;
                  }));
        WidgetState bounded;
        for (uint32_t id = 1; id < 1000; ++id) {
            router.regions({Button(id, {0, 0, 10, 10}).region()});
            router.dispatch({PointerKind::Move, {5, 5}});
            bounded.sync(router);
            bounded.advance(.00001);
        }
        check("feedback storage stays bounded across dynamic IDs", bounded.size() <= 256);
        router.dispatch({PointerKind::Cancel});
        bounded.sync(router);
        bounded.advance(.14);
        check("dynamic feedback cleans up after its final frame", bounded.size() == 0 && !bounded.active());
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        passed = false;
    }
    return passed ? 0 : 1;
}
