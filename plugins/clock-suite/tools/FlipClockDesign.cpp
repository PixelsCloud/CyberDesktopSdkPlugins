#include <filesystem>
#include <iostream>
#include <tuple>

#include "plugins/clock-suite/FlipClock.h"

int wmain(int argc, wchar_t** argv) {
    if (argc != 2)
        return 2;
    using namespace clockplugin;
    const std::filesystem::path root(argv[1]);
    std::filesystem::create_directories(root);
    FlipRenderer renderer;
    Renderer exporter;
    Spec spec{"flip.design", false, 2, 2, true};
    auto opts = options(defaults(spec), {43, 41, 36, 255});
    opts.language = "en";
    const Time time{2026, 10, 12, 1, 23, 28, 4, 0};
    for (auto [rows, columns, w, h] :
         {std::tuple{2, 2, 136, 136}, std::tuple{2, 2, 136, 208}, std::tuple{2, 2, 240, 400}, std::tuple{1, 4, 280, 64},
          std::tuple{1, 4, 280, 100}, std::tuple{2, 4, 280, 136}, std::tuple{2, 4, 280, 208}}) {
        spec.rows = rows;
        spec.columns = columns;
        std::vector<uint8_t> pixels(size_t(w) * h * 16);
        CdFrame frame{sizeof(frame),
                      uint32_t(w * 2),
                      uint32_t(h * 2),
                      uint32_t(w * 8),
                      uint64_t(pixels.size()),
                      pixels.data(),
                      2};
        const std::string name = "flip-" + std::to_string(rows) + "x" + std::to_string(columns) + "-" +
                                 std::to_string(w) + "x" + std::to_string(h);
        FlipPose pose{{FlipValue{11, 11, 1}, FlipValue{28, 28, 1}, FlipValue{4, 4, 1}}};
        if (!renderer.render(spec, opts, time, pose, frame) ||
            !exporter.savePng((root / (name + ".png")).wstring(), frame, {238, 238, 236, 255}))
            return 1;
        for (int phase = 0; phase <= 4; ++phase) {
            pose.values[1] = {28, 29, phase / 4.0};
            if (!renderer.render(spec, opts, time, pose, frame) ||
                !exporter.savePng((root / (name + "-phase-" + std::to_string(phase) + ".png")).wstring(), frame,
                                  {238, 238, 236, 255}))
                return 1;
        }
    }
    std::cout << "Flip clock: square/portrait reference and five downward flip "
                 "phases exported\n";
    return 0;
}
