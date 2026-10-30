#include <cstring>
#include <filesystem>
#include <iostream>

#include "plugins/lunar-suite/Lunar.h"

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) {
        return 1;
    }
    using namespace lunarplugin;
    const std::filesystem::path output(argv[1]), source(argv[2]);
    std::filesystem::create_directories(output);
    Renderer renderer;
    const auto settings = defaults();
    for (const auto spec : {Spec{"1x2", 1, 2}, Spec{"2x2", 2, 2}, Spec{"4x4", 4, 4}}) {
        for (int cellH : {72, 100, 150}) {
            const uint32_t w = spec.columns * 144, h = spec.rows * cellH * 2;
            std::vector<uint8_t> pixels(size_t(w) * h * 4);
            CdFrame frame{sizeof(frame), w, h, w * 4, pixels.size(), pixels.data(), 2};
            for (bool dark : {false, true}) {
                const auto name = std::wstring(spec.id, spec.id + std::strlen(spec.id)) +
                                  (dark ? L"-night-" : L"-day-") + std::to_wstring(cellH) + L".png";
                if (!renderer.render(spec, settings, {{2026, 2, 17}, {2026, 2, 17}, dark}, frame, {}) ||
                    !renderer.save((output / name).wstring(), frame)) {
                    return 2;
                }
                if (!dark && cellH == 100) {
                    std::filesystem::copy_file(output / name, source / "packages" / spec.id / "preview.png",
                                               std::filesystem::copy_options::overwrite_existing);
                }
            }
        }
    }
    std::cout << "18 native lunar designs exported\n";
    return 0;
}
