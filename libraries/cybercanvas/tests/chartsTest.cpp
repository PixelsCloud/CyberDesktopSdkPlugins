#include <CyberCanvas/Charts.h>

#include <iostream>
#include <limits>

namespace {
bool passed = true;

void check(const char* name, bool value) {
    passed &= value;
    std::cout << (value ? "PASS " : "FAIL ") << name << '\n';
}

template <class F>
bool rejects(F action) {
    try {
        action();
        return false;
    } catch (const std::exception&) {
        return true;
    }
}
}  // namespace

int main() {
    using namespace cybercanvas;
    try {
        DataChart bars;
        bars.data({{1, L"Loss", -10}, {2, L"Missing", {}}, {3, L"Gain", 20}, {4, L"Zero", 0}});
        check("bar auto axis includes negative and zero", bars.axis().minimum == -10 && bars.axis().maximum == 20);
        check("numeric ticks are bounded", bars.axis().ticks().size() <= 10);
        const NumericAxis tiny{0, std::numeric_limits<double>::denorm_min()};
        check("subnormal ranges retain finite endpoints",
              tiny.ticks() == std::vector<double>({tiny.minimum, tiny.maximum}));
        auto nodes = bars.semantics({0, 0, 400, 200});
        const auto area = nodes[2].bounds;
        auto hit = bars.hit({0, 0, 400, 200}, {area.x + area.width / 2, area.y + area.height / 2});
        check("bar hit matches rendered geometry", hit && hit->id == 3 && hit->value == 20);
        check("missing bar is not interactive", !nodes[1].enabled);
        bars.select(1);
        check("keyboard skips missing data", bars.key(Key::Right) && bars.selected() == 3);
        bars.key(Key::End);
        check("keyboard reaches zero values", bars.selected() == 4);
        bars.append({5, L"New", 40}, 4);
        check("live append evicts oldest while preserving IDs",
              bars.data().size() == 4 && bars.data().front().id == 2 && bars.selected() == 4);
        auto shared = std::make_shared<NumericAxis>(NumericAxis{-100, 100});
        bars.axis(shared);
        check("shared axis overrides automatic range", bars.axis().maximum == 100);
        check("duplicate IDs rejected", rejects([&] {
                  bars.data({{1, L"a", 1}, {1, L"b", 2}});
              }));
        DataChart donut(ChartKind::Donut);
        donut.data({{1, L"A", 1}, {2, L"B", 1}});
        check("donut hole does not hit", !donut.hit({0, 0, 200, 200}, {100, 100}));
        check("donut sector uses angle and radius",
              donut.hit({0, 0, 200, 200}, {175, 100})->id == 1 && donut.hit({0, 0, 200, 200}, {25, 100})->id == 2);
        check("negative donut weights rejected", rejects([&] {
                  donut.data({{1, L"invalid", -1}});
              }));
        DataChart heat(ChartKind::Heatmap);
        heat.columns(2);
        heat.data({{1, L"cold", -5}, {2, L"warm", 10}, {3, L"empty", {}}, {4, L"hot", 20}});
        auto heatNodes = heat.semantics({0, 0, 200, 200});
        check("heatmap rows share cell geometry", heatNodes[2].bounds.y > heatNodes[0].bounds.y);
        heat.select(2);
        check("heatmap keyboard moves by row", heat.key(Key::Down) && heat.selected() == 4);
        check("heatmap value retained in semantics", heatNodes[0].number == -5 && heatNodes[0].name == L"cold");
        Canvas canvas;
        for (double dpi : {1., 1.25, 1.5, 2.}) {
            const uint32_t edge = uint32_t(400 * dpi);
            std::vector<uint8_t> pixels(size_t(edge) * edge * 4);
            for (const bool dark : {false, true}) {
                WidgetTheme theme;
                theme.surface = dark ? 0x18212aff : 0xf3f5f8ff;
                theme.foreground = dark ? 0xffffffff : 0x18212aff;
                canvas.render({edge, edge, edge * 4, pixels.size(), pixels.data(), dpi}, {}, [&](Canvas& target, Rect) {
                    bars.draw(target, {0, 0, 400, 120}, theme);
                    donut.draw(target, {0, 125, 180, 180}, theme);
                    heat.draw(target, {200, 125, 180, 180}, theme);
                    donut.drawLegend(target, {10, 310, 300, 80}, theme);
                });
                size_t visible = 0;
                for (size_t i = 3; i < pixels.size(); i += 4) {
                    visible += pixels[i] > 0;
                }
                check("charts render in both themes at actual DPI", visible > 1000);
            }
        }
        DataChart empty;
        {
            DataChart compact;
            compact.data({{1, L"CPU", 100}});
            int labels = 0;
            compact.formatter([&](double value) {
                ++labels;
                return std::to_wstring(int(value));
            });
            std::vector<uint8_t> pixels(200 * 100 * 4);
            canvas.render({200, 100, 800, pixels.size(), pixels.data(), 1}, {}, [&](Canvas& draw, Rect) {
                compact.draw(draw, {0, 0, 200, 24}, WidgetTheme{}, false);
            });
            check("tiny plot omits overlapping axis labels", labels == 0);
            canvas.render({200, 100, 800, pixels.size(), pixels.data(), 1}, {}, [&](Canvas& draw, Rect) {
                compact.draw(draw, {0, 0, 200, 80}, WidgetTheme{}, false);
            });
            check("compact plot keeps a readable tick count", labels >= 2 && labels <= 4);
        }
        check("empty chart has stable axis and no hits",
              empty.axis().maximum == 1 && !empty.hit({0, 0, 100, 100}, {50, 50}));
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return passed ? 0 : 1;
}
