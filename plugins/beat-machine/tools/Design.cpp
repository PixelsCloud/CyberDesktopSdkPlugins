#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

#include "Machine.h"
#include "plugins/mechanical-instruments/PhysicsMaterials.h"

int wmain(int argc, wchar_t** argv) try {
    using namespace beatmachine;
    if (argc < 3) {
        return 1;
    }
    const std::filesystem::path output(argv[1]), packages(argv[2]);
    std::filesystem::create_directories(output);
    Renderer renderer(std::filesystem::path(CD_BEAT_TEST_ROOT) / "assets");
    for (int i = 0; renderer.pendingAssets() && i < 500; ++i) {
        renderer.pollAssets();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    renderer.pollAssets();
    cdsdk::Canvas canvas;
    for (bool compact : {false, true}) {
        const auto directory = compact ? output / "4x2" : output;
        std::filesystem::create_directories(directory);
        AudioMotion audio;
        Model model(36, compact);
        std::array<float, 63> bands{};
        const auto paint = [&](double zoom, double dpi, Appearance appearance, const std::filesystem::path& file) {
            const auto width = uint32_t((compact ? 400 : 600) * zoom * dpi), height = uint32_t(200 * zoom * dpi);
            std::vector<uint8_t> bytes(size_t(width) * height * 4);
            const CdFrame frame{sizeof(CdFrame), width, height, width * 4, bytes.size(), bytes.data(), dpi};
            const auto draw = [&] {
                canvas.render(frame, {}, [&](auto& c, auto) {
                    const auto bounds = mechanicalart::physicsmaterials::sceneBounds(width / dpi, height / dpi);
                    const auto view = cybercanvas::sceneViewport(bounds, {model.width(), kHeight});
                    c.translate(view.origin.x, view.origin.y);
                    c.scale(view.scale, view.scale);
                    renderer.draw(c, model, audio.signal(), appearance, view.scale);
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
            if (!canvas.save(file.wstring(), frame)) {
                throw std::runtime_error("cannot save actual beat machine rendering");
            }
        };
        if (argc > 3) {
            std::filesystem::create_directories(directory / "frames");
        }
        for (int frame = 0; frame < 30 * 18; ++frame) {
            const double t = frame / 30.;
            bands.fill(0);
            if (t < 14) {
                std::fill_n(bands.begin(), 21, frame % 15 < 3 ? .85f : .025f);
                std::fill(bands.begin() + 21, bands.begin() + 42, float(.22 + .17 * std::sin(t * 2)));
                std::fill(bands.begin() + 42, bands.end(), frame % 8 < 2 ? .6f : .04f);
            }
            audio.feed(bands);
            audio.advance(1. / 30, false);
            model.advance(1. / 30, audio.signal());
            if (argc > 3) {
                wchar_t filename[64];
                swprintf_s(filename, L"frame-%04d.png", frame);
                paint(1.6, 1, {}, directory / "frames" / filename);
            }
            if (frame % 45 == 0) {
                paint(1, 2, {}, directory / ("motion-" + std::to_string(frame) + ".png"));
            }
            if (frame == 210) {
                for (double zoom : {.72, 1., 1.6}) {
                    for (double dpi : {1., 1.5, 2.}) {
                        const auto name =
                            "grid-" + std::to_string(int(zoom * 100)) + "-dpi-" + std::to_string(int(dpi * 100));
                        paint(zoom, dpi, {}, directory / (name + ".png"));
                    }
                }
                paint(1, 2, {true}, directory / "light.png");
                paint(1, 2, {false, false}, directory / "transparent.png");
                const auto package = packages / (compact ? "4x2" : "6x2");
                std::filesystem::create_directories(package);
                std::filesystem::copy_file(directory / "grid-100-dpi-200.png", package / "preview.png",
                                           std::filesystem::copy_options::overwrite_existing);
            }
        }
        paint(1, 2, {}, directory / "settling.png");
        std::ofstream report(directory / "simulation.json");
        report << "{\"input\":\"synthetic test spectrum, not live audio\",\"seconds\":18,\"balls\":"
               << model.balls().size() << ",\"launches\":" << model.launches() << ",\"returns\":" << model.returns()
               << ",\"hits\":" << model.hits() << ",\"recoveries\":" << model.recoveries() << "}\n";
        std::cout << "Native design captured: " << model.launches() << " launches / " << model.returns()
                  << " returns\n";
        std::ofstream performance(directory / "performance.json");
        performance << "{\"scope\":\"short physics and native rendering smoke; excludes encoding and live "
                       "capture\",\"cases\":[";
        bool first = true;
        for (unsigned count : {36u, 48u}) {
            for (double dpi : {1., 2.}) {
                Model scene(count, compact);
                Signal signal{.6, .3, .2, .8};
                const auto width = uint32_t((compact ? 400 : 600) * dpi), height = uint32_t(200 * dpi);
                std::vector<uint8_t> pixels(size_t(width) * height * 4);
                const CdFrame frame{sizeof(CdFrame), width, height, width * 4, pixels.size(), pixels.data(), dpi};
                std::vector<double> samples;
                for (int i = -30; i < 300; ++i) {
                    if (i % 15 == 0) {
                        ++signal.beat;
                    }
                    const auto start = std::chrono::steady_clock::now();
                    scene.advance(1. / 30, signal);
                    canvas.render(frame, {}, [&](auto& c, auto) {
                        constexpr double scale = 200 / kHeight;
                        c.scale(scale, scale);
                        renderer.draw(c, scene, signal, {}, scale);
                    });
                    if (i >= 0) {
                        samples.push_back(
                            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                                .count());
                    }
                }
                std::sort(samples.begin(), samples.end());
                performance << (first ? "" : ",") << "{\"balls\":" << count << ",\"dpi\":" << dpi
                            << ",\"p50Ms\":" << samples[149] << ",\"p95Ms\":" << samples[284]
                            << ",\"maximumMs\":" << samples.back() << "}";
                first = false;
            }
        }
        performance << "]}\n";
    }
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 2;
}
