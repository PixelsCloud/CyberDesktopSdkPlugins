#include <CyberCanvas/Scene.h>

#include <filesystem>
#include <iostream>

int main(int argc, char** argv) {
    using namespace cybercanvas;
    try {
        Canvas canvas;
        View root;
        root.layout.padding = {24, 24, 24, 24};
        root.layout.gap = 16;
        root.paint = [](Canvas& draw, Rect area, const WidgetTheme&) {
            draw.box(area.x, area.y, area.width, area.height, 20, 0x111B28FF);
        };
        Scene scene(root);
        auto theme = WidgetTheme::dark();
        theme.font.family = L"Microsoft YaHei";
        theme.font.size = 14;
        scene.theme(theme);
        View title;
        title.kind = ViewKind::Text;
        title.text = L"CyberCanvas 0.9 / 动态组件树";
        title.theme.fontSize = 22;
        scene.add(0, title);
        View caption = title;
        caption.text = L"同一场景：移动控件、删除内容，保留音量和业务 ID";
        caption.theme.fontSize = 13;
        caption.theme.foreground = theme.muted;
        scene.add(0, caption);
        View row;
        row.kind = ViewKind::Row;
        row.layout.gap = 16;
        row.layout.grow = 1;
        const auto columns = scene.add(0, row);
        const auto panel = [&](uint32_t accent) {
            View view;
            view.layout.grow = 1;
            view.layout.basis = Length::points(0);
            view.layout.padding = {18, 18, 18, 18};
            view.layout.gap = 14;
            view.theme.accent = accent;
            view.paint = [](Canvas& draw, Rect area, const WidgetTheme& colors) {
                draw.box(area.x, area.y, area.width, area.height, 16, 0x1D2B3DFF);
                draw.box(area.x + 18, area.y + 18, 26, 3, 1.5, colors.accent);
            };
            return scene.add(columns, view);
        };
        const auto library = panel(0x6DD9C3FF);
        const auto mixer = panel(0x9DBBFFFF);
        const auto label = [&](Scene::Node parent, const wchar_t* text) {
            View view;
            view.kind = ViewKind::Text;
            view.text = text;
            view.layout.margin.top = 10;
            view.theme.fontSize = 16;
            return scene.add(parent, view);
        };
        label(library, L"声音库");
        label(mixer, L"混音面板");
        View group;
        group.layout.gap = 14;
        const auto rainGroup = scene.add(library, group);
        View sound;
        sound.kind = ViewKind::Button;
        sound.key = 101;
        sound.text = L"雨声 / Rain";
        sound.appearance = ButtonStyle::Outline;
        sound.selected = true;
        sound.layout.height = Length::points(42);
        const auto rain = scene.add(rainGroup, sound);
        label(rainGroup, L"音量 42%");
        View volume;
        volume.kind = ViewKind::Slider;
        volume.key = 102;
        volume.value = 42;
        volume.maximum = 100;
        volume.layout.height = Length::points(5);
        const auto slider = scene.add(rainGroup, volume);
        sound.key = 201;
        sound.text = L"海浪 / Ocean";
        scene.add(mixer, sound);
        const auto placeholder = label(mixer, L"等待加入更多声音…");
        caption.text = L"移动后沿用面板主题，音量仍为 42%";
        caption.theme.foreground = theme.muted;
        const auto note = scene.add(0, caption);
        const std::filesystem::path directory = argc > 1 ? argv[1] : ".";
        const auto save = [&](const wchar_t* name) {
            scene.arrange(canvas, {0, 0, 640, 440}, 2);
            std::vector<uint8_t> pixels(1280 * 880 * 4);
            FrameBuffer frame{1280, 880, 5120, pixels.size(), pixels.data(), 2};
            canvas.render(frame, {}, [&](auto&, auto) {
                scene.draw(canvas);
            });
            if (!canvas.save((directory / name).wstring(), frame)) {
                throw std::runtime_error("Cannot save the example frame");
            }
        };
        save(L"scene-before.png");
        scene.remove(placeholder);
        scene.move(rainGroup, mixer, scene.find(201));
        scene.text(note, L"已移动雨声和音量，移除占位内容；句柄与数值保持不变");
        save(L"scene-after.png");
        if (scene.find(101) != rain || scene.find(102) != slider || scene.view(slider).value != 42) {
            return 2;
        }
        std::cout << "PASS dynamic scene: before/after PNGs, stable handles and retained value\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
