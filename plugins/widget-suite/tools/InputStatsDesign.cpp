#include <filesystem>
#include <iostream>

#include "plugins/widget-suite/Widget.h"

int wmain(int argc, wchar_t** argv) {
    if (argc != 2)
        return 1;
    using namespace widgetplugin;
    const Spec spec{"input-stats-1x4", Kind::InputStats, 1, 4};
    std::filesystem::create_directories(argv[1]);
    Canvas canvas;
    auto settings = defaults(spec.kind);
    const CdInputStats stats{sizeof(stats), CD_INPUT_STATS_ACTIVE, 20261014, 0, 14208, 3156, 328412, 105284, 0, 0, 0};
    for (const int height : {72, 100, 150}) {
        std::vector<uint8_t> pixels(size_t(616) * height * 2 * 4);
        CdFrame frame{sizeof(frame), 616, uint32_t(height * 2), 616 * 4, pixels.size(), pixels.data(), 2};
        if (!canvas.render(spec, settings, {}, {}, {}, 1, {}, 0, frame, {}, {}, 0, stats))
            return 2;
        const auto file = std::filesystem::path(argv[1]) / (L"input-stats-1x4-" + std::to_wstring(height) + L".png");
        if (!canvas.savePng(file.wstring(), frame))
            return 3;
    }
    std::cout << "3 input-statistics designs exported\n";
}
