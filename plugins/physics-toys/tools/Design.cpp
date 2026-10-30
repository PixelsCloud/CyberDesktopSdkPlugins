#include <filesystem>
#include <iostream>

#include "Toys.h"

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) {
        return 1;
    }
    const std::filesystem::path out(argv[1]), source(argv[2]);
    std::filesystem::create_directories(out);
    cdsdk::Canvas canvas;
    for (const std::string variant : {"chime-2x2", "chime-3x2", "clock-2x4", "clock-1x4"}) {
        const bool clock = variant.starts_with("clock"), compact = variant == "clock-1x4";
        const unsigned width = clock ? 376 : 184, height = compact ? 88 : variant == "chime-3x2" ? 280 : 184;
        for (bool dark : {false, true}) {
            for (double dpi : {1., 1.5, 2.}) {
                toys::Model model(clock ? toys::Kind::Clock : toys::Kind::Chime, compact);
                model.setTime(10, 28, 36, false);
                for (int n = 0; n < 90; ++n) {
                    model.advance(1. / 60, clock ? 0 : .35, .75);
                }
                const auto size = model.size();
                const auto w = uint32_t(width * dpi), h = uint32_t(height * dpi);
                std::vector<uint8_t> bytes(size_t(w) * h * 4);
                const CdFrame frame{sizeof(CdFrame), w, h, w * 4, bytes.size(), bytes.data(), dpi};
                for (bool background : {true, false}) {
                    canvas.render(frame, {}, [&](auto& c, auto) {
                        if (background) {
                            c.box(0, 0, width, height, 12, dark ? 0x24363bff : 0xfaf6edff);
                        }
                        const double scale = std::min(width / size.width, height / size.height);
                        c.translate((width - size.width * scale) / 2, (height - size.height * scale) / 2);
                        c.scale(scale, scale);
                        toys::draw(c, model, {dark, background});
                    });
                    const auto name = variant + (dark ? "-night-" : "-day-") + std::to_string(int(dpi * 100)) +
                                      (background ? "" : "-transparent") + ".png";
                    if (!canvas.save((out / name).wstring(), frame)) {
                        return 2;
                    }
                    if (dark && dpi == 2 && background) {
                        std::filesystem::create_directories(source / "packages" / variant);
                        std::filesystem::copy_file(out / name, source / "packages" / variant / "preview.png",
                                                   std::filesystem::copy_options::overwrite_existing);
                    }
                }
            }
        }
    }
    std::cout << "48 theme/DPI/transparent previews saved\n";
    if (argc > 3) {
        const std::filesystem::path frames(argv[3]);
        std::filesystem::create_directories(frames);
        for (bool clock : {false, true}) {
            toys::Model model(clock ? toys::Kind::Clock : toys::Kind::Chime);
            model.setTime(10, 28, 36, false);
            const auto area = model.size();
            const uint32_t w = clock ? 720 : 520, h = clock ? 370 : 580;
            std::vector<uint8_t> rgba(size_t(w) * h * 4);
            CdFrame frame{sizeof(CdFrame), w, h, w * 4, rgba.size(), rgba.data(), 2};
            for (int n = 0; n < 120; ++n) {
                if (clock) {
                    model.setTime(10, 28, 36 + n / 30, false);
                }
                if (n == 70) {
                    model.beginDrag(0, clock ? toys::Point{.8, 1.4} : toys::Point{1.05, 1.7});
                }
                if (n == 85) {
                    model.cancelDrag();
                }
                model.advance(1. / 30, clock ? 0 : .35, .75);
                canvas.render(frame, {}, [&](auto& c, auto) {
                    c.box(0, 0, area.width, area.height, 12, 0x24363bff);
                    toys::draw(c, model, {true, true});
                });
                const auto name =
                    std::wstring(clock ? L"clock-" : L"chime-") + std::to_wstring(1000 + n).substr(1) + L".png";
                if (!canvas.save((frames / name).wstring(), frame)) {
                    return 3;
                }
            }
        }
    }
}
