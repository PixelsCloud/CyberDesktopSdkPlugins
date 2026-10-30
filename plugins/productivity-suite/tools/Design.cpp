#include <Windows.h>

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

#include "plugins/productivity-suite/Productivity.h"
using namespace productivity;

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc < 3 || argc > 4)
            return 2;
        std::ifstream file(argv[1]);
        const auto catalog = nlohmann::json::parse(file);
        const std::filesystem::path output(argv[2]);
        std::filesystem::create_directories(output);
        Renderer renderer;
        std::ofstream html(output / "rendered.html");
        html << "<!doctype html><html lang='zh-CN'><meta "
                "charset='utf-8'><title>实际 C++ "
                "插件绘制</"
                "title><style>body{background:#14202a;color:#dce6ec;font:16px "
                "system-ui}main{display:flex;flex-wrap:wrap;gap:24px;max-width:"
                "1080px;margin:auto}figure{margin:0;padding:18px;background:#"
                "22313d;border-radius:10px}figcaption{margin-bottom:10px}img{"
                "display:block}</style><h2>14 个插件 · 实际 C++ 渲染</h2><p>77×99 "
                "长方形格子，含共享字体、DirectWrite 字体排版和 Direct2D "
                "阴影。另附窄长格子、英文与高 DPI 样图。</p><main>";
        for (const auto& item : catalog) {
            const auto id = item.at("id").get<std::string>();
            Spec spec{id.c_str(), Kind(item.at("kind").get<int>()), item.at("rows"), item.at("columns")};
            auto settings = defaults(spec.kind, argc == 4);
            spec.mechanical =
                argc == 4 && (spec.kind == Kind::Timer || spec.kind == Kind::Network || spec.kind == Kind::Disk);
            Data sample;
            sample.valid = true;
            sample.remaining = 18 * 60 + 42;
            sample.running = true;
            sample.days = 80;
            sample.available = 182ull * 1024 * 1024 * 1024;
            sample.total = 476ull * 1024 * 1024 * 1024;
            sample.free = sample.available;
            sample.down = uint64_t(12.8 * 1024 * 1024);
            sample.up = uint64_t(1.24 * 1024 * 1024);
            for (int i = 0; i < 60; ++i)
                sample.history.emplace_back(uint64_t((.3 + .6 * std::abs(std::sin(i * .65))) * sample.down),
                                            uint64_t((.2 + .8 * std::abs(std::cos(i * .55))) * sample.up));
            sample.tasks = {{"整理桌面", true}, {"完成今天的计划", false}, {"阅读十分钟", false}};
            sample.source = "Qt Blog";
            sample.articles = {{"新的工具，让桌面工作更轻松", "https://example.org/one", "10-13 · 点击阅读全文"},
                               {"设计与性能：本周更新", "https://example.org/two", "10-12 · 点击阅读全文"}};
            const auto folder = item.at("folder").get<std::string>();
            for (int mode = 0; mode < 3; ++mode) {
                const uint32_t logicalW = uint32_t(spec.columns * (mode == 1 ? 65 : 77)),
                               logicalH = uint32_t(spec.rows * (mode == 1 ? 129 : 99));
                if (mode == 2) {
                    CdSettingValue language{sizeof(CdSettingValue), CD_SETTING_CHOICE, "language", 0, 0, "en"};
                    settings.apply(&language, 1);
                }
                const uint32_t dpr = mode == 2 ? 3 : 2, w = logicalW * dpr, h = logicalH * dpr;
                std::vector<uint8_t> pixels(size_t(w) * h * 4);
                CdFrame frame{sizeof(CdFrame), w, h, w * 4, pixels.size(), pixels.data(), double(dpr)};
                const std::string name = folder + (mode == 1 ? "-tall" : mode == 2 ? "-en" : "") + ".png";
                if (!renderer.render(spec, settings, sample, frame, {}) || !renderer.savePng(output / name, frame))
                    return 3;
                if (mode == 0)
                    html << "<figure><figcaption>" << item.at("name").get<std::string>() << "</figcaption><img src='"
                         << name << "' width='" << logicalW << "' height='" << logicalH << "'></figure>";
            }
        }
        html << "</main></html>";
        std::cout << "42 design exports completed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 4;
    }
}
