#include <filesystem>
#include <iostream>

#include "Clock.h"

int wmain(int argc, wchar_t** argv) try {
    if (argc < 2) {
        return 1;
    }
    const std::filesystem::path output(argv[1]);
    std::filesystem::create_directories(output);
    cdsdk::Canvas canvas;
    brickclock::Renderer renderer;
    const auto paint = [&](brickclock::Model& model, const std::string& palette, double dpi,
                           const std::filesystem::path& filename) {
        const uint32_t w = uint32_t(400 * dpi), h = uint32_t(100 * dpi);
        std::vector<uint8_t> bytes(size_t(w) * h * 4);
        CdFrame frame{sizeof(CdFrame), w, h, w * 4, bytes.size(), bytes.data(), dpi};
        canvas.render(frame, {}, [&](auto& drawing, auto) {
            drawing.scale(.4, .4);
            renderer.draw(drawing, model, palette, .4);
        });
        if (!canvas.save(filename.wstring(), frame)) {
            throw std::runtime_error("cannot save rendering");
        }
    };
    brickclock::Model model;
    model.setTime(10, 28, 46);
    for (const std::string colors : {"clay", "sand", "slate"}) {
        for (double dpi : {1., 1.5, 2.}) {
            const auto filename = output / (colors + "-" + std::to_string(int(dpi * 100)) + ".png");
            paint(model, colors, dpi, filename);
            if (argc > 2 && colors == "clay" && dpi == 2) {
                const std::filesystem::path preview(argv[2]);
                std::filesystem::create_directories(preview.parent_path());
                std::filesystem::copy_file(filename, preview, std::filesystem::copy_options::overwrite_existing);
            }
        }
    }
    for (double phase : {.03, .18, .65}) {
        brickclock::Model sample;
        sample.setTime(10, 28, 45);
        sample.setTime(10, 28, 46);
        for (double t = 0; t < phase; t += 1. / 60) {
            sample.advance(1. / 60);
        }
        paint(sample, "clay", 2, output / ("phase-" + std::to_string(int(phase * 1000)) + ".png"));
    }
    if (argc > 3) {
        const std::filesystem::path frames(argv[3]);
        std::filesystem::create_directories(frames);
        brickclock::Model animation;
        animation.setTime(10, 28, 45);
        for (int frame = 0; frame < 180; ++frame) {
            animation.setTime(10, 28, 45 + frame / 30);
            animation.advance(1. / 30);
            auto number = std::to_string(10000 + frame).substr(1);
            paint(animation, "clay", 2, frames / ("frame-" + number + ".png"));
        }
    }
    std::cout << "PASS 9 palette/DPI renders, 3 carving phases and optional 180-frame animation\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 2;
}
