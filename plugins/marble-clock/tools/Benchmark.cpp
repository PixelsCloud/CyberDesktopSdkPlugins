#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

#include "plugins/marble-clock/Clock.h"

int wmain(int argc, wchar_t** argv) try {
    if (argc != 2) {
        return 1;
    }
    std::ofstream report(std::filesystem::path(argv[1]), std::ios::trunc);
    report << "{\"scope\":\"short actual plugin performance smoke, not full engine acceptance\",\"scenarios\":[";
    bool first = true;
    for (bool compact : {false, true}) {
        for (double dpi : {1., 2.}) {
            marbleclock::Model model(compact);
            model.setTime(10, 28, 57);
            cdsdk::Canvas canvas;
            marbleclock::Renderer renderer(std::filesystem::path(CD_MARBLE_TEST_ROOT) / "assets");
            for (unsigned i = 0; renderer.pendingAssets() && i < 1000; ++i) {
                renderer.pollAssets();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            renderer.pollAssets();
            if (renderer.assetStats().ready != 3 || renderer.assetStats().failed) {
                throw std::runtime_error("Mechanical PNG assets did not load");
            }
            const auto width = uint32_t((compact ? 400 : 600) * dpi), height = uint32_t(200 * dpi);
            std::vector<uint8_t> bytes(size_t(width) * height * 4);
            CdFrame frame{sizeof(CdFrame), width, height, width * 4, bytes.size(), bytes.data(), dpi};
            std::vector<double> samples;
            size_t peak = 0;
            for (int i = -30; i < 360; ++i) {
                const auto clock = 10 * 3600 + 28 * 60 + 57 + std::max(0, i) / 30;
                model.setTime(clock / 3600, clock / 60 % 60, clock % 60);
                const auto start = std::chrono::steady_clock::now();
                model.advance(1. / 30);
                canvas.render(frame, {}, [&](auto& c, auto) {
                    constexpr double scale = 200 / marbleclock::kHeight;
                    c.scale(scale, scale);
                    renderer.draw(c, model, {}, scale);
                });
                peak = std::max(peak, model.marbles().size());
                if (i >= 0) {
                    samples.push_back(
                        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
                }
            }
            const auto raw = samples;
            std::sort(samples.begin(), samples.end());
            if (!first) {
                report << ',';
            }
            first = false;
            report << "{\"layout\":\"" << (compact ? "4x2" : "6x2") << "\",\"dpi\":" << dpi
                   << ",\"peakMarbles\":" << peak << ",\"p50Ms\":" << samples[179] << ",\"p95Ms\":" << samples[341]
                   << ",\"maxMs\":" << samples.back() << ",\"recycled\":" << model.recycled()
                   << ",\"fallbackRecoveries\":" << model.recoveries() << ",\"rawMs\":[";
            for (size_t i = 0; i < raw.size(); ++i) {
                report << (i ? "," : "") << raw[i];
            }
            report << "]}";
            std::cout << (compact ? "4x2" : "6x2") << " DPI " << dpi << " peak " << peak << " p95 " << samples[341]
                      << " ms\n";
        }
    }
    report << "]}\n";
    return report.good() ? 0 : 2;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 3;
}
