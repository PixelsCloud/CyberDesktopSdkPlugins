#include <CyberCanvas/DataWidgets.h>
#include <CyberCanvas/DesktopWidgets.h>

#include <iostream>
#include <limits>

int main() {
    using namespace cybercanvas;
    int failures = 0;
    const auto check = [&](const char* name, bool value) {
        std::cout << (value ? "PASS " : "FAIL ") << name << '\n';
        failures += !value;
    };
    try {
        ChoiceGroup choices({{1, {0, 0, 70, 24}, L"World"}, {2, {74, 0, 70, 24}, L"中文", false}});
        InputRouter input;
        input.regions(choices.regions({3, 7}));
        input.dispatch({PointerKind::Press, {10, 12}, true});
        check("selection uses shared translated geometry",
              choices.change(input.dispatch({PointerKind::Release, {10, 12}})) == 1);
        input.dispatch({PointerKind::Press, {80, 12}, true});
        check("disabled option cannot activate", !choices.change(input.dispatch({PointerKind::Release, {80, 12}})));
        bool rejected = false;
        try {
            ChoiceGroup invalid({{1, {}, L"A"}, {1, {}, L"B"}});
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        check("duplicate IDs rejected", rejected);
        LineChart chart;
        chart.samples({-1e308, 1e308, std::numeric_limits<double>::quiet_NaN(), 0});
        const auto points = chart.points({5, 9, 120, 60});
        check("gaps and extreme values remain finite",
              points.size() == 2 && points[0][0].y == 69 && points[0][1].y == 9 && points[1][0].y == 39);
        check("unchanged samples do not invalidate geometry", !chart.samples({-1e308, 1e308, NAN, 0}));
        chart.range(std::pair{-10., 10.});
        check("fixed range clamps endpoints", chart.points({0, 0, 10, 10})[0][0].y == 10);
        Canvas canvas;
        TextStyle spacing;
        spacing.size = 14;
        const auto normalWidth = canvas.measureText(L"123456", spacing, {200, 50}).width;
        spacing.characterSpacing = 1;
        check("shared typography preserves custom character spacing",
              canvas.measureText(L"123456", spacing, {200, 50}).width > normalWidth + 10);
        ComPtr<IDWriteFontCollection> collection;
        require(canvas.nativeTextFactory()->GetSystemFontCollection(collection.GetAddressOf()));
        const auto layouts = canvas.stats().textLayouts.count;
        canvas.fontCollection(collection.Get());
        canvas.measureText(L"123456", spacing, {200, 50});
        check("changing font collection invalidates shared text layouts",
              canvas.stats().textLayouts.count == layouts + 1);
        canvas.fontCollection(collection.Get());
        canvas.measureText(L"123456", spacing, {200, 50});
        check("unchanged font collection preserves shared text cache", canvas.stats().textLayouts.count == layouts + 1);
        canvas.fontCollection(nullptr);
        CalendarGrid calendar{{10, 20, 140, 120}, 6};
        check("calendar cells and hit testing share geometry",
              calendar.hit({30, 40}) == 8 && !calendar.hit({150, 40}) && !calendar.hit({30, 140}) &&
                  calendar.cell(41).x == 130);
        SegmentedScale scale{{0, 0, 120, 4}, {0, 50, 100, 200}, {0x00FF00FF, 0xFFFF00FF, 0xFF0000FF}};
        check("severity clamps values and interpolates unequal thresholds",
              scale.position(-1) == 0 && scale.position(300) == 1 && std::abs(scale.position(150) - 5. / 6) < 1e-9);
        Checkbox checkbox(5, {0, 0, 14, 14});
        input.regions({checkbox.region()});
        input.dispatch({PointerKind::Press, {5, 5}, true});
        check("checkbox toggles only on matching release",
              checkbox.change(input.dispatch({PointerKind::Release, {5, 5}}), false) == true);
        checkbox.enabled = false;
        check("disabled checkbox ignores activation", !checkbox.change(PointerResult{.clicked = 5}, false));
        ProgressRing ring;
        MeterGauge gauge;
        for (double dpi : {1., 1.25, 1.5, 2., 3.}) {
            for (bool dark : {false, true}) {
                const auto theme = dark ? WidgetTheme::dark() : WidgetTheme::light();
                const uint32_t w = uint32_t(240 * dpi), h = uint32_t(180 * dpi);
                std::vector<uint8_t> pixels(size_t(w) * h * 4);
                FrameBuffer frame{w, h, w * 4, pixels.size(), pixels.data(), dpi};
                canvas.render(frame, {}, [&](auto&, auto) {
                    choices.draw(canvas, theme, 1);
                    MetricReadout{{0, 30, 120, 110}, L"Memory / 内存", L"78%", L"Measured"}.draw(canvas, theme);
                    MetricReadout{{120, 30, 120, 80}, L"Loading", {}, {}, ContentState::Loading}.draw(canvas, theme);
                    chart.draw(canvas, {8, 145, 220, 25}, theme);
                    gauge.draw(canvas, {170, 85, 48, 48}, .62, theme);
                    ring.draw(canvas, {80, 80, 40, 40}, .75, theme);
                    ring.draw(canvas, {120, 80, 40, 40}, 1, theme);
                    scale.draw(canvas, theme, 150);
                });
                const auto builds = chart.geometryBuilds();
                canvas.render(frame, {}, [&](auto&, auto) {
                    chart.draw(canvas, {8, 145, 220, 25}, theme);
                });
                check("unchanged chart reuses geometry at actual DPI", chart.geometryBuilds() == builds);
            }
        }
        std::vector<uint8_t> pixels(80 * 40 * 4);
        FrameBuffer frame{80, 40, 320, pixels.size(), pixels.data(), 1};
        canvas.render(frame, {}, [&](auto&, auto) {
            ContentNotice{{0, 0, 80, 40}, {}, {}, ContentState::Empty, false}.draw(canvas, WidgetTheme::dark());
        });
        check("transparent empty status adds no pixels", std::all_of(pixels.begin(), pixels.end(), [](auto value) {
                  return value == 0;
              }));
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return failures ? 1 : 0;
}
