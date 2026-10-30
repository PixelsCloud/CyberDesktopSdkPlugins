#include <CyberCanvas/PhysicsScene.h>

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
    using namespace cybercanvas::physics;
    try {
        Canvas canvas;
        View root;
        root.inheritGap = false;
        Scene scene(root);
        World world({.gravity = {}});
        View ball;
        ball.kind = ViewKind::Decoration;
        ball.clickable = true;
        ball.draggable = true;
        ball.layout.width = ball.layout.height = Length::points(40);
        ball.layout.shrink = 0;
        ball.paintLocal = [](Canvas& c, Rect r, const WidgetTheme&) {
            c.rectangle(r, 0x10aa80ff);
        };
        const auto node = scene.add(0, ball);
        const auto body = world.createBody({.position = {1, 1}, .velocity = {1, 0}, .sleepingAllowed = false});
        world.circle(body, {}, .2);
        SceneBinding binding(world, scene, 0);
        binding.bind(body, node);
        scene.arrange(canvas, {0, 0, 300, 300});
        binding.update();
        auto bounds = scene.visualBounds(node);
        check("meters map to logical pixels and visual centers", bounds.x == 80 && bounds.y == 80);
        world.advance(1. / 60);
        world.advance(1. / 120);
        binding.update();
        const auto shown = world.interpolated(body).position;
        bounds = scene.visualBounds(node);
        check("scene uses interpolated physical pose", std::abs(bounds.x + 20 - shown.x * 100) < 1e-4);
        check("drag picks currently displayed location", binding.beginDrag({bounds.x + 20, bounds.y + 20}));
        binding.drag({150, 100});
        for (int i = 0; i < 120; ++i) {
            world.advance(1. / 60);
        }
        binding.update();
        check("drag is connected to a mouse joint", std::abs(world.state(body).position.x - 1.5) < .02);
        binding.cancelDrag();
        check("drag cancellation removes the joint", !binding.dragging() && world.stats().joints == 0);
        bounds = scene.visualBounds(node);
        const Point origin{17, 29};
        const Point center{bounds.x + 20, bounds.y + 20};
        const auto press = scene.input({PointerKind::Press, {center.x + origin.x, center.y + origin.y}, true}, origin);
        check("physical view requests pointer capture", press.pointer.drag == DragPhase::Begin);
        check("offset host pointer starts physical drag", binding.beginDrag(center, 1000, origin));
        check("starting physical drag preserves router capture",
              scene.router().captured() && scene.router().pressed() == node);
        binding.cancelDrag();
        scene.cancel();
        const auto unchanged = world.state(body).position.x;
        scene.arrange(canvas, {0, 0, 300, 300}, 2);
        binding.update();
        check("DPI does not alter physical positions", world.state(body).position.x == unchanged);
        scene.transform(0, {.translation = {10, 20}, .scale = {2, 2}});
        const auto mapped = binding.toWorld({210, 220});
        check("root transform is inverted for pointer input",
              std::abs(mapped.x - 1) < 1e-5 && std::abs(mapped.y - 1) < 1e-5);
        scene.transform(0, {.scale = {2, 1}});
        bool rejected = false;
        try {
            binding.update();
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        check("nonuniform physical root rejected", rejected);
        scene.transform(0, {});
        std::vector<uint8_t> rgba(300 * 300 * 4);
        canvas.render({300, 300, 1200, rgba.size(), rgba.data(), 1}, {}, [&](Canvas& c, Rect) {
            scene.draw(c);
            binding.debugDraw(c);
        });
        scene.remove(node);
        scene.arrange(canvas, {0, 0, 300, 300});
        binding.update();
        check("removed visual is detached without destroying simulation", world.valid(body));
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return passed ? 0 : 1;
}
