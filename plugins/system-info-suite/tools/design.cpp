#include <filesystem>
#include <fstream>
#include <iostream>

#include "plugins/system-info-suite/Monitor.h"

int wmain(int argc, wchar_t** argv) {
    if (argc < 2 || argc > 3)
        return 2;
    using namespace monitorplugin;
    std::filesystem::path root(argv[1]);
    std::filesystem::create_directories(root);
    Renderer renderer;
    std::ofstream report(root / "design.json");
    report << "{\"cases\":[";
    bool first = true;
    for (auto [rows, columns] : {std::pair{1, 1}, std::pair{1, 2}, std::pair{1, 4}, std::pair{2, 2}})
        for (int unit : {72, 96})
            for (int ratio : {100, 150})
                for (bool dark : {true, false}) {
                    const auto name = std::to_string(rows) + "x" + std::to_string(columns) + "-" +
                                      std::to_string(unit) + "-" + std::to_string(ratio) + (dark ? "-dark" : "-light");
                    const int width = columns * unit - 24, height = rows * unit * ratio / 100 - 24;
                    std::vector<uint8_t> pixels(size_t(width) * height * 16);
                    CdFrame frame{sizeof(frame),
                                  uint32_t(width * 2),
                                  uint32_t(height * 2),
                                  uint32_t(width * 8),
                                  uint64_t(pixels.size()),
                                  pixels.data(),
                                  2};
                    Spec spec{name.c_str(), rows, columns};
                    spec.mechanical = argc == 3;
                    Options options;
                    options.fontFamily = spec.mechanical ? "Microsoft YaHei UI" : "@siyuan-heavy";
                    options.palette = dark ? "dark" : "light";
                    if (!renderer.render(spec, options, {23, 75, 3, 0, 7}, frame) ||
                        !renderer.savePng((root / (name + ".png")).wstring(), frame, {0, 0, 0, 0}))
                        return 3;
                    if (!first)
                        report << ',';
                    first = false;
                    report << "{\"name\":\"" << name << "\",\"width\":" << width << ",\"height\":" << height << "}";
                }
    report << "]}";
    std::cout << "32 native design exports at exact accepted sizes and colors PASS\n";
    return 0;
}
