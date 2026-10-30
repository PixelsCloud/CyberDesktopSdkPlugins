#include <filesystem>
#include <iostream>

#include "NewsCatalog.h"
#include "plugins/news-suite/tools/Samples.h"

int wmain(int argc, wchar_t** argv) {
    if (argc != 3 && !(argc == 4 && std::wstring(argv[3]) == L"--responsive")) {
        return 1;
    }
    using namespace news;
    const std::filesystem::path output(argv[1]), source(argv[2]);
    std::filesystem::create_directories(output);
    Renderer renderer;
    if (argc == 4) {
        for (const auto kind : {Kind::Reader, Kind::Github}) {
            const Spec spec{"preview", kind, 4, 4};
            const auto settings = defaults(kind);
            auto model = sample(kind, true);
            model.items[0].title = "一个很长的中文标题 — A long title that must wrap without overlapping its metadata";
            for (double dpr : {1., 1.5, 2.}) {
                for (const auto size : {cybercanvas::Size{144, 400}, {288, 400}}) {
                    const auto width = uint32_t(size.width * dpr), height = uint32_t(size.height * dpr);
                    std::vector<uint8_t> pixels(size_t(width) * height * 4);
                    CdFrame frame{sizeof(frame), width, height, width * 4, pixels.size(), pixels.data(), dpr};
                    const auto name = std::wstring(kind == Kind::Reader ? L"reader-" : L"github-") +
                                      std::to_wstring(int(size.width)) + L"-" + std::to_wstring(int(dpr * 100)) +
                                      L".png";
                    if (!renderer.render(spec, settings, model, frame, {}) || !renderer.savePng(output / name, frame)) {
                        return 2;
                    }
                }
            }
        }
        std::cout << "Exported twelve responsive news previews with long mixed-language titles\n";
        return 0;
    }
    int count = 0;
    for (const auto& c : catalog) {
        for (const auto cellHeight : {72, 100, 150}) {
            for (const bool zh : {false, true}) {
                for (const bool light : {false, true}) {
                    for (const bool transparent : {false, true}) {
                        const auto spec = c.spec;
                        auto settings = defaults(spec.kind);
                        const CdSettingValue changes[]{
                            {sizeof(CdSettingValue), CD_SETTING_CHOICE, "language", 0, 0, zh ? "zh-CN" : "en"},
                            {sizeof(CdSettingValue), CD_SETTING_CHOICE, "lighting", 0, 0, light ? "day" : "night"},
                            {sizeof(CdSettingValue), CD_SETTING_TOGGLE, "show_background", !transparent}};
                        settings.apply(changes, 3);
                        if (spec.kind == Kind::Reader) {
                            const CdSettingValue sourceChoice{
                                sizeof(CdSettingValue), CD_SETTING_CHOICE, "source", 0, 0, zh ? "sspai" : "guardian"};
                            settings.apply(&sourceChoice, 1);
                        }
                        const uint32_t width = 576, height = spec.rows * cellHeight * 2;
                        std::vector<uint8_t> pixels(size_t(width) * height * 4);
                        CdFrame frame{sizeof(frame), width, height, width * 4, pixels.size(), pixels.data(), 2};
                        auto model = sample(spec.kind, zh);
                        const auto name = std::wstring(c.folder) + (zh ? L"-zh" : L"-en") +
                                          (light ? L"-day" : L"-night") + (transparent ? L"-clear-" : L"-solid-") +
                                          std::to_wstring(cellHeight) + L".png";
                        if (!renderer.render(spec, settings, model, frame, {}) ||
                            !renderer.savePng(output / name, frame)) {
                            return 2;
                        }
                        if (cellHeight == 100 && zh && light && !transparent) {
                            std::filesystem::copy_file(output / name, source / "packages" / c.folder / "preview.png",
                                                       std::filesystem::copy_options::overwrite_existing);
                        }
                        ++count;
                    }
                }
            }
        }
    }
    std::cout << "Exported " << count << " actual native previews; editorial text is illustrative sample data.\n";
    return 0;
}
