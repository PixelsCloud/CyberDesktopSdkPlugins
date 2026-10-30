#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

#include "plugins/mechanical-sign/Sign.h"

int wmain(int argc, wchar_t** argv) try {
    using namespace mechanicalsign;
    if (argc < 3) {
        return 1;
    }
    const std::filesystem::path output(argv[1]), packages(argv[2]);
    std::filesystem::create_directories(output);
    Renderer renderer(std::filesystem::path(CD_SIGN_TEST_ROOT) / "assets");
    for (int i = 0; renderer.pending() && i < 500; ++i) {
        renderer.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    renderer.poll();
    Model model;
    Format format = Format::Standard;
    cdsdk::Canvas canvas;
    auto paint = [&](double zoom, double dpi, bool background, const std::filesystem::path& file) {
        const auto g = geometry(format);
        const auto width = uint32_t(g.width / 2 * zoom * dpi), height = uint32_t(g.height / 2 * zoom * dpi);
        std::vector<uint8_t> bytes(size_t(width) * height * 4);
        const CdFrame frame{sizeof(CdFrame), width, height, width * 4, bytes.size(), bytes.data(), dpi};
        canvas.render(frame, {}, [&](auto& c, auto) {
            c.scale(zoom / 2, zoom / 2);
            renderer.draw(c, model, background, zoom / 2, format);
        });
        if (!canvas.save(file.wstring(), frame)) {
            throw std::runtime_error("Cannot save mechanical sign rendering");
        }
    };
    if (argc > 3) {
        std::filesystem::create_directories(output / "frames");
    }
    for (int frame = 0; frame < 30 * 14; ++frame) {
        if (frame == 90) {
            model.request();
        } else if (frame == 180) {
            model.text(pages("机械翻牌，欢迎回家"), true);
        } else if (frame == 270) {
            model.text(pages("CyberDesktop"), true);
        }
        model.advance(1. / 30);
        if (argc > 3) {
            wchar_t name[64];
            swprintf_s(name, L"frame-%04d.png", frame);
            paint(1.6, 1, true, output / "frames" / name);
        }
        if (frame == 18 || frame == 100 || frame == 198) {
            paint(1, 2, true, output / ("motion-" + std::to_string(frame) + ".png"));
        }
        if (frame == 65) {
            for (double zoom : {.72, 1., 1.6}) {
                for (double dpi : {1., 1.5, 2.}) {
                    paint(zoom, dpi, true,
                          output / ("grid-" + std::to_string(int(zoom * 100)) + "-dpi-" +
                                    std::to_string(int(dpi * 100)) + ".png"));
                }
            }
            paint(1, 2, false, output / "transparent.png");
            std::filesystem::copy_file(output / "grid-100-dpi-200.png", packages / "6x2/preview.png",
                                       std::filesystem::copy_options::overwrite_existing);
        }
    }
    model.finish();
    paint(1.6, 1, true, output / "implemented.png");
    for (const auto value : {Format::Compact, Format::Strip}) {
        format = value;
        const std::string name = value == Format::Compact ? "4x1" : "6x1";
        paint(1, 2, true, output / (name + ".png"));
        paint(1, 2, false, output / (name + "-transparent.png"));
        for (double zoom : {.72, 1., 1.6}) {
            for (double dpi : {1., 1.5, 2.}) {
                paint(zoom, dpi, true,
                      output / (name + "-grid-" + std::to_string(int(zoom * 100)) + "-dpi-" +
                                std::to_string(int(dpi * 100)) + ".png"));
            }
        }
    }
    format = Format::Standard;
    std::ofstream performance(output / "performance.json");
    performance
        << "{\"scope\":\"short native paint smoke; includes text layout and mechanism, excludes encoding\",\"cases\":[";
    bool first = true;
    for (double dpi : {1., 2.}) {
        const auto width = uint32_t(600 * dpi), height = uint32_t(200 * dpi);
        std::vector<uint8_t> bytes(size_t(width) * height * 4);
        const CdFrame frame{sizeof(CdFrame), width, height, width * 4, bytes.size(), bytes.data(), dpi};
        std::vector<double> samples;
        for (int i = -30; i < 300; ++i) {
            if (!model.active()) {
                model.request();
            }
            const auto start = std::chrono::steady_clock::now();
            model.advance(1. / 30);
            canvas.render(frame, {}, [&](auto& c, auto) {
                c.scale(.5, .5);
                renderer.draw(c, model, true, .5);
            });
            if (i >= 0) {
                samples.push_back(
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
            }
        }
        std::sort(samples.begin(), samples.end());
        performance << (first ? "" : ",") << "{\"dpi\":" << dpi << ",\"p50Ms\":" << samples[149]
                    << ",\"p95Ms\":" << samples[284] << ",\"maximumMs\":" << samples.back() << "}";
        first = false;
    }
    performance << "]}\n";
    std::cout << "Native mechanical sign previews and animation captured\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 2;
}
