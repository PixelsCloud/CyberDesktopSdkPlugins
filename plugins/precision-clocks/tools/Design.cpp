#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

#include "Clock.h"

int wmain(int argc, wchar_t** argv) try {
    using namespace precisionclock;
    if (argc < 3) {
        return 1;
    }
    const std::filesystem::path output(argv[1]), packages(argv[2]);
    std::filesystem::create_directories(output);
    std::ofstream performance(output / "performance.json");
    performance << "{\"scope\":\"short native Canvas paint smoke, excludes PNG encoding\",\"cases\":[";
    bool first = true;
    for (auto kind : {Kind::Skeleton, Kind::Green, Kind::Dress}) {
        const auto& spec = specification(kind);
        const auto directory = output / spec.folder;
        std::filesystem::create_directories(directory);
        cdsdk::Canvas canvas;
        Renderer renderer(kind, std::filesystem::path(CD_PRECISION_ROOT) / "assets" / spec.folder);
        Model model(kind);
        Options value;
        using namespace std::chrono;
        const auto epoch = duration_cast<milliseconds>(sys_days{year{2026} / 10 / 20}.time_since_epoch()).count() +
                           (2 * 3600 + 10 * 60 + 36) * 1000ll;
        CdEnvironmentSnapshot environment{
            sizeof(CdEnvironmentSnapshot), 1, uint64_t(epoch), 1000, 480, CD_ENV_THEME_DARK, "zh-CN"};
        model.synchronize(environment, value);
        const auto paint = [&](double zoom, double dpi, const std::filesystem::path& file) {
            const auto edge = uint32_t(200 * zoom * dpi);
            std::vector<uint8_t> bytes(size_t(edge) * edge * 4);
            const CdFrame frame{sizeof(CdFrame), edge, edge, edge * 4, bytes.size(), bytes.data(), dpi};
            const auto render = [&] {
                canvas.render(frame, {}, [&](auto& c, auto) {
                    c.scale(zoom / 5, zoom / 5);
                    renderer.draw(c, model.state(), value, zoom / 5);
                });
            };
            render();
            for (int i = 0; renderer.pending() && i < 500; ++i) {
                renderer.poll();
                std::this_thread::sleep_for(milliseconds(4));
            }
            render();
            if (!canvas.save(file.wstring(), frame)) {
                throw std::runtime_error("Cannot save native precision clock preview");
            }
        };
        for (double zoom : {.6, 1., 1.6}) {
            for (double dpi : {1., 1.5, 2.}) {
                paint(zoom, dpi,
                      directory / ("grid-" + std::to_string(int(zoom * 100)) + "-dpi-" +
                                   std::to_string(int(dpi * 100)) + ".png"));
            }
        }
        paint(1, 2, directory / "preview.png");
        std::filesystem::copy_file(directory / "preview.png", packages / spec.folder / "preview.png",
                                   std::filesystem::copy_options::overwrite_existing);
        paint(1.5, 2, directory / "implemented.png");
        value.background = false;
        paint(1, 2, directory / "round.png");
        value.background = true;
        if (argc > 3) {
            const auto frames = directory / "frames";
            std::filesystem::create_directories(frames);
            for (int i = 0; i < 180; ++i) {
                environment.unix_ms = uint64_t(epoch) + uint64_t(i) * 1000 / 30;
                model.synchronize(environment, value);
                wchar_t name[48];
                swprintf_s(name, L"frame-%04d.png", i);
                paint(1.5, 2, frames / name);
            }
        }
        for (double dpi : {1., 2.}) {
            const auto edge = uint32_t(200 * dpi);
            std::vector<uint8_t> bytes(size_t(edge) * edge * 4);
            const CdFrame frame{sizeof(CdFrame), edge, edge, edge * 4, bytes.size(), bytes.data(), dpi};
            std::vector<double> samples;
            for (int i = -20; i < 180; ++i) {
                environment.unix_ms = uint64_t(epoch) + uint64_t(i + 20) * 33;
                model.synchronize(environment, value);
                const auto begin = steady_clock::now();
                canvas.render(frame, {}, [&](auto& c, auto) {
                    c.scale(.2, .2);
                    renderer.draw(c, model.state(), value, .2);
                });
                if (i >= 0) {
                    samples.push_back(duration<double, std::milli>(steady_clock::now() - begin).count());
                }
            }
            std::sort(samples.begin(), samples.end());
            performance << (first ? "" : ",") << "{\"kind\":\"" << spec.folder << "\",\"dpi\":" << dpi
                        << ",\"p50Ms\":" << samples[89] << ",\"p95Ms\":" << samples[170]
                        << ",\"maximumMs\":" << samples.back() << "}";
            first = false;
        }
    }
    performance << "]}\n";
    std::cout << "Three native precision clock previews, DPI grids and performance samples exported\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 2;
}
