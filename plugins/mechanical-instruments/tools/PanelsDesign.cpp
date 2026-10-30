#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

#include "plugins/mechanical-instruments/InstrumentPlates.h"
#include "plugins/mechanical-sign/Sign.h"
#include "plugins/productivity-suite/Productivity.h"
#include "plugins/system-info-suite/Monitor.h"

namespace {
std::string utf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

struct Panel {
    const char* folder;
    const char* id;
    unsigned columns, rows;
};

constexpr Panel kPanels[]{{"monitor-2x2", "com.cyberdesktop.systeminfo.mechanical.2x2", 2, 2},
                          {"monitor-1x2", "com.cyberdesktop.systeminfo.mechanical.1x2", 2, 1},
                          {"monitor-1x1", "com.cyberdesktop.systeminfo.mechanical.1x1", 1, 1},
                          {"monitor-1x4", "com.cyberdesktop.systeminfo.mechanical.1x4", 4, 1},
                          {"network-1x4", "com.cyberdesktop.network.mechanical.1x4", 4, 1},
                          {"sign-4x1", "com.cyberdesktop.sign.mechanical.6x2", 4, 1}};
}  // namespace

int wmain(int argc, wchar_t** argv) try {
    require(argc == 3, "Expected repository and fixed validation output paths");
    const std::filesystem::path root(argv[1]), output(argv[2]);
    std::filesystem::create_directories(output);
    const auto build = root / "build/relwithdebinfo";
    unsigned renders = 0;
    for (const auto& panel : kPanels) {
        const bool monitor = panel.folder[0] == 'm', network = panel.folder[0] == 'n';
        const auto directory = utf8(build / "plugins" / panel.id);
        CdHostApi host{};
        host.struct_size = sizeof(host);
        host.abi_version = CD_PLUGIN_ABI_VERSION;
        host.request_redraw = [](void*) {
        };
        CdCreateContext context{sizeof(context), CD_PLUGIN_ABI_VERSION, panel.id, "design", directory.c_str(), &host};
        monitorplugin::Renderer gauges;
        gauges.bind(context, [] {
        });
        monitorplugin::Options options;
        options.fontFamily = "Microsoft YaHei UI";
        options.primary = "ram";
        productivity::Renderer rates;
        rates.bind(context, [](uint32_t) {
        });
        const auto settings = productivity::defaults(productivity::Kind::Network, true);
        productivity::Data data;
        data.valid = true;
        data.down = uint64_t(33.7 * 1024);
        data.up = 230 * 1024;
        for (unsigned i = 0; i < 60; ++i) {
            const double download =
                .16 + .22 * std::abs(std::sin(i * .51)) + .19 * std::abs(std::sin(i * 1.91)) + (i % 11 == 4 ? .65 : 0);
            const double upload =
                .09 + .20 * std::abs(std::cos(i * .37)) + .14 * std::abs(std::sin(i * 1.13)) + (i % 17 == 6 ? .8 : 0);
            data.history.emplace_back(uint64_t(data.down * download), uint64_t(data.up * upload));
        }
        mechanicalsign::Renderer sign(root / "plugins/mechanical-sign/assets");
        mechanicalsign::Model model;
        model.finish();
        cdsdk::Canvas canvas;
        for (unsigned cell : {80u, 96u, 112u, 128u, 160u}) {
            for (double dpi : {1., 1.5, 2.}) {
                const double w = panel.columns * cell - 6., h = panel.rows * cell * 1.25 - 6.;
                const auto width = uint32_t(std::ceil(w * dpi)), height = uint32_t(std::ceil(h * dpi));
                std::vector<uint8_t> bytes(size_t(width) * height * 4 + 32, 0);
                std::fill(bytes.end() - 32, bytes.end(), uint8_t{0xa5});
                const CdFrame frame{sizeof(frame), width, height, width * 4, size_t(width) * height * 4,
                                    bytes.data(),  dpi};
                for (unsigned pass = 0; pass < 30; ++pass) {
                    if (monitor) {
                        require(gauges.render({panel.id, int(panel.rows), int(panel.columns), true}, options,
                                              {23, 90, 7, 77,
                                               CD_SYSTEM_CPU_VALID | CD_SYSTEM_MEMORY_VALID | CD_SYSTEM_GPU_VALID |
                                                   CD_SYSTEM_CPU_TEMPERATURE_VALID},
                                              frame),
                                "Monitor reference render failed");
                    } else if (network) {
                        require(rates.render({panel.id, productivity::Kind::Network, 1, 4, true}, settings, data, frame,
                                             {}),
                                "Network reference render failed");
                    } else {
                        canvas.render(frame, {}, [&](auto& drawing, auto) {
                            const auto plate = mechanicalart::InstrumentPlate{{2172, 724}, {31, 138, 2107, 459}, {}, 0};
                            const auto stage = mechanicalart::arrangePlate(plate, w, h);
                            stage.draw(drawing, [&] {
                                sign.draw(drawing, model, true, stage.scale, mechanicalsign::Format::Compact);
                            });
                        });
                    }
                    if (pass < 29) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    }
                }
                require(std::all_of(bytes.end() - 32, bytes.end(),
                                    [](uint8_t byte) {
                                        return byte == 0xa5;
                                    }),
                        "Panel rendering crossed the output buffer boundary");
                if (monitor) {
                    unsigned green = 0, yellow = 0, red = 0;
                    for (size_t offset = 0; offset < frame.capacity; offset += 4) {
                        const auto* pixel = bytes.data() + offset;
                        green += pixel[1] > pixel[0] * 1.2 && pixel[1] > pixel[2] * 1.15 && pixel[3] > 150;
                        yellow += pixel[0] > pixel[2] * 1.4 && pixel[1] > pixel[2] * 1.4 && pixel[3] > 150;
                        red += pixel[0] > pixel[1] * 1.35 && pixel[0] > pixel[2] * 1.35 && pixel[3] > 150;
                    }
                    require(green && yellow && red, "Each monitor must visibly retain all three scale zones");
                }
                const std::string name = std::string(panel.folder) + "-w" + std::to_string(cell) + "-dpi" +
                                         std::to_string(int(dpi * 100)) + ".png";
                const auto file = output / name;
                require(canvas.save(file.wstring(), frame), "Could not save native panel reference");
                if (cell == 96 && dpi == 2) {
                    const auto destination = root /
                                             (monitor || network ? "plugins/mechanical-instruments/packages"
                                                                 : "plugins/mechanical-sign/packages") /
                                             (monitor || network ? panel.folder : "6x2") / "preview.png";
                    std::filesystem::copy_file(file, destination, std::filesystem::copy_options::overwrite_existing);
                }
                ++renders;
            }
        }
    }
    std::cout << renders
              << " approved-panel reference renders: real PNGs, three color zones and guarded buffers PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
}
