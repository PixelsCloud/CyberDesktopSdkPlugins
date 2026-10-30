#include <CyberCanvas/Images.h>
#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <thread>

#include "WidgetCatalog.h"
#include "plugins/widget-suite/Widget.h"

int wmain(int argc, wchar_t** argv) {
    using namespace widgetplugin;
    if (argc < 2)
        return 1;
    const std::filesystem::path output(argv[1]);
    std::filesystem::create_directories(output);
    Photo photo;
    if (argc > 2) {
        const auto store = cybercanvas::ImageStore::shared();
        for (const auto& entry : std::filesystem::directory_iterator(argv[2])) {
            if (!entry.is_regular_file()) {
                continue;
            }
            auto handle = store->requestOriented(entry.path(), 0, 600);
            for (int i = 0; i < 100 && handle.snapshot().state == cybercanvas::ImageState::Loading; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
            }
            photo.bitmap = handle.snapshot().image;
            if (photo.bitmap) {
                break;
            }
        }
    }
    Canvas canvas;
    for (const auto& item : catalog) {
        const auto spec = item.spec;
        for (int cellHeight : {72, 100, 150}) {
            const uint32_t width = uint32_t(spec.columns * 72), height = uint32_t(spec.rows * cellHeight);
            auto settings = defaults(spec.kind);
            Spectrum spectrum;
            for (size_t i = 0; i < Spectrum::count; ++i) {
                const float amount =
                    float(.30 + .25 * std::sin(i * .28) + .14 * std::sin(i * .81) + .10 * std::cos(i * .57));
                spectrum.level[i] = std::clamp(amount, .08f, .92f);
                spectrum.peak[i] = std::min(1.f, spectrum.level[i] + .045f);
            }
            std::vector<uint8_t> pixels(size_t(width) * height * 4);
            CdFrame frame{sizeof(CdFrame), width, height, width * 4, pixels.size(), pixels.data(), 1};
            if (!canvas.render(spec, settings, spectrum, photo, {}, 1, {2026, 10, 13}, 0, frame, {}))
                return 2;
            if (!canvas.savePng((output / (std::wstring(item.folder) + L"-" + std::to_wstring(width) + L"x" +
                                           std::to_wstring(height) + L".png"))
                                    .wstring(),
                                frame))
                return 3;
        }
    }
    std::cout << catalog.size() * 3 << " real-render size previews exported\n";
    return 0;
}
