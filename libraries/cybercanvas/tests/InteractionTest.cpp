#include <CyberCanvas/Canvas.h>
#include <CyberCanvas/Interaction.h>

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
        auto rect = HitShape::rectangle({0, 0, 20, 10});
        check("rectangles use half-open bounds", rect.contains({0, 0}) && !rect.contains({20, 5}));
        auto ellipse = HitShape::ellipse({0, 0, 20, 10});
        check("ellipse rejects bounding-box corners", ellipse.contains({10, 5}) && !ellipse.contains({1, 1}));
        auto transformed = rect.transformed(D2D1::Matrix3x2F::Rotation(90) * D2D1::Matrix3x2F::Translation(40, 30));
        check("rotation and translation match Direct2D",
              transformed.contains({35, 40}) && !transformed.contains({5, 5}));
        const auto local = transformed.local({35, 40});
        check("drag positions invert the drawing transform",
              local && std::abs(local->x - 10) < .001 && std::abs(local->y - 5) < .001);
        check("singular transforms are not interactive",
              !rect.transformed(D2D1::Matrix3x2F::Scale(0, 1)).contains({0, 1}));
        check("nonfinite positions are ignored", !rect.contains({std::numeric_limits<double>::quiet_NaN(), 5}));
        check("DPI conversion happens once", rect.contains(logicalPointer({30, 10}, 2)) && !rect.contains({30, 10}));
        check("invalid DPI is rejected", rejected([&] {
                  logicalPointer({}, 0);
              }));
        check("nonfinite bounds are rejected", rejected([&] {
                  HitShape::rectangle({0, 0, std::numeric_limits<double>::quiet_NaN(), 10});
              }));
        auto path = canvas.path(FillRule::Alternate);
        path.moveTo({0, 0}).lineTo({40, 0}).lineTo({40, 40}).lineTo({0, 40}).close();
        path.moveTo({10, 10}).lineTo({30, 10}).lineTo({30, 30}).lineTo({10, 30}).close();
        const auto shape = HitShape::path(path, canvas.factory.Get());
        check("native path holes are respected", shape.contains({5, 5}) && !shape.contains({20, 20}));
        InputRouter router;
        router.regions(
            {{1, HitShape::rectangle({0, 0, 100, 100})}, {2, rect}, {3, ellipse, PointerBehavior::Button, 2}});
        check("higher priority wins", router.hit({10, 5}) == 3);
        check("later registrations win ties", router.hit({1, 1}) == 2);
        router.regions({{1, rect, PointerBehavior::Button, 0, false},
                        {2, rect, PointerBehavior::Button, 0, true, {HitShape::rectangle({5, 0, 5, 10})}}});
        check("disabled targets and clips are respected", router.hit({2, 5}) == 0 && router.hit({7, 5}) == 2);
        check("duplicate IDs are rejected atomically", rejected([&] {
                                                           router.regions({{2, rect}, {2, rect}});
                                                       }) &&
                                                           router.hit({7, 5}) == 2);
        router.regions({{1, rect}});
        check("hover transitions request a redraw",
              router.dispatch({PointerKind::Move, {5, 5}}).changed && router.hovered() == 1);
        check("stationary hover does not redraw", !router.dispatch({PointerKind::Move, {5, 5}}).changed);
        router.dispatch({PointerKind::Press, {5, 5}, true});
        check("buttons show pressed state without capturing tile drag", router.pressed() == 1 && !router.captured());
        check("release activates a stationary button",
              router.dispatch({PointerKind::Release, {5, 5}}).clicked == 1 && !router.pressed());
        check("release without press cannot activate", !router.dispatch({PointerKind::Release, {5, 5}}).clicked);
        router.dispatch({PointerKind::Press, {5, 5}, true});
        router.dispatch({PointerKind::Move, {15, 5}, true});
        check("button drag stays canceled after returning", !router.dispatch({PointerKind::Release, {5, 5}}).clicked);
        router.dispatch({PointerKind::Press, {5, 5}, true});
        router.dispatch({PointerKind::Leave});
        check("leave cancels buttons", !router.dispatch({PointerKind::Release, {5, 5}}).clicked);
        router.dispatch({PointerKind::Press, {5, 5}, false});
        check("right button cannot press controls", !router.pressed());
        router.regions({{9, transformed, PointerBehavior::Drag}});
        auto result = router.dispatch({PointerKind::Press, {35, 40}, true});
        check("drag starts and claims capture immediately",
              result.drag == DragPhase::Begin && result.target == 9 && router.captured());
        router.dispatch({PointerKind::Leave});
        check("captured sliders survive leave", router.pressed() == 9 && router.captured() && !router.hovered());
        result = router.dispatch({PointerKind::Move, {0, 80}, true});
        check("captured motion outside returns local coordinates",
              result.target == 9 && result.drag == DragPhase::Update && result.local.x > 40);
        result = router.dispatch({PointerKind::Release, {0, 80}});
        check("outside release ends capture without clicking",
              result.drag == DragPhase::End && !result.clicked && !router.captured());
        router.dispatch({PointerKind::Press, {35, 40}, true});
        result = router.dispatch({PointerKind::Cancel});
        check("cancel clears hover and capture",
              result.drag == DragPhase::Cancel && result.target == 9 && !router.pressed() && !router.hovered());
        router.dispatch({PointerKind::Press, {35, 40}, true});
        check("missing left button cancels lost capture",
              router.dispatch({PointerKind::Move, {35, 40}}).drag == DragPhase::Cancel && !router.captured());
        router.dispatch({PointerKind::Press, {35, 40}, true});
        result = router.regions({});
        check("removing a target cancels and clears its state",
              result.drag == DragPhase::Cancel && result.target == 9 && !router.captured() && !router.pressed());
        InteractionFeedback a, b;
        a.target(1);
        b.target(1);
        a.advance(.07);
        b.advance(.035);
        b.advance(.035);
        check("feedback depends on time, not frame count", std::abs(a.value() - b.value()) < 1e-12);
        const auto current = a.value();
        a.target(0);
        check("retargeting preserves the visible value", a.value() == current);
        a.advance(.14);
        check("completed feedback becomes idle", !a.active() && a.value() == 0);
        check("invalid feedback time is rejected", rejected([&] {
                  a.advance(-1);
              }));
        check("nonfinite feedback target is rejected", rejected([&] {
                  a.target(std::numeric_limits<double>::quiet_NaN());
              }));
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        passed = false;
    }
    return passed ? 0 : 1;
}
