#include <cstring>
#include <filesystem>
#include <iostream>

#include "AmbienceCatalog.h"
#include "Atmosphere.h"

int wmain(int argc, wchar_t** argv) {
    if (argc < 3 || argc > 4) {
        return 1;
    }
    using namespace atmosphere;
    const std::filesystem::path output(argv[1]), source(argv[2]);
    std::filesystem::create_directories(output);
    Renderer renderer(source.wstring());
    const auto renderSettled = [&](Spec spec, const cdsdk::Settings& settings, const Model& model, const CdFrame& frame,
                                   CdRect safe) {
        if (!renderer.render(spec, settings, model, frame, safe)) {
            return false;
        }
        for (int attempt = 0; attempt < 1000 && renderer.imagesPending(); ++attempt) {
            Sleep(1);
            renderer.pollImages();
        }
        return !renderer.imagesPending() && renderer.render(spec, settings, model, frame, safe);
    };
    if (argc == 4 && std::wstring(argv[3]) == L"--responsive") {
        const Spec spec{"preview", Kind::Noise, 2, 4};
        const auto settings = defaults(spec);
        Model model;
        model.audioStatus = 1;
        model.levels = {.65f, .35f, 0, .25f, .2f, 0};
        for (double dpr : {1., 1.5, 2.}) {
            for (const auto size : {cybercanvas::Size{144, 400}, {288, 144}, {288, 200}}) {
                const auto width = uint32_t(size.width * dpr), height = uint32_t(size.height * dpr);
                std::vector<uint8_t> pixels(size_t(width) * height * 4);
                CdFrame frame{sizeof(frame), width, height, width * 4, pixels.size(), pixels.data(), dpr};
                const auto name = L"noise-" + std::to_wstring(int(size.width)) + L"x" +
                                  std::to_wstring(int(size.height)) + L"-" + std::to_wstring(int(dpr * 100)) + L".png";
                if (!renderSettled(spec, settings, model, frame, {}) ||
                    !renderer.save((output / name).wstring(), frame)) {
                    return 2;
                }
            }
        }
        std::cout << "Exported nine responsive noise previews\n";
        return 0;
    }
    for (const auto& item : catalog) {
        const auto spec = item.spec;
        auto settings = defaults(spec);
        for (int cellHeight : {72, 100, 150}) {
            const uint32_t width = spec.columns * 144, height = spec.rows * cellHeight * 2;
            std::vector<uint8_t> pixels(size_t(width) * height * 4);
            CdFrame frame{sizeof(frame), width, height, width * 4, pixels.size(), pixels.data(), 2};
            for (bool dark : {false, true}) {
                const int variants = spec.kind == Kind::Noise ? (spec.rows == 1 ? 6 : 2) : 3;
                for (int variant = 0; variant < variants; ++variant) {
                    const char* choices[] = {"pothos", "succulent", "daisy"};
                    if (spec.kind != Kind::Noise) {
                        const CdSettingValue change{sizeof(change),  CD_SETTING_CHOICE, "species", 0, 0,
                                                    choices[variant]};
                        settings.apply(&change, 1);
                        if (spec.kind == Kind::Plant && spec.columns >= 4) {
                            const CdSettingValue single{sizeof(single), CD_SETTING_TOGGLE, "garden",
                                                        variant == 0 ? 1 : 0};
                            settings.apply(&single, 1);
                        }
                    }
                    if (spec.kind == Kind::Noise && spec.rows == 1) {
                        const CdSettingValue primary{sizeof(primary), CD_SETTING_INTEGER, "primary", variant};
                        settings.apply(&primary, 1);
                    }
                    Model model;
                    model.plantedAt = wallSeconds();
                    model.dark = dark;
                    model.motion.seek(2.2);
                    model.levels = {.65f, .35f, 0, .25f, .2f, 0};
                    model.audioStatus = 1;
                    if (spec.kind == Kind::Noise && spec.rows > 1) {
                        model.noisePage = variant;
                    }
                    const auto name = std::wstring(item.folder) + (dark ? L"-night-" : L"-day-") +
                                      std::to_wstring(variant) + L"-" + std::to_wstring(cellHeight) + L".png";
                    if (!renderSettled(spec, settings, model, frame, {}) ||
                        !renderer.save((output / name).wstring(), frame)) {
                        return 2;
                    }
                    if (!dark && variant == 0 && cellHeight == 100) {
                        std::filesystem::copy_file(output / name, source / "packages" / item.folder / "preview.png",
                                                   std::filesystem::copy_options::overwrite_existing);
                    }
                    if (spec.kind == Kind::Noise && cellHeight == 100) {
                        const CdSettingValue transparent{sizeof(transparent), CD_SETTING_TOGGLE, "show_background", 0};
                        settings.apply(&transparent, 1);
                        const auto clearName = std::wstring(item.folder) + (dark ? L"-night-" : L"-day-") +
                                               std::to_wstring(variant) + L"-transparent-100.png";
                        if (!renderSettled(spec, settings, model, frame, {}) ||
                            !renderer.save((output / clearName).wstring(), frame)) {
                            return 4;
                        }
                        const CdSettingValue opaque{sizeof(opaque), CD_SETTING_TOGGLE, "show_background", 1};
                        settings.apply(&opaque, 1);
                    }
                }
            }
        }
    }
    if (argc == 4) {
        std::filesystem::create_directories(argv[3]);
        const Spec spec{"preview", Kind::Plant, 2, 2};
        auto settings = defaults(spec);
        const uint32_t width = 288, height = 400;
        std::vector<uint8_t> pixels(size_t(width) * height * 4);
        CdFrame frame{sizeof(frame), width, height, width * 4, pixels.size(), pixels.data(), 2};
        Model model;
        model.plantedAt = wallSeconds();
        for (int i = 0; i < 120; ++i) {
            model.advance(.05, .75);
            if (i == 35) {
                model.water(wallSeconds());
            }
            const auto name = L"plant-" + std::to_wstring(1000 + i).substr(1) + L".png";
            if (!renderSettled(spec, settings, model, frame, {}) ||
                !renderer.save((std::filesystem::path(argv[3]) / name).wstring(), frame)) {
                return 3;
            }
        }
        const Spec noiseSpec{"preview", Kind::Noise, 2, 4};
        const auto noiseSettings = defaults(noiseSpec);
        const uint32_t noiseWidth = 576;
        pixels.resize(size_t(noiseWidth) * height * 4);
        frame = {sizeof(frame), noiseWidth, height, noiseWidth * 4, pixels.size(), pixels.data(), 2};
        Model sound;
        sound.audioStatus = 1;
        sound.levels = {.6f, .3f, 0, .25f, .2f, 0};
        for (int i = 0; i < 128; ++i) {
            const auto name = L"noise-" + std::to_wstring(1000 + i).substr(1) + L".png";
            if (!renderSettled(noiseSpec, noiseSettings, sound, frame, {}) ||
                !renderer.save((std::filesystem::path(argv[3]) / name).wstring(), frame)) {
                return 5;
            }
            sound.advance(.05, .75);
            renderer.advanceFeedback(.05);
        }
    }
    std::cout << "Native ambience previews and optional animation frames exported\n";
    return 0;
}
