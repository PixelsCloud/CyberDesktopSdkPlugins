#include <CyberCanvas/Components.h>

#include <iostream>

namespace {
bool within(cybercanvas::Rect inner, cybercanvas::Rect outer) {
    return inner.width >= 0 && inner.height >= 0 && inner.x >= outer.x - 1 && inner.y >= outer.y - 1 &&
           inner.x + inner.width <= outer.x + outer.width + 1 && inner.y + inner.height <= outer.y + outer.height + 1;
}
}  // namespace

int main() {
    using namespace cybercanvas;
    bool passed = true;
    const auto check = [&](const char* name, bool value) {
        passed &= value;
        std::cout << (value ? "PASS " : "FAIL ") << name << '\n';
    };
    try {
        Canvas canvas;
        auto theme = WidgetTheme::dark();
        theme.font.size = 12;
        theme.font.wrap = true;
        const std::wstring longText =
            L"A long title with English words, 中文标题，以及更多内容，用来验证自动换行和测量。";
        FlexLayout layout;
        const auto text = layout.add(0, {}, measuredText(canvas, longText, theme.font));
        layout.calculate({0, 0, 320, 300});
        const double wideHeight = layout.bounds(text).height;
        layout.calculate({0, 0, 120, 300});
        check("DirectWrite measurement increases height when text wraps", layout.bounds(text).height > wideHeight);
        const auto header = titleActions({5, 7, 180, 28}, 2);
        check("header preserves a nonempty title area", header.title.width > 0 && header.title.height == 28);
        check("header reserves space before its action buttons",
              header.title.x + header.title.width <= header.actions[0].x &&
                  header.actions[0].x + header.actions[0].width <= header.actions[1].x);
        const auto narrow = wrappingRow({0, 0, 130, 100}, {{60, 22}, {60, 22}, {60, 22}}, 4);
        const auto wide = wrappingRow({0, 0, 200, 100}, {{60, 22}, {60, 22}, {60, 22}}, 4);
        check("wrapping component exposes its used content height", narrow.height == 48 && wide.height == 22);
        for (double dpr : {1., 1.25, 1.5, 2., 3.}) {
            for (double width : {72., 144., 288., 480.}) {
                const Rect bounds{8, 8, width, 90};
                std::vector<uint8_t> pixels(size_t(std::ceil((width + 16) * dpr)) * size_t(std::ceil(130 * dpr)) * 4);
                const uint32_t w = uint32_t(std::ceil((width + 16) * dpr)), h = uint32_t(std::ceil(130 * dpr));
                FrameBuffer frame{w, h, w * 4, pixels.size(), pixels.data(), dpr};
                LabelledSlider slider{bounds, longText, Slider(9, {0, 0, 0, 4}, 0, 100, 1)};
                canvas.render(frame, {}, [&](auto& draw, auto) {
                    const auto parts = slider.layout(draw, theme);
                    check("label and slider stay inside their shared bounds",
                          within(parts.label, bounds) && within(parts.slider.bounds, bounds));
                    check("wrapped label cannot overlap its slider",
                          parts.label.y + parts.label.height <= parts.slider.bounds.y);
                    InputRouter input;
                    input.regions({slider.region(draw, theme)});
                    const auto track = parts.slider.bounds;
                    const Point point{track.x + track.width * .75, track.y + track.height * .5};
                    const auto result = input.dispatch({PointerKind::Press, point, true});
                    const auto value = slider.change(draw, theme, result);
                    check("composite drawing and input share slider geometry", value && std::abs(*value - 75) <= 1);
                    slider.draw(draw, theme, 75, {true, true, .8});
                    TitleActions title{{8, 104, width, 24}, longText, {IconButton(10, {}, "check", L"OK")}};
                    WidgetState state;
                    title.draw(draw, theme, state, {});
                    IconLabel{{8, 100, width, 20}, "check", L"A long label"}.draw(draw, theme, {});
                });
                check("composites produce pixels at the actual output DPI",
                      std::any_of(pixels.begin(), pixels.end(), [](auto p) {
                          return p != 0;
                      }));
            }
        }
        const auto sections = textSections(canvas, {0, 0, 120, 80}, longText, theme.font, 34, 13, 3);
        check("article content respects the title height limit", sections.title.height <= 34);
        check("article footer stays below the title and body",
              sections.title.y + sections.title.height <= sections.body.y &&
                  sections.body.y + sections.body.height <= sections.footer.y);
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        passed = false;
    }
    return passed ? 0 : 1;
}
