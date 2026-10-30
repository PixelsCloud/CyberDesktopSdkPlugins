#include <Windows.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>

#include "plugins/widget-suite/Widget.h"

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

struct Panel {
    const char* id;
    const char* folder;
    widgetplugin::Kind kind;
    int columns, rows;
};

constexpr Panel kPanels[]{
    {"com.cyberdesktop.music.radial.mechanical.2x2", "radial-2x2", widgetplugin::Kind::Radial, 2, 2},
    {"com.cyberdesktop.music.spectrum.mechanical.1x4", "spectrum-1x4", widgetplugin::Kind::Bars, 4, 1}};

void verifyBallRelease() {
    widgetplugin::Spectrum normal, ball;
    std::array<float, widgetplugin::Spectrum::count> pulse{};
    pulse[12] = 1;
    normal.feed(pulse.data(), uint32_t(pulse.size()));
    ball.feed(pulse.data(), uint32_t(pulse.size()));
    normal.advance(.05f, false, 1, 40);
    ball.advance(.05f, false, 1, 40, widgetplugin::Spectrum::PeakMotion::FastBall);
    for (unsigned step = 0; step < 18; ++step) {
        normal.advance(1.f / 60, true, 1, 40);
        ball.advance(1.f / 60, true, 1, 40, widgetplugin::Spectrum::PeakMotion::FastBall);
        require(ball.peak[12] >= ball.level[12], "Falling ball must not pass through its rod");
    }
    require(ball.peak[12] > .65f && ball.peak[12] < .8f && normal.peak[12] > .7f,
            "Ball must keep visible separation after a beat while falling faster than a standard peak");
    std::cout << "PASS fast ball returns after a beat; standard peak hold preserved\n";
}
}  // namespace

int wmain(int argc, wchar_t** argv) try {
    require(argc == 3, "Expected repository and fixed output directory");
    verifyBallRelease();
    const std::filesystem::path root(argv[1]), output(argv[2]);
    const auto build = std::filesystem::absolute(argv[0]).parent_path();
    std::filesystem::create_directories(output);
    for (const auto& panel : kPanels) {
        const auto module = (build / "plugins" / panel.id).u8string();
        CdHostApi host{};
        host.struct_size = sizeof(host);
        host.abi_version = CD_PLUGIN_ABI_VERSION;
        host.request_redraw = [](void*) {
        };
        const CdCreateContext context{sizeof(CdCreateContext),
                                      CD_PLUGIN_ABI_VERSION,
                                      panel.id,
                                      "native-design",
                                      reinterpret_cast<const char*>(module.c_str()),
                                      &host};
        widgetplugin::Canvas canvas;
        canvas.bind(context, [](uint32_t) {
        });
        auto settings = widgetplugin::defaults(panel.kind, true);
        widgetplugin::Spectrum spectrum;
        std::array<float, widgetplugin::Spectrum::count> values{};
        for (size_t i = 0; i < values.size(); ++i) {
            const double t = double(i) / (values.size() - 1);
            values[i] = float(.035 + .38 * std::pow(.5 + .5 * std::sin(t * 22 - 1), 3) +
                              .035 * std::sin(i * 1.7) * std::sin(i * 1.7));
        }
        spectrum.feed(values.data(), uint32_t(values.size()));
        for (unsigned step = 0; step < 30; ++step) {
            spectrum.advance(1.f / 60, false, 2, 160,
                             panel.kind == widgetplugin::Kind::Bars ? widgetplugin::Spectrum::PeakMotion::FastBall
                                                                    : widgetplugin::Spectrum::PeakMotion::Default);
        }

        for (unsigned cell : {80u, 96u, 112u, 128u, 160u}) {
            for (double dpi : {1., 1.5, 2.}) {
                const double logicalWidth = panel.columns * cell - 6., logicalHeight = panel.rows * cell * 1.25 - 6.;
                const auto width = uint32_t(std::ceil(logicalWidth * dpi)),
                           height = uint32_t(std::ceil(logicalHeight * dpi));
                const uint32_t stride = width * 4 + 16;
                std::vector<uint8_t> pixels(size_t(stride) * height + 32, 0xa5);
                const CdFrame frame{sizeof(frame), width, height, stride, size_t(stride) * height, pixels.data(), dpi};
                const widgetplugin::Spec spec{panel.id, panel.kind, panel.rows, panel.columns, true};
                const auto paint = [&](const widgetplugin::Spectrum& signal) {
                    require(canvas.render(spec, settings, signal, {}, {}, 1, {}, 0, frame, {}),
                            "Native spectrum render");
                };
                paint(spectrum);
                for (unsigned attempt = 0; canvas.pendingImages() && attempt < 1000; ++attempt) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    paint(spectrum);
                }
                require(!canvas.pendingImages(), "Spectrum materials must finish decoding");
                paint(spectrum);
                const auto active = pixels;
                widgetplugin::Spectrum silent;
                paint(silent);
                require(pixels != active, "Live audio must move rods rather than displaying baked amplitudes");
                paint(spectrum);
                require(pixels == active, "Identical FFT and DPI must render deterministically");
                CdSettingValue peaks{sizeof(peaks), CD_SETTING_TOGGLE, "peaks", 0};
                require(settings.apply(&peaks, 1) == CD_OK, "Disable peak caps");
                paint(spectrum);
                require(pixels != active, "Peak toggle changes the visible hardware caps");
                peaks.integer = 1;
                require(settings.apply(&peaks, 1) == CD_OK, "Enable peak caps");
                paint(spectrum);
                for (uint32_t y = 0; y < height; ++y) {
                    for (uint32_t x = 0; x < width; ++x) {
                        const auto* p = pixels.data() + size_t(y) * stride + x * 4;
                        require(p[0] <= p[3] && p[1] <= p[3] && p[2] <= p[3], "Premultiplied alpha");
                        if (x < 2 || y < 2 || x + 2 >= width || y + 2 >= height) {
                            require(p[3] == 0, "Spectrum hardware must leave transparent outer space");
                        }
                    }
                    for (uint32_t x = width * 4; x < stride; ++x) {
                        require(pixels[size_t(y) * stride + x] == 0xa5, "Row guard");
                    }
                }
                for (size_t i = size_t(stride) * height; i < pixels.size(); ++i) {
                    require(pixels[i] == 0xa5, "Capacity guard");
                }
                const auto file = output / (std::string(panel.folder) + "-w" + std::to_string(cell) + "-dpi" +
                                            std::to_string(int(dpi * 100)) + ".png");
                require(canvas.savePng(file.wstring(), frame), "Save actual native spectrum preview");
                if (cell == 96 && dpi == 2) {
                    std::filesystem::copy_file(
                        file, root / "plugins/mechanical-instruments/packages" / panel.folder / "preview.png",
                        std::filesystem::copy_options::overwrite_existing);
                }
            }
        }
        std::cout << "PASS 15 grid/DPI captures, live/silent dynamics, peak caps, alpha and guards: " << panel.id
                  << '\n';
    }
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
