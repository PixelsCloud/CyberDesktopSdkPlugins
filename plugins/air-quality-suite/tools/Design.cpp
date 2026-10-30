#include <Windows.h>

#include <filesystem>
#include <iostream>

#include "plugins/air-quality-suite/AirQuality.h"
using namespace airplugin;

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc != 3)
            return 2;
        const std::filesystem::path assets(argv[1]), output(argv[2]);
        std::filesystem::create_directories(output);
        Renderer renderer(assets);
        Snapshot sample;
        sample.valid = true;
        sample.city = "济南";
        sample.station = "示例监测站";
        sample.time = "2026-10-13 11:00:00";
        sample.aqi = 42;
        sample.dominant = "pm25";
        sample.pollutants = {37, 23, 16, 9, 4, 3};
        sample.sources = {{"Environmental Protection Department (山东省环境保护厅)", "https://example.org/epa"},
                          {"World Air Quality Index Project", "https://waqi.info/"}};
        sample.forecast = {{"2026-10-13", 37, 30, 44}, {"2026-10-14", 52, 44, 60}, {"2026-10-15", 61, 52, 75}};
        for (const Spec spec : std::array<Spec, 3>{{{"1x2", 1, 2}, {"1x4", 1, 4}, {"2x4", 2, 4}}})
            for (const auto palette : {"dark", "light"}) {
                auto settings = defaults();
                CdSettingValue change{sizeof(CdSettingValue), CD_SETTING_CHOICE, "palette", 0, 0, palette};
                settings.apply(&change, 1);
                const uint32_t w = uint32_t(spec.columns * 77), h = uint32_t(spec.rows * 99);
                std::vector<uint8_t> pixels(size_t(w) * h * 16);
                CdFrame frame{sizeof(CdFrame), w * 2, h * 2, w * 8, pixels.size(), pixels.data(), 2};
                if (!renderer.renderReference(spec, settings, sample, false, frame, {}) ||
                    !renderer.savePng(output / (L"air-quality-" + wide(spec.id) + L"-" + wide(palette) + L".png"),
                                      frame))
                    return 3;
            }
        auto settings = defaults();
        for (int severity = 0; severity < 6; ++severity) {
            sample.aqi = std::array<double, 6>{42, 78, 125, 175, 250, 350}[size_t(severity)];
            std::vector<uint8_t> pixels(308 * 198 * 4);
            CdFrame frame{sizeof(CdFrame), 308, 198, 308 * 4, pixels.size(), pixels.data(), 2};
            if (!renderer.renderReference({"1x2", 1, 2}, settings, sample, false, frame, {}) ||
                !renderer.savePng(output / (L"air-quality-level-" + std::to_wstring(severity) + L".png"), frame))
                return 4;
        }
        std::cout << "6 layout designs and 6 severity previews exported\n";
        return 0;
    } catch (...) {
        return 5;
    }
}
