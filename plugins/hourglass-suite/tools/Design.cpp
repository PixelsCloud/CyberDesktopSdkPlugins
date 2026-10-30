#include <filesystem>
#include <iostream>

#include "plugins/hourglass-suite/Hourglass.h"
#include "plugins/hourglass-suite/PhysicalSand.h"

int wmain(int argc, wchar_t** argv) {
    using namespace hourglassplugin;
    if (argc < 3) {
        return 1;
    }
    const std::filesystem::path output(argv[1]), source(argv[2]);
    std::filesystem::create_directories(output);
    Renderer renderer;
    // Export the physical variant from the same renderer used by the plugin.
    for (bool square : {false, true}) {
        const uint32_t w = square ? 264 : 552, h = 264;
        std::vector<uint8_t> rgba(size_t(w) * h * 4);
        CdFrame physicalFrame{sizeof(CdFrame), w, h, w * 4, rgba.size(), rgba.data(), 3};
        for (bool dark : {false, true}) {
            Model timer;
            timer.reset(5);
            PhysicalSand grains;
            Options view;
            view.dark = dark;
            view.showTimer = !square;
            view.physicalSand = &grains;
            for (int index = 0; index < 240; ++index) {
                timer.advance(1. / 30, true);
                grains.advance(1. / 30, timer);
                if (index == 75) {
                    const auto name =
                        std::wstring(square ? L"physical-1x1-" : L"physical-1x2-") + (dark ? L"night.png" : L"day.png");
                    if (!renderer.render(timer, view, physicalFrame, {}) ||
                        !renderer.savePng((output / name).wstring(), physicalFrame)) {
                        return 12;
                    }
                }
                if (argc > 3 && square) {
                    const std::filesystem::path frames(argv[3]);
                    std::filesystem::create_directories(frames);
                    const auto name = std::wstring(dark ? L"physical-night-" : L"physical-day-") +
                                      std::to_wstring(1000 + index).substr(1) + L".png";
                    if (!renderer.render(timer, view, physicalFrame, {}) ||
                        !renderer.savePng((frames / name).wstring(), physicalFrame)) {
                        return 13;
                    }
                }
            }
        }
    }
    constexpr uint32_t width = 552, height = 264;
    std::vector<uint8_t> pixels(size_t(width) * height * 4);
    CdFrame frame{sizeof(frame), width, height, width * 4, pixels.size(), pixels.data(), 3};
    for (const auto* palette : {"amber", "mint", "cyan"}) {
        for (bool dark : {false, true}) {
            Options options;
            options.dark = dark;
            options.color = palette;
            Model model;
            model.advance(9, true);
            const std::wstring color(palette, palette + std::char_traits<char>::length(palette));
            const auto name = color + (dark ? L"-night.png" : L"-day.png");
            if (!renderer.render(model, options, frame, {}) || !renderer.savePng((output / name).wstring(), frame)) {
                return 2;
            }
            if (dark && options.color == "amber") {
                std::filesystem::copy_file(output / name, source / "packages/1x2/preview.png",
                                           std::filesystem::copy_options::overwrite_existing);
            }
        }
    }
    Model model;
    model.advance(30.5, true);
    if (!renderer.render(model, {}, frame, {}) || !renderer.savePng((output / L"turning.png").wstring(), frame)) {
        return 3;
    }
    model.reset(30);
    model.advance(12, true);
    Options transparent;
    transparent.background = false;
    if (!renderer.render(model, transparent, frame, {}) ||
        !renderer.savePng((output / L"transparent.png").wstring(), frame)) {
        return 4;
    }
    Options centered;
    centered.showTimer = false;
    if (!renderer.render(model, centered, frame, {}) ||
        !renderer.savePng((output / L"centered.png").wstring(), frame)) {
        return 5;
    }
    if (argc > 3) {
        const std::filesystem::path frames(argv[3]);
        std::filesystem::create_directories(frames);
        // An eight-second setting shows a full drain, flip and new cycle in the preview.
        for (bool dark : {false, true}) {
            Model loop;
            loop.reset(8);
            Options options;
            options.dark = dark;
            for (int index = 0; index < 240; ++index) {
                loop.advance(.05, true);
                const auto number = std::to_wstring(1000 + index).substr(1);
                const auto name = (dark ? L"night-" : L"day-") + number + L".png";
                if (!renderer.render(loop, options, frame, {}) || !renderer.savePng((frames / name).wstring(), frame)) {
                    return 6;
                }
            }
        }
    }
    constexpr uint32_t squareWidth = 264, squareHeight = 264;
    std::vector<uint8_t> squarePixels(size_t(squareWidth) * squareHeight * 4);
    CdFrame squareFrame{
        sizeof(squareFrame), squareWidth, squareHeight, squareWidth * 4, squarePixels.size(), squarePixels.data(), 3};
    for (const auto* palette : {"amber", "mint", "cyan"}) {
        for (bool dark : {false, true}) {
            Options options;
            options.showTimer = false;
            options.dark = dark;
            options.color = palette;
            Model squareModel;
            squareModel.advance(9, true);
            const std::wstring color(palette, palette + std::char_traits<char>::length(palette));
            const auto name = L"1x1-" + color + (dark ? L"-night.png" : L"-day.png");
            if (!renderer.render(squareModel, options, squareFrame, {}) ||
                !renderer.savePng((output / name).wstring(), squareFrame)) {
                return 7;
            }
            if (dark && options.color == "amber") {
                std::filesystem::copy_file(output / name, source / "packages/1x1/preview.png",
                                           std::filesystem::copy_options::overwrite_existing);
            }
        }
    }
    Options squareOptions;
    squareOptions.showTimer = false;
    Model turning;
    turning.advance(30.5, true);
    if (!renderer.render(turning, squareOptions, squareFrame, {}) ||
        !renderer.savePng((output / L"1x1-turning.png").wstring(), squareFrame)) {
        return 8;
    }
    squareOptions.background = false;
    model.reset(30);
    model.advance(12, true);
    if (!renderer.render(model, squareOptions, squareFrame, {}) ||
        !renderer.savePng((output / L"1x1-transparent.png").wstring(), squareFrame)) {
        return 9;
    }
    if (argc > 3) {
        const std::filesystem::path frames(argv[3]);
        for (bool dark : {false, true}) {
            Options options;
            options.dark = dark;
            options.showTimer = false;
            Model loop;
            loop.reset(8);
            for (int index = 0; index < 240; ++index) {
                loop.advance(.05, true);
                const auto number = std::to_wstring(1000 + index).substr(1);
                const auto name = (dark ? L"1x1-night-" : L"1x1-day-") + number + L".png";
                if (!renderer.render(loop, options, squareFrame, {}) ||
                    !renderer.savePng((frames / name).wstring(), squareFrame)) {
                    return 10;
                }
            }
        }
    }
    std::cout << "Both pixel hourglass native previews exported\n";
    return 0;
}
