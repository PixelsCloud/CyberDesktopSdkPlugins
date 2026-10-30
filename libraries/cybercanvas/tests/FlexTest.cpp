#include <CyberCanvas/Flex.h>

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
bool near(double a, double b) {
    return std::abs(a - b) < .01;
}
}  // namespace

int main() {
    using namespace cybercanvas;
    bool passed = true;
    const auto check = [&](const char* name, bool value) {
        passed &= value;
        std::cout << (value ? "PASS " : "FAIL ") << name << '\n';
    };
    const auto rejects = [](auto&& fn) {
        try {
            fn();
        } catch (const std::exception&) {
            return true;
        }
        return false;
    };
    try {
        FlexStyle row;
        row.axis = Axis::Horizontal;
        row.padding = {10, 5, 10, 5};
        row.gap = 8;
        FlexLayout tree(row);
        FlexStyle fixed;
        fixed.width = Length::points(40);
        fixed.shrink = 0;
        auto first = tree.add(0, fixed);
        FlexStyle grow;
        grow.grow = 1;
        grow.basis = Length::points(0);
        auto second = tree.add(0, grow);
        grow.grow = 2;
        auto third = tree.add(0, grow);
        check("layouts cannot be queried before calculation", rejects([&] {
                  tree.bounds(first);
              }));
        check("first layout reports a change", tree.calculate({20, 30, 300, 80}));
        check("padding and world origin are included",
              near(tree.bounds(first).x, 30) && near(tree.bounds(first).y, 35));
        check("remaining width follows grow weights",
              near(tree.bounds(second).width, 75) && near(tree.bounds(third).width, 149));
        check("cross-axis stretch respects padding", near(tree.bounds(second).height, 70));
        check("unchanged layouts reuse the result", !tree.calculate({20, 30, 300, 80}));
        grow.maximum = Size{80, 1000};
        tree.style(third, grow);
        check("dirty layout results cannot leak", rejects([&] {
                  tree.bounds(third);
              }));
        tree.calculate({20, 30, 300, 80});
        check("maximum constraints redistribute free space",
              near(tree.bounds(third).width, 80) && near(tree.bounds(second).width, 144));
        tree.calculate({20, 30, 300, 80}, 1, true);
        check("RTL reverses row placement", tree.bounds(first).x > tree.bounds(second).x);
        FlexStyle wrapped;
        wrapped.axis = Axis::Horizontal;
        wrapped.wrap = true;
        wrapped.gap = 6;
        FlexLayout flow(wrapped);
        FlexStyle item;
        item.width = Length::points(60);
        item.height = Length::points(20);
        item.shrink = 0;
        auto a = flow.add(0, item), b = flow.add(0, item), c = flow.add(0, item);
        flow.calculate({0, 0, 140, 100});
        check("wrapping packs items before starting another row",
              near(flow.bounds(a).y, flow.bounds(b).y) && near(flow.bounds(c).y, 26));
        flow.calculate({0, 0, 210, 100});
        check("resizing recomputes wrap rows", near(flow.bounds(c).y, 0));
        item.visible = false;
        flow.style(b, item);
        flow.calculate({0, 0, 140, 100});
        check("hidden items leave no layout space", flow.bounds(b).width == 0 && near(flow.bounds(c).y, 0));
        FlexLayout measured;
        int calls = 0;
        double naturalHeight = 20;
        const auto leaf = measured.add(0, {}, [&](MeasureConstraint input) {
            ++calls;
            return Size{input.widthMode == MeasureMode::Undefined ? 100 : input.width, naturalHeight};
        });
        measured.calculate({0, 0, 200, 100});
        check("measurement supplies intrinsic height", near(measured.bounds(leaf).height, 20));
        const int before = calls;
        measured.calculate({0, 0, 200, 100});
        check("stable measurement is not repeated", calls == before);
        naturalHeight = 35;
        measured.invalidate(leaf);
        measured.calculate({0, 0, 200, 100});
        check("content invalidation remeasures a leaf", calls > before && near(measured.bounds(leaf).height, 35));
        check("measured leaves reject children", rejects([&] {
                  measured.add(leaf);
              }));
        check("containers reject leaf measurement", rejects([&] {
                  measured.measure(0, [](auto) {
                      return Size{1, 1};
                  });
              }));
        measured.measure(leaf, [](auto) -> Size {
            throw std::runtime_error("measurement error");
        });
        check("measurement exceptions cross the C adapter safely", rejects([&] {
                  measured.calculate({0, 0, 200, 100});
              }));
        measured.measure(leaf, [](auto) {
            return Size{50, 25};
        });
        measured.calculate({0, 0, 200, 100});
        check("failed measurements can recover", near(measured.bounds(leaf).height, 25));
        FlexLayout clipped;
        FlexStyle wide;
        wide.minimum = {200, 200};
        const auto overflow = clipped.add(0, wide);
        clipped.calculate({3, 4, 100, 50});
        check("minimum sizes retain their meaning", clipped.bounds(overflow).width == 200);
        check("overflow exposes a separate clipped rectangle",
              clipped.clippedBounds(overflow).width == 100 && clipped.clippedBounds(overflow).height == 50);
        FlexStyle fractions;
        fractions.axis = Axis::Horizontal;
        FlexLayout pixels(fractions);
        FlexStyle equal;
        equal.grow = 1;
        equal.basis = Length::points(0);
        const auto x = pixels.add(0, equal), y = pixels.add(0, equal), z = pixels.add(0, equal);
        for (double dpr : {1., 1.25, 1.5, 2., 3.}) {
            pixels.calculate({0, 0, 101, 20}, dpr);
            const auto one = pixels.bounds(x), two = pixels.bounds(y), three = pixels.bounds(z);
            check("DPI rounding aligns adjoining edges",
                  near(one.x + one.width, two.x) && near(two.x + two.width, three.x));
            check("DPI rounding aligns to physical pixels", near(two.x * dpr, std::round(two.x * dpr)));
        }
        check("invalid geometry is rejected", rejects([&] {
                  pixels.calculate({0, 0, -1, 10});
              }));
        check("invalid DPI is rejected", rejects([&] {
                  pixels.calculate({0, 0, 10, 10}, 0);
              }));
        check("invalid node IDs are rejected", rejects([&] {
                  pixels.style(900, {});
              }));
        auto invalid = equal;
        invalid.minimum.width = 20;
        invalid.maximum = Size{10, 10};
        check("inverted limits are rejected", rejects([&] {
                  pixels.add(0, invalid);
              }));
        invalid = equal;
        invalid.gap = std::numeric_limits<double>::quiet_NaN();
        check("nonfinite styles are rejected", rejects([&] {
                  pixels.add(0, invalid);
              }));
        auto moved = std::move(measured);
        moved.invalidate(leaf);
        moved.calculate({0, 0, 200, 100});
        check("moving a tree preserves measurement contexts", near(moved.bounds(leaf).height, 25));
        moved.measure(leaf, {});
        moved.calculate({0, 0, 200, 100});
        check("removing measurement discards the cached intrinsic size", near(moved.bounds(leaf).height, 0));
        moved.measure(leaf, [](auto) {
            return Size{50, 30};
        });
        moved.calculate({0, 0, 200, 100});
        check("adding measurement updates an already calculated leaf", near(moved.bounds(leaf).height, 30));

        FlexLayout dynamic(row);
        FlexStyle panel;
        panel.width = Length::points(100);
        panel.height = Length::points(150);
        panel.shrink = 0;
        const auto left = dynamic.add(0, panel);
        const auto moving = dynamic.add(left, {}, [](auto) {
            return Size{50, 20};
        });
        const auto right = dynamic.add(0, panel);
        const auto sibling = dynamic.add(right, {}, [](auto) {
            return Size{50, 30};
        });
        dynamic.calculate({20, 30, 300, 180});
        check("subtrees can move under a newer parent", dynamic.move(moving, right, sibling));
        dynamic.calculate({20, 30, 300, 180});
        check("moved nodes resolve world coordinates after their new parent",
              near(dynamic.bounds(moving).x, dynamic.bounds(right).x) &&
                  near(dynamic.bounds(sibling).y - dynamic.bounds(moving).y, 20));
        check("sibling order and parent queries follow the retained tree",
              dynamic.parent(moving) == right &&
                  dynamic.children(right) == std::vector<FlexLayout::Node>{moving, sibling});
        check("moving to the same position preserves the layout cache",
              !dynamic.move(moving, right, sibling) && !dynamic.calculate({20, 30, 300, 180}));
        check("moving a node before itself is a no-op", !dynamic.move(moving, right, moving));
        check("cycles are rejected before changing the tree", rejects([&] {
                                                                  dynamic.move(right, moving);
                                                              }) &&
                                                                  dynamic.parent(right) == 0);
        check("moving the root is rejected", rejects([&] {
                  dynamic.move(0, right);
              }));
        check("insertion siblings must belong to the destination", rejects([&] {
                  dynamic.move(moving, left, sibling);
              }));
        const auto descendant = dynamic.add(left);
        check("container ancestry cannot be made cyclic", rejects([&] {
                                                              dynamic.move(left, descendant);
                                                          }) &&
                                                              dynamic.parent(left) == 0 &&
                                                              dynamic.parent(descendant) == left);
        dynamic.remove(descendant);
        dynamic.move(sibling, right, moving);
        dynamic.calculate({20, 30, 300, 180}, 1.5, true);
        check("same-parent reordering works with RTL and fractional DPI",
              near(dynamic.bounds(moving).y - dynamic.bounds(sibling).y, 30));
        dynamic.remove(right);
        check("removal releases a subtree and retains other handles",
              dynamic.size() == 2 && dynamic.contains(left) && !dynamic.contains(right) && !dynamic.contains(moving) &&
                  !dynamic.contains(sibling));
        check("stale handles cannot mutate another node", rejects([&] {
                  dynamic.style(moving, {});
              }));
        check("removing the root is rejected", rejects([&] {
                  dynamic.remove(0);
              }));
        bool monotonic = true;
        auto last = sibling;
        for (int i = 0; i < 5000; ++i) {
            const auto temporary = dynamic.add(left);
            monotonic &= temporary > last;
            last = temporary;
            dynamic.remove(temporary);
        }
        check("repeated additions reclaim capacity without reusing handles", monotonic && dynamic.size() == 2);
        dynamic.calculate({20, 30, 300, 180});
        check("a pruned tree still calculates normally", dynamic.bounds(left).width == 100);

        FlexLayout reentrant;
        reentrant.add(0, {}, [&](auto) {
            reentrant.remove(0);
            return Size{};
        });
        check("structural mutation during measurement is rejected", rejects([&] {
                  reentrant.calculate({0, 0, 40, 40});
              }));
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        passed = false;
    }
    return passed ? 0 : 1;
}
