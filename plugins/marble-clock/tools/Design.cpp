#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>

#include "Clock.h"
#include "plugins/mechanical-instruments/PhysicsMaterials.h"

int wmain(int argc, wchar_t** argv) try {
    if (argc < 3) {
        return 1;
    }
    const std::filesystem::path output(argv[1]), packages(argv[2]);
    std::filesystem::create_directories(output);
    for (bool compact : {false, true}) {
        const std::string layout = compact ? "4x2" : "6x2";
        cdsdk::Canvas canvas;
        marbleclock::Renderer renderer(std::filesystem::path(CD_MARBLE_TEST_ROOT) / "assets", compact);
        for (unsigned i = 0; renderer.pendingAssets() && i < 1000; ++i) {
            renderer.pollAssets();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        renderer.pollAssets();
        if (renderer.assetStats().ready != 4 || renderer.assetStats().failed) {
            throw std::runtime_error("Mechanical PNG assets did not load");
        }
        const auto paint = [&](marbleclock::Model& model, bool light, double dpi, const std::filesystem::path& filename,
                               bool background = true, double zoom = 1) {
            const double logicalWidth = (compact ? 400 : 600) * zoom;
            const uint32_t w = uint32_t(logicalWidth * dpi), h = uint32_t(200 * zoom * dpi);
            std::vector<uint8_t> bytes(size_t(w) * h * 4);
            CdFrame frame{sizeof(CdFrame), w, h, w * 4, bytes.size(), bytes.data(), dpi};
            const auto draw = [&] {
                canvas.render(frame, {}, [&](auto& c, auto) {
                    const auto bounds = mechanicalart::physicsmaterials::sceneBounds(w / dpi, h / dpi);
                    const auto view = cybercanvas::sceneViewport(bounds, {model.shape().width, marbleclock::kHeight});
                    c.translate(view.origin.x, view.origin.y);
                    c.scale(view.scale, view.scale);
                    renderer.draw(c, model, {light, background}, view.scale);
                });
            };
            draw();
            for (unsigned attempt = 0; renderer.pendingAssets() && attempt < 1000; ++attempt) {
                renderer.pollAssets();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            if (renderer.pendingAssets() || renderer.assetStats().failed) {
                throw std::runtime_error("Precision assets did not settle at the requested DPI");
            }
            draw();
            if (!canvas.save(filename.wstring(), frame)) {
                throw std::runtime_error("cannot save actual plugin rendering");
            }
        };
        marbleclock::Model model(compact);
        model.setTime(10, 28, 57);
        for (unsigned i = 0; i < 60; ++i) {
            model.advance(1. / 60);
        }
        model.setTime(10, 28, 58);
        for (unsigned i = 0; i < 45; ++i) {
            model.advance(1. / 60);
        }
        for (bool light : {false, true}) {
            for (double dpi : {1., 1.5, 2.}) {
                const auto file =
                    output / (layout + (light ? "-light-" : "-dark-") + std::to_string(int(dpi * 100)) + ".png");
                paint(model, light, dpi, file);
                if (!light && dpi == 2) {
                    std::filesystem::create_directories(packages / layout);
                    std::filesystem::copy_file(file, packages / layout / "preview.png",
                                               std::filesystem::copy_options::overwrite_existing);
                }
            }
        }
        paint(model, false, 2, output / (layout + "-transparent.png"), false);
        for (double zoom : {.72, 1.6}) {
            for (double dpi : {1., 1.5, 2.}) {
                const auto file = output / (layout + "-grid-" + std::to_string(int(zoom * 100)) + "-dpi-" +
                                            std::to_string(int(dpi * 100)) + ".png");
                paint(model, false, dpi, file, true, zoom);
            }
        }
        for (int start : {0, 25}) {
            marbleclock::Model feeding(compact);
            feeding.setTime(10, 28, start);
            for (int second = start + 1; second < 60; ++second) {
                feeding.setTime(10, 28, second);
                for (unsigned i = 0; i < 60; ++i) {
                    feeding.advance(1. / 60);
                }
            }
            paint(feeding, false, 2, output / (layout + "-feeding-" + std::to_string(start) + "-59.png"));
        }
        model.drain();
        unsigned last = 0;
        for (unsigned frame : {18u, 90u, 450u}) {
            for (; last < frame; ++last) {
                model.advance(1. / 60);
            }
            paint(model, false, 2, output / (layout + "-drain-" + std::to_string(frame) + ".png"));
        }
        if (argc > 3) {
            const std::filesystem::path frames = std::filesystem::path(argv[3]) / layout;
            std::filesystem::create_directories(frames);
            marbleclock::Model animation(compact);
            animation.setTime(10, 28, 55);
            for (int frame = 0; frame < 540; ++frame) {
                const auto clock = 10 * 3600 + 28 * 60 + 55 + frame / 30;
                animation.setTime(clock / 3600, clock / 60 % 60, clock % 60);
                animation.advance(1. / 30);
                paint(animation, false, 2, frames / ("frame-" + std::to_string(10000 + frame).substr(1) + ".png"));
            }
        }
    }
    std::cout << "PASS 12 layout/theme/DPI renders, transparent backplates, three drain stages and optional actual "
                 "physics frames\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 2;
}
