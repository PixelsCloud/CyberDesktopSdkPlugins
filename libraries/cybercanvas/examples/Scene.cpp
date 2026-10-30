#include <CyberCanvas/Scene.h>

#include <iostream>

int main(int argc, char** argv) {
    using namespace cybercanvas;
    try {
        Canvas canvas;
        View root;
        root.layout.padding = {24, 24, 24, 24};
        root.layout.gap = 16;
        root.paint = [](auto& canvas, Rect area, const WidgetTheme&) {
            canvas.box(area.x, area.y, area.width, area.height, 0, 0x151C28FF);
        };
        Scene scene(root);
        auto theme = WidgetTheme::dark();
        theme.font.size = 13;
        theme.font.wrap = true;
        scene.theme(theme);
        View title;
        title.kind = ViewKind::Text;
        title.text = L"CyberCanvas 0.7\nRetained components";
        title.theme.fontSize = 22;
        scene.add(0, title);
        View text;
        text.kind = ViewKind::Text;
        text.text = L"Layout, themes and input share one tree. Values redraw without remeasuring text.";
        text.theme.foreground = theme.muted;
        scene.add(0, text);
        View row;
        row.kind = ViewKind::Row;
        row.layout.wrap = true;
        const auto toolbar = scene.add(0, row);
        View button;
        button.kind = ViewKind::Button;
        button.key = 1;
        button.text = L"Reset volume";
        button.appearance = ButtonStyle::Filled;
        scene.add(toolbar, button);
        button.key = 2;
        button.text = L"Muted";
        button.appearance = ButtonStyle::Outline;
        button.enabled = false;
        scene.add(toolbar, button);
        text.text = L"Volume / 音量";
        scene.add(0, text);
        View slider;
        slider.kind = ViewKind::Slider;
        slider.key = 10;
        slider.layout.height = Length::points(5);
        slider.maximum = 100;
        slider.value = 42;
        slider.step = 1;
        const auto volume = scene.add(0, slider);
        const double width = argc > 2 ? 280 : 520, height = 360;
        scene.arrange(canvas, {0, 0, width, height}, 2);
        const auto bounds = scene.bounds(volume);
        const auto event = scene.input({PointerKind::Press, {bounds.x + bounds.width * .65, bounds.y + 2}, true});
        if (event.key != 10 || !event.value || std::abs(*event.value - 65) > 1) {
            return 2;
        }
        scene.input({PointerKind::Release, {bounds.x + bounds.width * .65, bounds.y + 2}});
        scene.advance(.14);
        std::vector<uint8_t> pixels(size_t(width * 2) * size_t(height * 2) * 4);
        FrameBuffer frame{uint32_t(width * 2), uint32_t(height * 2), uint32_t(width * 8),
                          pixels.size(),       pixels.data(),        2};
        canvas.render(frame, {}, [&](auto&, auto) {
            scene.draw(canvas);
        });
        const std::string output = argc > 1 ? argv[1] : "scene.png";
        if (!canvas.save(std::wstring(output.begin(), output.end()), frame)) {
            return 1;
        }
        std::cout << "PASS retained scene example: " << scene.stats().layouts << " layout, "
                  << scene.stats().measurements << " measurements\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
