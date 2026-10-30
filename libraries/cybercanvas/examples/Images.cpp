#include <CyberCanvas/Images.h>
#include <CyberCanvas/Scene.h>

#include <chrono>
#include <iostream>
#include <thread>

int wmain(int argc, wchar_t** argv) {
    using namespace cybercanvas;
    if (argc < 3 || argc > 4) {
        std::wcerr << L"Usage: cybercanvas_images_demo <image> <output.png> [sprite-cells]\n";
        return 2;
    }
    try {
        const auto output = std::filesystem::path(argv[2]);
        const auto cells = argc == 4 ? uint32_t(std::stoul(argv[3])) : 1u;
        Canvas canvas;
        ImageStore images;
        const auto requested = std::chrono::steady_clock::now();
        auto request = images.request(argv[1]);
        const auto returned = std::chrono::steady_clock::now();
        View root;
        root.layout.padding = {20, 20, 20, 20};
        root.layout.gap = 12;
        root.paint = [](Canvas& draw, Rect area, const WidgetTheme&) {
            draw.box(area.x, area.y, area.width, area.height, 18, 0x192832FF);
        };
        Scene scene(root);
        View title;
        title.kind = ViewKind::Text;
        title.text = L"CyberCanvas 0.10 / Background images";
        title.theme.fontSize = 18;
        const auto heading = scene.add(0, title);
        View image;
        image.kind = ViewKind::Image;
        image.layout.grow = 1;
        image.layout.basis = Length::points(0);
        image.imageCells = cells;
        const auto picture = scene.add(0, image);
        const auto save = [&](const std::filesystem::path& path) {
            scene.arrange(canvas, {0, 0, 400, 280}, 2);
            std::vector<uint8_t> pixels(800 * 560 * 4);
            FrameBuffer frame{800, 560, 3200, pixels.size(), pixels.data(), 2};
            canvas.render(frame, {}, [&](auto&, auto) {
                scene.draw(canvas);
            });
            if (!canvas.save(path.wstring(), frame)) {
                throw std::runtime_error("Cannot write image example");
            }
        };
        scene.imageResource(picture, {ImageState::Loading});
        save(output.parent_path() / (output.stem().wstring() + L"-loading.png"));
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        auto resource = request.snapshot();
        // Offline export waits here; a GUI instead polls from its existing event loop.
        while (resource.state == ImageState::Loading && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            resource = request.snapshot();
        }
        scene.imageResource(picture, resource);
        scene.text(heading, resource.state == ImageState::Ready ? L"Ready / Worker decode, UI-thread upload"
                                                                : L"Unavailable / Safe placeholder");
        save(output);
        const auto startPaint = std::chrono::steady_clock::now();
        for (int i = 0; i < 100; ++i) {
            // Render a fresh texture each time; no PNG encoding in the timed section.
            std::vector<uint8_t> pixels(800 * 560 * 4);
            FrameBuffer frame{800, 560, 3200, pixels.size(), pixels.data(), 2};
            canvas.render(frame, {}, [&](auto&, auto) {
                scene.draw(canvas);
            });
        }
        const auto rendered = std::chrono::steady_clock::now();
        std::cout << "{\"state\":" << int(resource.state)
                  << ",\"requestMs\":" << std::chrono::duration<double, std::milli>(returned - requested).count()
                  << ",\"warmFrameMeanMs\":"
                  << std::chrono::duration<double, std::milli>(rendered - startPaint).count() / 100
                  << ",\"decodedBytes\":" << images.stats().decodedBytes
                  << ",\"uploadedBytes\":" << canvas.imageUploadBytes() << ",\"decodes\":" << images.stats().decodes
                  << ",\"layouts\":" << scene.stats().layouts << "}\n";
        return resource.state == ImageState::Loading ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
