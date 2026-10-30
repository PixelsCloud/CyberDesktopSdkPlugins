#include <CyberCanvas/Scene.h>

#include <iostream>

namespace {
bool passed = true;

void check(const char* name, bool value) {
    passed &= value;
    std::cout << (value ? "PASS " : "FAIL ") << name << '\n';
}
}  // namespace

int main() {
    using namespace cybercanvas;
    try {
        Canvas canvas;
        View column;
        column.layout.padding = {10, 10, 10, 10};
        Scene scene(column);
        View text;
        text.kind = ViewKind::Text;
        text.text = L"A measured title / 真实文字测量";
        text.theme.fontSize = 12;
        const auto title = scene.add(0, text);
        View slider;
        slider.kind = ViewKind::Slider;
        slider.key = 10;
        slider.layout.height = Length::points(4);
        slider.layout.shrink = 0;
        slider.maximum = 100;
        slider.step = 1;
        const auto volume = scene.add(0, slider);
        View button;
        button.kind = ViewKind::Button;
        button.key = 20;
        button.text = L"Apply";
        button.layout.height = Length::points(28);
        button.layout.shrink = 0;
        const auto apply = scene.add(0, button);
        View row;
        row.kind = ViewKind::Row;
        row.layout.height = Length::points(25);
        row.layout.shrink = 0;
        const auto group = scene.add(0, row);
        button.key = 21;
        button.theme.foreground = 0xAA11FFFF;
        const auto child = scene.add(group, button);
        scene.arrange(canvas, {0, 0, 200, 180});
        const auto paint = [&](double dpr = 1) {
            std::vector<uint8_t> pixels(size_t(200 * dpr) * size_t(180 * dpr) * 4);
            FrameBuffer frame{uint32_t(200 * dpr), uint32_t(180 * dpr), uint32_t(200 * dpr) * 4,
                              pixels.size(),       pixels.data(),       dpr};
            canvas.render(frame, {}, [&](auto&, auto) {
                scene.draw(canvas);
            });
            return pixels;
        };
        const auto original = paint();
        check("drawing produces pixels and clears the paint request",
              !scene.needsPaint() && std::any_of(original.begin(), original.end(), [](auto v) {
                  return v != 0;
              }));
        const auto initial = scene.stats();
        check("unchanged arrange skips Yoga and text measurement",
              !scene.arrange(canvas, {0, 0, 200, 180}) && scene.stats().measurements == initial.measurements);
        int invalidations = 0;
        scene.onInvalidate([&] {
            ++invalidations;
        });
        scene.value(volume, 60);
        scene.value(volume, 70);
        check("value changes coalesce notifications and keep layout",
              invalidations == 1 && scene.needsPaint() && !scene.needsLayout());
        check("paint-only state changes produce different pixels", paint() != original);
        scene.value(volume, 70);
        check("setting the same value keeps the scene idle", !scene.needsPaint() && invalidations == 1);
        auto theme = WidgetTheme::light();
        scene.theme(theme);
        check("color-only theme changes skip layout", !scene.needsLayout() && scene.needsPaint());
        check("unspecified theme tokens inherit and overrides survive",
              scene.resolvedTheme(title).foreground == theme.foreground &&
                  scene.resolvedTheme(child).foreground == 0xAA11FFFF && scene.resolvedTheme(title).font.size == 12);
        scene.arrange(canvas, {0, 0, 200, 180});
        paint();
        check("theme colors do not remeasure text", scene.stats().measurements == initial.measurements);
        const auto track = scene.bounds(volume);
        const auto point = Point{track.x + track.width * .75, track.y + track.height * .5};
        auto event = scene.input({PointerKind::Press, {point.x + 7, point.y + 9}, true}, {7, 9});
        check("automatic hit regions share arranged slider geometry",
              event.key == 10 && event.value && *event.value == 75 && scene.router().captured());
        event = scene.input({PointerKind::Move, {-100, point.y}, true}, {7, 9});
        check("captured drags outside the view clamp to the endpoint", event.value && *event.value == 0);
        scene.input({PointerKind::Release, {-100, point.y}}, {7, 9});
        check("outside release ends capture", !scene.router().captured());
        scene.input({PointerKind::Press, point, true});
        scene.text(title, L"A replacement title that wraps");
        check("content invalidation cancels old geometry immediately",
              !scene.router().captured() && scene.needsLayout());
        scene.arrange(canvas, {0, 0, 200, 180});
        paint();
        scene.targets();
        const auto area = scene.bounds(child);
        scene.input({PointerKind::Press, {area.x + 3, area.y + 3}, true});
        scene.enabled(group, false);
        check("disabling an ancestor cancels pending input", scene.router().pressed() == 0);
        event = scene.input({PointerKind::Release, {area.x + 3, area.y + 3}});
        check("disabled descendants cannot activate", !event.clicked);
        scene.visible(group, false);
        scene.arrange(canvas, {0, 0, 200, 180});
        check("hidden ancestors collapse descendants", scene.bounds(child).width == 0);
        for (double dpr : {1., 1.25, 1.5, 2., 3.}) {
            scene.arrange(canvas, {0, 0, 200, 180}, dpr);
            paint(dpr);
            const auto current = scene.stats();
            check("stable DPI layouts remain cached",
                  !scene.arrange(canvas, {0, 0, 200, 180}, dpr) && current.layouts == scene.stats().layouts);
        }
        scene.cancel();
        scene.advance(.2);
        paint();
        check("settled feedback does not request more frames",
              !scene.active() && !scene.advance(.016) && !scene.needsPaint());
        theme.font.size = 18;
        scene.theme(theme);
        check("font metrics invalidate layout", scene.needsLayout());
        check("stable business keys find nodes", scene.find(10) == volume && scene.find(999) == 0);
        bool rejected = false;
        try {
            scene.add(apply, text);
        } catch (const std::logic_error&) {
            rejected = true;
        }
        check("leaves cannot acquire child layout nodes", rejected);

        View decoration;
        decoration.paint = [](Canvas& draw, Rect, const WidgetTheme&) {
            draw.translate(1000, 1000);
        };
        Scene isolated(decoration);
        button.appearance = ButtonStyle::Filled;
        isolated.add(0, button);
        isolated.arrange(canvas, {0, 0, 40, 40});
        std::vector<uint8_t> pixels(40 * 40 * 4);
        FrameBuffer frame{40, 40, 160, pixels.size(), pixels.data(), 1};
        canvas.render(frame, {}, [&](auto&, auto) {
            isolated.draw(canvas);
        });
        check("custom drawing cannot leak transforms to other nodes",
              std::any_of(pixels.begin(), pixels.end(), [](auto value) {
                  return value != 0;
              }));
        rejected = false;
        try {
            isolated.add(0, button);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        check("duplicate business keys are rejected", rejected);
        View fixed;
        fixed.kind = ViewKind::Button;
        fixed.layout.width = Length::points(35);
        fixed.layout.height = Length::points(25);
        fixed.text = L"0:59";
        const auto clock = isolated.add(0, fixed);
        isolated.arrange(canvas, {0, 0, 40, 80});
        isolated.text(clock, L"0:58");
        check("text in a fixed-size control only needs paint", !isolated.needsLayout());

        View dynamicRoot;
        dynamicRoot.inheritGap = false;
        dynamicRoot.layout.padding = {8, 8, 8, 8};
        Scene dynamic(dynamicRoot);
        std::vector<int> order;
        View panel;
        panel.clickable = true;
        panel.key = 300;
        panel.inheritGap = false;
        panel.layout.height = Length::points(72);
        panel.layout.shrink = 0;
        panel.paint = [&](auto&, auto, auto&) {
            order.push_back(1);
        };
        panel.theme.foreground = 0xFF2211FF;
        const auto firstPanel = dynamic.add(0, panel);
        View movable = fixed;
        movable.layout = {};
        movable.layout.shrink = 0;
        movable.key = 100;
        movable.selected = true;
        movable.paint = [&](auto&, auto, auto&) {
            order.push_back(2);
        };
        const auto moving = dynamic.add(firstPanel, movable);
        panel.theme.foreground = 0x11CCFFFF;
        panel.key = 400;
        panel.paint = [&](auto&, auto, auto&) {
            order.push_back(3);
        };
        const auto secondPanel = dynamic.add(0, panel);
        movable.key = 200;
        movable.paint = [&](auto&, auto, auto&) {
            order.push_back(4);
        };
        const auto secondButton = dynamic.add(secondPanel, movable);
        const auto dynamicPaint = [&] {
            std::vector<uint8_t> output(200 * 180 * 4);
            FrameBuffer frame{200, 180, 800, output.size(), output.data(), 1};
            order.clear();
            canvas.render(frame, {}, [&](auto&, auto) {
                dynamic.draw(canvas);
            });
        };
        dynamic.arrange(canvas, {0, 0, 200, 180});
        dynamicPaint();
        const auto measurements = dynamic.stats().measurements;
        int structuralRedraws = 0;
        dynamic.onInvalidate([&] {
            ++structuralRedraws;
        });
        check("a same-position move does not request a frame",
              !dynamic.move(moving, firstPanel) && !dynamic.needsPaint() && structuralRedraws == 0);
        auto areaBefore = dynamic.bounds(moving);
        dynamic.input({PointerKind::Press, {areaBefore.x + 4, areaBefore.y + 4}, true});
        dynamic.move(moving, secondPanel, secondButton);
        check("moving a pressed subtree cancels pending gestures", dynamic.router().pressed() == 0);
        check("moves preserve business identity and controlled state",
              dynamic.find(100) == moving && dynamic.view(moving).selected && dynamic.parent(moving) == secondPanel);
        check("theme inheritance follows the new ancestor", dynamic.resolvedTheme(moving).foreground == 0x11CCFFFF);
        dynamic.arrange(canvas, {0, 0, 200, 180});
        dynamicPaint();
        check("drawing follows current tree order rather than allocation order", order == std::vector<int>{1, 3, 2, 4});
        check("a structural move does not remeasure every unchanged leaf",
              measurements > 0 && dynamic.stats().measurements < measurements * 2);
        const auto movedBounds = dynamic.bounds(moving);
        dynamic.input({PointerKind::Press, {movedBounds.x + 4, movedBounds.y + 4}, true});
        auto movedClick = dynamic.input({PointerKind::Release, {movedBounds.x + 4, movedBounds.y + 4}});
        check("moved controls receive input at their new geometry", movedClick.clicked && movedClick.key == 100);
        check("moves reject a destination inside the same subtree", [&] {
            try {
                dynamic.move(secondPanel, secondPanel);
            } catch (const std::invalid_argument&) {
                return dynamic.parent(secondPanel) == 0;
            }
            return false;
        }());
        dynamic.remove(firstPanel);
        dynamic.remove(secondButton);
        check("removal only invalidates deleted handles",
              !dynamic.contains(firstPanel) && !dynamic.contains(secondButton) && dynamic.contains(moving) &&
                  dynamic.find(200) == 0 && dynamic.size() == 3);
        const auto replacement = dynamic.add(secondPanel, movable);
        check("a removed business key can be reused with a fresh handle",
              replacement > secondButton && dynamic.find(200) == replacement && !dynamic.contains(secondButton));
        bool staleRejected = false;
        try {
            dynamic.text(secondButton, L"stale");
        } catch (const std::out_of_range&) {
            staleRejected = true;
        }
        check("stale handles cannot modify replacement controls",
              staleRejected && dynamic.view(replacement).text == fixed.text);
        dynamic.arrange(canvas, {0, 0, 200, 180});
        dynamicPaint();
        const int beforeClear = structuralRedraws;
        dynamic.clear(secondPanel);
        check("clearing children preserves the container and coalesces notifications",
              dynamic.contains(secondPanel) && dynamic.children(secondPanel).empty() && dynamic.size() == 2 &&
                  structuralRedraws == beforeClear + 1);
        dynamic.arrange(canvas, {0, 0, 200, 180});
        dynamicPaint();
        dynamic.clear(secondPanel);
        check("clearing an empty container keeps the scene idle", !dynamic.needsPaint());
        bool handlesFresh = true;
        auto lastHandle = replacement;
        for (int i = 0; i < 5000; ++i) {
            const auto temporary = dynamic.add(secondPanel, fixed);
            handlesFresh &= temporary > lastHandle;
            lastHandle = temporary;
            dynamic.remove(temporary);
        }
        check("long-lived scenes reclaim removed capacity", handlesFresh && dynamic.size() == 2);
        auto resource = std::make_shared<int>(42);
        std::weak_ptr<int> lifetime = resource;
        View resourceView = fixed;
        resourceView.paint = [resource](auto&, auto, auto&) {
        };
        const auto resourceNode = dynamic.add(secondPanel, std::move(resourceView));
        resource.reset();
        dynamic.remove(resourceNode);
        check("removing a node releases its captured drawing resources", lifetime.expired());
        dynamic.clear();
        check("clearing the scene retains its root", dynamic.size() == 1 && dynamic.contains(0));
        dynamic.arrange(canvas, {0, 0, 200, 180});
        dynamicPaint();
        bool rootRejected = false;
        try {
            dynamic.remove(0);
        } catch (const std::invalid_argument&) {
            rootRejected = true;
        }
        check("root deletion is rejected without invalidation", rootRejected && !dynamic.needsPaint());
        View hiddenPanel;
        hiddenPanel.layout.visible = false;
        hiddenPanel.enabled = false;
        const auto hidden = dynamic.add(0, hiddenPanel);
        const auto childAfterClear = dynamic.add(0, fixed);
        dynamic.move(childAfterClear, hidden);
        dynamic.arrange(canvas, {0, 0, 200, 180});
        check("moved descendants inherit hidden ancestor layout", dynamic.bounds(childAfterClear).width == 0);
        dynamic.visible(hidden, true);
        dynamic.arrange(canvas, {0, 0, 200, 180});
        const auto disabledBounds = dynamic.bounds(childAfterClear);
        const Point disabledPoint{disabledBounds.x + 2, disabledBounds.y + 2};
        dynamic.input({PointerKind::Press, disabledPoint, true});
        check("moved descendants inherit disabled ancestor input",
              !dynamic.input({PointerKind::Release, disabledPoint}).clicked);
        dynamic.move(childAfterClear, 0);
        dynamic.arrange(canvas, {0, 0, 200, 180});
        const auto restoredBounds = dynamic.bounds(childAfterClear);
        const Point restoredPoint{restoredBounds.x + 2, restoredBounds.y + 2};
        dynamic.input({PointerKind::Press, restoredPoint, true});
        check("moving out restores input without changing the local enabled value",
              dynamic.input({PointerKind::Release, restoredPoint}).clicked && dynamic.view(childAfterClear).enabled);
        View protectedPaint = fixed;
        bool mutationRejected = false;
        protectedPaint.paint = [&](auto&, auto, auto&) {
            try {
                dynamic.clear();
            } catch (const std::logic_error&) {
                mutationRejected = true;
            }
        };
        dynamic.add(0, protectedPaint);
        dynamic.arrange(canvas, {0, 0, 200, 180});
        dynamicPaint();
        check("structural mutation during drawing is rejected", mutationRejected && dynamic.contains(childAfterClear));
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        passed = false;
    }
    return passed ? 0 : 1;
}
