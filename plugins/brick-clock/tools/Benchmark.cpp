#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "plugins/brick-clock/Clock.h"

int wmain(int argc, wchar_t** argv) try {
    if (argc != 2) {
        return 1;
    }
    std::ofstream report(std::filesystem::path(argv[1]), std::ios::trunc);
    report << "{\"scope\":\"short plugin performance smoke, not full engine acceptance\",\"scenarios\":[";
    bool first = true;
    for (bool maximum : {false, true}) {
        for (double dpi : {1., 2.}) {
            brickclock::Model model;
            model.setChipCount(maximum ? 20 : 12);
            model.setTime(10, 28, 44);
            cdsdk::Canvas canvas;
            brickclock::Renderer renderer;
            const auto w = uint32_t(376 * dpi), h = uint32_t(88 * dpi);
            std::vector<uint8_t> bytes(size_t(w) * h * 4);
            const CdFrame frame{sizeof(CdFrame), w, h, w * 4, bytes.size(), bytes.data(), dpi};
            std::vector<double> samples;
            size_t peak = 0;
            for (int i = -30; i < 240; ++i) {
                if (i % 30 == 0) {
                    if (maximum) {
                        model.setTime(23, 59, 59);
                        model.setTime(0, 0, 0);
                    } else {
                        model.setTime(10, 28, 45 + std::max(0, i) / 30);
                    }
                }
                const auto start = std::chrono::steady_clock::now();
                model.advance(1. / 30);
                canvas.render(frame, {}, [&](auto& c, auto) {
                    constexpr double scale = 88 / brickclock::kHeight;
                    c.translate((376 - brickclock::kWidth * scale) / 2, 0);
                    c.scale(scale, scale);
                    renderer.draw(c, model, "clay", scale);
                });
                const double milliseconds =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                peak = std::max(peak, model.chips().size());
                if (i >= 0) {
                    samples.push_back(milliseconds);
                }
            }
            const auto raw = samples;
            std::sort(samples.begin(), samples.end());
            const auto name = maximum ? "six-digit-rollover" : "ordinary-seconds";
            if (!first) {
                report << ',';
            }
            first = false;
            report << "{\"name\":\"" << name << "\",\"dpi\":" << dpi << ",\"samples\":240,\"peakChips\":" << peak
                   << ",\"p50Ms\":" << samples[119] << ",\"p95Ms\":" << samples[227] << ",\"maxMs\":" << samples.back()
                   << ",\"rawMs\":[";
            for (size_t i = 0; i < raw.size(); ++i) {
                report << (i ? "," : "") << raw[i];
            }
            report << "]}";
            std::cout << name << " DPI " << dpi << " chips " << peak << " p50 " << samples[119] << " ms p95 "
                      << samples[227] << " ms max " << samples.back() << " ms\n";
        }
    }
    report << "]}\n";
    return report.good() ? 0 : 2;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 3;
}
