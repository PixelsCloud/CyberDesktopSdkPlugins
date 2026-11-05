#include <Windows.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

#include "plugins/weather-suite/Weather.h"
using namespace weatherplugin;

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc < 3 || argc > 4)
            return 2;
        const bool preview = argc == 4 && std::wstring_view(argv[3]) == L"preview";
        const bool mechanical = argc == 4 && !preview;
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
                auto settings = defaults(mechanical);
                spec.mechanical = mechanical;
                CdSettingValue change{sizeof(CdSettingValue), CD_SETTING_CHOICE, "palette", 0, 0, palette};
                settings.apply(&change, 1);
                const uint32_t w = uint32_t(spec.columns * 77), h = uint32_t(spec.rows * 99);
                const uint32_t scale = preview ? 1 : 2;
                const double dpr = scale;
                std::vector<uint8_t> pixels(size_t(w) * h * 4 * scale * scale);
                CdFrame frame{sizeof(CdFrame), w * scale, h * scale, w * scale * 4, pixels.size(), pixels.data(), dpr};
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
        if (argc == 3) {
            sample.city = "济南";
            sample.temperature = 19;
            sample.feels = 15;
            sample.humidity = 26;
            sample.wind = 4;
            sample.code = 0;
            sample.days = {{"2026-11-05", 0, 19, 9, 0},
                           {"2026-11-06", 3, 15, 8, 0},
                           {"2026-11-07", 61, 18, 8, 35},
                           {"2026-11-08", 63, 12, 5, 70}};
            for (const double dpi : {1., 1.25, 1.5, 2.}) {
                for (const bool dark : {false, true}) {
                    auto settings = defaults();
                    const CdSettingValue colors[]{
                        {sizeof(CdSettingValue), CD_SETTING_COLOR, "background", dark ? 0x1D2835FFu : 0xEAF4FFFFu},
                        {sizeof(CdSettingValue), CD_SETTING_COLOR, "foreground", dark ? 0xE7F3F8FFu : 0x205B88FFu},
                        {sizeof(CdSettingValue), CD_SETTING_COLOR, "secondary", dark ? 0xADC5D3FFu : 0x627F99FFu}};
                    if (settings.apply(colors, 3) != CD_OK) {
                        return 6;
                    }
                    const uint32_t width = uint32_t(std::lround(297 * dpi)), height = uint32_t(std::lround(179 * dpi));
                    std::vector<uint8_t> pixels(size_t(width) * height * 4);
                    CdFrame frame{sizeof(CdFrame), width, height, width * 4, pixels.size(), pixels.data(), dpi};
                    const auto name =
                        L"layout-" + std::to_wstring(int(dpi * 100)) + (dark ? L"-dark.png" : L"-light.png");
                    if (!renderer.renderReference({"2x4", 2, 4}, settings, sample, false, frame, {}) ||
                        !renderer.savePng(output / name, frame)) {
                        return 7;
                    }
                }
            }
        }
        std::cout << "Weather size, palette and actual-DPI design previews exported\n";
        return 0;
    } catch (...) {
        return 5;
    }
}
