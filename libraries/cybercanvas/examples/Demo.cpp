#include <CyberCanvas/Animation.h>
#include <CyberCanvas/Canvas.h>

#include <filesystem>
#include <iostream>
#include <vector>

int main(int argc, char** argv) {
    try {
        using namespace cybercanvas;
        std::vector<uint8_t> pixels(800 * 520 * 4);
        FrameBuffer output{800, 520, 800 * 4, pixels.size(), pixels.data(), 2};
        cybercanvas::Canvas canvas;
        const double seconds = argc > 2 ? std::stod(argv[2]) : 0;
        Tween<Pose> pulse({.opacity = .6}, {.scale = {1.1, 1.1}, .rotation = 12},
                          {.duration = 2.4, .easing = Ease::InOutSine, .repeat = Repeat::PingPong, .cycles = 0});
        auto curve = canvas.path();
        curve.moveTo({0, 65}).cubicTo({50, 65}, {40, 10}, {90, 28}).quadraticTo({140, 48}, {180, 15});
        canvas.render(output, {}, [&](auto& draw, auto area) {
            draw.gradientBox({0, 0, area.width, area.height}, 20, 0x263A50FF, 0x14212FFF);
            Stack column(inset(area, {24, 24, 24, 24}), Axis::Vertical, 14);
            const auto header = column.take(36);
            Stack row(header, Axis::Horizontal, 14);
            const auto badge = row.take(28);
            auto pose = pulse.sample(seconds).value;
            pose.pivot = {badge.x + 14, badge.y + 18};
            draw.posed(pose, [&] {
                draw.oval(badge.x + 11, badge.y + 15, 10, 10, 0x69D4B6FF);
                draw.oval(badge.x + 18, badge.y + 20, 8, 8, 0x8EC8FFFF);
            });
            TextStyle heading;
            heading.size = 22;
            heading.bold = true;
            draw.text(L"CyberCanvas 0.3", heading, row.remaining(), 0xFFFFFFFF);
            TextStyle body;
            body.size = 13;
            body.wrap = true;
            body.vertical = VerticalAlign::Top;
            const std::wstring label =
                L"Host-driven time, easing and reusable animation tracks.\nPause, resume and repeat without a private "
                L"timer.";
            const auto measured = draw.measureText(label, body, {column.remaining().width, 100});
            draw.text(label, body, column.take(std::ceil(measured.height)), 0xCEDCEAFF);
            const auto plot = column.remaining();
            draw.box(plot.x, plot.y, plot.width, plot.height, 12, 0xFFFFFF09);
            draw.saved([&] {
                draw.translate(plot.x + 16, plot.y + 8);
                draw.scale((plot.width - 32) / 180, (plot.height - 16) / 80);
                draw.stroke(curve, 0x69D4B6FF, {.width = 2, .cap = LineCap::Round, .join = LineJoin::Round});
            });
        });
        const auto path = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("cybercanvas.png");
        if (!canvas.save(path.wstring(), output)) {
            return 1;
        }
        std::cout << path.string() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
