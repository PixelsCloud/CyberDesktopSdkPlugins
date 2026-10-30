#include <CyberCanvas/DataWidgets.h>
#include <CyberCanvas/Scene.h>

#include <filesystem>
#include <iostream>

int main(int argc, char** argv) {
    using namespace cybercanvas;
    bool passed = true;
    const auto check = [&](const char* name, bool ok) {
        passed &= ok;
        std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
    };
    try {
        Canvas canvas;
        Scene scene;
        auto theme = WidgetTheme::desktop();
        scene.theme(theme);
        View button;
        button.kind = ViewKind::Button;
        button.text = L"确定 / Apply";
        button.key = 1;
        button.layout.shrink = 0;
        const auto node = scene.add(0, button);
        scene.arrange(canvas, {0, 0, 320, 180});
        check("desktop buttons have room for text and minimum control height", scene.bounds(node).height >= 32);
        std::vector<uint8_t> scenePixels(320 * 180 * 4);
        FrameBuffer sceneFrame{320, 180, 1280, scenePixels.size(), scenePixels.data(), 1};
        const auto draw = [&] {
            canvas.render(sceneFrame, {}, [&](auto&, auto) {
                scene.draw(canvas);
            });
        };
        draw();
        const auto layouts = scene.stats().layouts;
        const auto original = scenePixels;
        scene.enabled(node, false);
        draw();
        const auto disabled = scenePixels;
        theme.disabledOpacity = .2;
        scene.theme(theme);
        scene.arrange(canvas, {0, 0, 320, 180});
        draw();
        check("disabled opacity changes paint without layout",
              scene.stats().layouts == layouts && scenePixels != disabled);
        check("disabled state changes visible content", original != disabled);
        ThemePatch override;
        override.disabledOpacity = .75;
        scene.theme(node, override);
        scene.arrange(canvas, {0, 0, 320, 180});
        check("local opacity override inherits other desktop tokens",
              scene.resolvedTheme(node).disabledOpacity == .75 && scene.resolvedTheme(node).radius == 7);
        theme.controlHeight = 48;
        scene.theme(theme);
        scene.arrange(canvas, {0, 0, 320, 180});
        check("control height changes measured geometry",
              scene.bounds(node).height >= 48 && scene.stats().layouts > layouts);
        const auto previous = scene.resolvedTheme(node);
        bool rejected = false;
        try {
            override.disabledOpacity = 1.1;
            scene.theme(node, override);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        check("invalid opacity is rejected without changing the theme",
              rejected && scene.resolvedTheme(node).disabledOpacity == previous.disabledOpacity);
        std::vector<uint8_t> smallPixels(8 * 8 * 4);
        FrameBuffer smallFrame{8, 8, 32, smallPixels.size(), smallPixels.data(), 1};
        canvas.render(smallFrame, {}, [&](auto& painter, auto) {
            ContentNotice{{3, 3, 2, 2}, L"narrow", L"clipped", ContentState::Error}.draw(painter, theme);
        });
        bool clipped = true;
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                if (x < 3 || x >= 5 || y < 3 || y >= 5) {
                    clipped &= smallPixels[(y * 8 + x) * 4 + 3] == 0;
                }
            }
        }
        check("tiny status panels cannot paint outside their bounds", clipped);

        const std::filesystem::path output = argc > 1 ? argv[1] : "theme-gallery";
        std::filesystem::create_directories(output);
        for (double dpi : {1., 1.5, 2., 3.}) {
            for (bool light : {false, true}) {
                const auto colors = WidgetTheme::desktop(light);
                const uint32_t w = uint32_t(600 * dpi), h = uint32_t(390 * dpi);
                std::vector<uint8_t> pixels(size_t(w) * h * 4);
                FrameBuffer frame{w, h, w * 4, pixels.size(), pixels.data(), dpi};
                const auto gallery = [&] {
                    canvas.render(frame, {}, [&](auto& painter, auto) {
                        painter.box(0, 0, 600, 390, 0, light ? 0xFAFBFDFF : 0x151C27FF);
                        const wchar_t* names[] = {L"普通 / Normal", L"悬停 / Hover", L"按下 / Press",
                                                  L"选中 / Selected", L"禁用 / Disabled"};
                        for (int i = 0; i < 5; ++i) {
                            Button control(i + 1, {12. + i * 117, 16, 108, 32}, names[i], ButtonStyle::Outline);
                            control.selected = i == 3;
                            control.enabled = i != 4;
                            control.draw(painter, colors, {i == 1, i == 2, i == 1 ? .55 : i == 2 ? 1. : 0.});
                        }
                        Slider(10, {22, 73, 210, 4}).draw(painter, colors, .65, {true, false, .55});
                        Slider disabledSlider(11, {270, 73, 180, 4});
                        disabledSlider.enabled = false;
                        disabledSlider.draw(painter, colors, .35);
                        Toggle(12, {500, 62, 40, 24}).draw(painter, colors, true);
                        const ContentState states[] = {ContentState::Ready, ContentState::Loading, ContentState::Empty,
                                                       ContentState::Error};
                        const wchar_t* titles[] = {L"内容已就绪", L"正在加载…", L"暂无内容", L"暂时无法加载"};
                        const wchar_t* details[] = {L"Ready: data updated", L"Loading: please wait",
                                                    L"Empty: add an item to begin", L"Error: try again later"};
                        for (int i = 0; i < 4; ++i) {
                            ContentNotice{
                                {12. + (i % 2) * 294, 104. + (i / 2) * 132, 282, 120}, titles[i], details[i], states[i]}
                                .draw(painter, colors);
                        }
                        ContentNotice{{580, 380, 2, 2}, L"narrow", L"clipped", ContentState::Error}.draw(painter,
                                                                                                         colors);
                    });
                };
                gallery();
                const auto first = pixels;
                gallery();
                check("all statuses are deterministic without a timer", first == pixels);
                const auto path = output / ((light ? "light-" : "dark-") + std::to_string(dpi) + ".png");
                check("theme gallery exports at actual DPI", canvas.save(path.wstring(), frame));
            }
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        passed = false;
    }
    return passed ? 0 : 1;
}
