#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>

#include "Weather.h"
using namespace weatherplugin;

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc < 3 || argc > 4)
            return 2;
        const std::filesystem::path assets(argv[1]), output(argv[2]);
        std::filesystem::create_directories(output);
        Renderer renderer(assets);
        Snapshot sample;
        sample.valid = true;
        sample.city = "北京";
        sample.region = "北京市";
        sample.time = "2026-10-13T10:30";
        sample.temperature = 18.4;
        sample.feels = 17.2;
        sample.humidity = 46;
        sample.wind = 2.3;
        sample.code = 2;
        sample.days = {{"2026-10-13", 2, 22, 14, 10},
                       {"2026-10-14", 0, 24, 15, 0},
                       {"2026-10-15", 3, 21, 13, 20},
                       {"2026-10-16", 61, 19, 12, 75},
                       {"2026-10-17", 2, 22, 14, 15}};
        for (auto spec : std::array<Spec, 3>{{{"1x2", 1, 2}, {"1x4", 1, 4}, {"2x4", 2, 4}}})
            for (const auto palette : {"dark", "light"}) {
                auto settings = defaults(argc == 4);
                spec.mechanical = argc == 4;
                CdSettingValue change{sizeof(CdSettingValue), CD_SETTING_CHOICE, "palette", 0, 0, palette};
                settings.apply(&change, 1);
                const uint32_t w = uint32_t(spec.columns * 77), h = uint32_t(spec.rows * 99);
                constexpr double dpr = 2;
                std::vector<uint8_t> pixels(size_t(w) * h * 16);
                CdFrame frame{sizeof(CdFrame), w * 2, h * 2, w * 8, pixels.size(), pixels.data(), dpr};
                if (!renderer.renderReference(spec, settings, sample, false, frame, {}) ||
                    !renderer.savePng(output / (L"weather-" + wide(spec.id) + L"-" + wide(palette) + L".png"), frame))
                    return 3;
                if (std::string(palette) == "dark") {
                    CdSettingValue language{sizeof(CdSettingValue), CD_SETTING_CHOICE, "language", 0, 0, "en"};
                    settings.apply(&language, 1);
                    sample.city = "Beijing";
                    if (!renderer.renderReference(spec, settings, sample, false, frame, {}) ||
                        !renderer.savePng(output / (L"weather-" + wide(spec.id) + L"-english.png"), frame))
                        return 4;
                    sample.city = "北京";
                }
            }
        std::cout << "9 real-render weather design previews exported\n";
        return 0;
    } catch (...) {
        return 5;
    }
}
