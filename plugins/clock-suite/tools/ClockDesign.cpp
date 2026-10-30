#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "plugins/clock-suite/Clock.h"

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        std::cerr << "cd_clock_design OUTPUT_DIRECTORY\n";
        return 2;
    }
    using namespace clockplugin;
    const std::filesystem::path root(argv[1]);
    std::filesystem::create_directories(root);
    Renderer renderer;
    std::ofstream report(root / "design.json");
    report << "{\"time\":\"2026-10-12T10:08:30\",\"cases\":[";
    bool first = true;
    for (bool analog : {false, true})
        for (auto [rows, columns] : {std::pair{1, 1}, std::pair{1, 2}, std::pair{1, 4}, std::pair{2, 2}})
            for (int ratio : {100, 150}) {
                const std::string name = std::string(analog ? "analog-" : "digital-") + std::to_string(rows) + "x" +
                                         std::to_string(columns) + "-" + std::to_string(ratio);
                Spec spec{name.c_str(), analog, rows, columns};
                const int w = 128 * columns - 24, h = 128 * rows * ratio / 100 - 24;
                const double dpr = 2;
                std::vector<uint8_t> rgba(size_t(w) * h * 16);
                CdFrame frame{sizeof(frame),
                              uint32_t(w * 2),
                              uint32_t(h * 2),
                              uint32_t(w * 8),
                              uint64_t(rgba.size()),
                              rgba.data(),
                              dpr};
                const auto opts = options(defaults(spec), {43, 41, 36, 255});
                std::string error;
                if (!renderer.render(spec, opts, {2026, 10, 12, 1, 10, 8, 30, 0}, frame, {}, &error)) {
                    std::cerr << error << '\n';
                    return 1;
                }
                if (!renderer.savePng((root / (name + ".png")).wstring(), frame, {0, 0, 0, 0}))
                    return 1;
                if (!renderer.savePng((root / (name + "-ceramic.png")).wstring(), frame, {250, 247, 241, 255}))
                    return 1;
                if (!first)
                    report << ',';
                first = false;
                report << "{\"name\":\"" << name << "\",\"rows\":" << rows << ",\"columns\":" << columns
                       << ",\"heightPercent\":" << ratio << ",\"width\":" << w << ",\"height\":" << h
                       << ",\"dpr\":2,\"file\":\"" << name << ".png\"}";
            }
    report << "]}\n";
    std::cout << "16 real Clock designs and transparent texture exports: PASS\n";
    return 0;
}
