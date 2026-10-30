#include <CyberCanvas/PanelLayout.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>

#include "plugins/air-quality-suite/AirQuality.h"
#include "plugins/mechanical-instruments/PhysicsMaterials.h"
#include "plugins/physics-toys/Toys.h"

namespace {
struct Case {
    const char* id;
    const char* folder;
    int rows, columns;
    bool air, chime;
};

constexpr Case kCases[]{
    {"com.cyberdesktop.physics.springclock.mechanical.1x4", "mechanical-clock-1x4", 1, 4, false, false},
    {"com.cyberdesktop.physics.springclock.mechanical.2x4", "mechanical-clock-2x4", 2, 4, false, false},
    {"com.cyberdesktop.physics.windchime.mechanical.2x2", "mechanical-chime-2x2", 2, 2, false, true},
    {"com.cyberdesktop.airquality.mechanical.1x2", "air-1x2", 1, 2, true, false},
    {"com.cyberdesktop.airquality.mechanical.1x4", "air-1x4", 1, 4, true, false},
    {"com.cyberdesktop.airquality.mechanical.2x4", "air-2x4", 2, 4, true, false}};
}  // namespace

int wmain(int argc, wchar_t** argv) try {
    if (argc < 3) {
        return 1;
    }
    const std::filesystem::path root(argv[1]), output(argv[2]);
    const auto build = std::filesystem::absolute(argv[0]).parent_path();
    std::filesystem::create_directories(output);
    for (const auto& item : kCases) {
        const auto module = build / "plugins" / item.id;
        const auto directory = module.u8string();
        CdCreateContext context{sizeof(CdCreateContext), 1, item.id, "native-design",
                                reinterpret_cast<const char*>(directory.c_str())};
        const std::string suite = item.air ? "mechanical-instruments" : "physics-toys";
        auto settings = airplugin::defaults();
        airplugin::Snapshot data;
        data.valid = true;
        data.city = "济南";
        data.aqi = 42;
        data.pollutants = {37, 23, -1, -1, -1, -1};
        airplugin::Renderer air(module / "assets");
        air.bind(context);
        toys::Model model(item.chime ? toys::Kind::Chime : toys::Kind::Clock, item.rows == 1, true);
        model.setWindSeed(1337);
        model.setTime(10, 28, 36, false);
        toys::MechanicalRenderer toy;
        toy.bind(context, [] {
        });
        cdsdk::Canvas canvas;
        for (unsigned frame = 0; frame < 120; ++frame) {
            model.advance(1. / 60, item.chime ? .55 : 0, .75);
        }
        const auto paint = [&](unsigned cellWidth, double dpi, const std::filesystem::path& file) {
            const unsigned logicalWidth = item.columns * cellWidth - 6;
            const unsigned logicalHeight = item.rows * cellWidth * 5 / 4 - 6;
            const uint32_t width = uint32_t(logicalWidth * dpi), height = uint32_t(logicalHeight * dpi);
            std::vector<uint8_t> pixels(size_t(width) * height * 4);
            const CdFrame target{sizeof(CdFrame), width, height, width * 4, pixels.size(), pixels.data(), dpi};
            if (item.air) {
                if (!air.renderReference({item.id, item.rows, item.columns, true}, settings, data, false, target, {})) {
                    throw std::runtime_error("Air instrument did not render");
                }
            } else {
                const auto draw = [&] {
                    canvas.render(target, {}, [&](auto& c, auto) {
                        const auto view = cybercanvas::sceneViewport(
                            mechanicalart::physicsmaterials::sceneBounds(logicalWidth, logicalHeight), model.size());
                        c.translate(view.origin.x, view.origin.y);
                        c.scale(view.scale, view.scale);
                        toy.draw(c, model, {}, view.scale);
                    });
                };
                draw();
                for (unsigned attempt = 0; toy.pending() && attempt < 1000; ++attempt) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    draw();
                }
                if (toy.pending()) {
                    throw std::runtime_error("Hanging materials did not finish decoding");
                }
                draw();
            }
            if (!canvas.save(file.wstring(), target)) {
                throw std::runtime_error("Cannot save native widget image");
            }
        };
        for (unsigned width : {80u, 96u, 112u, 128u, 160u}) {
            for (double dpi : {1., 1.5, 2.}) {
                const auto name = std::string(item.folder) + "-w" + std::to_string(width) + "-dpi" +
                                  std::to_string(int(dpi * 100)) + ".png";
                const auto file = output / name;
                paint(width, dpi, file);
                if (width == 96 && dpi == 2) {
                    std::filesystem::copy_file(file,
                                               root / "plugins" / suite / "packages" / item.folder / "preview.png",
                                               std::filesystem::copy_options::overwrite_existing);
                }
            }
        }
        if (item.chime && argc > 3) {
            const auto frames = output / "chime-frames";
            std::filesystem::create_directories(frames);
            for (unsigned i = 0; i < 240; ++i) {
                model.advance(1. / 30, .55, .75);
                wchar_t name[32];
                swprintf_s(name, L"frame-%04u.png", i);
                paint(160, 2, frames / name);
            }
        }
        std::cout << "PASS native 15 grid/DPI captures: " << item.id << '\n';
    }
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 2;
}
