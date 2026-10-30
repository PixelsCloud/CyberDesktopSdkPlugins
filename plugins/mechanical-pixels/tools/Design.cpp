#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>

#include "plugins/mechanical-pixels/Pixels.h"

namespace {
std::string utf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

void check(bool result, const char* message) {
    if (!result) {
        throw std::runtime_error(message);
    }
}
}  // namespace

int wmain(int argc, wchar_t** argv) try {
    using namespace mechanicalpixels;
    check(argc == 3, "Expected repository and fixed testing output paths");
    const std::filesystem::path root(argv[1]), output(argv[2]);
    std::filesystem::create_directories(output);
    unsigned count = 0;
    for (const auto& spec : kCatalog) {
        const auto directory = utf8(root / "build/relwithdebinfo/plugins" / spec.id);
        CdHostApi host{};
        host.struct_size = sizeof(host);
        host.abi_version = 1;
        CdCreateContext context{sizeof(context), 1, spec.id, "design", directory.c_str(), &host};
        Renderer renderer(context);
        auto settings = defaults(spec.kind);
        widgetplugin::Spectrum spectrum;
        // Demonstration data belongs only to this design tool, never the plugin.
        for (unsigned i = 0; i < spectrum.count; ++i) {
            spectrum.level[i] = std::min(1.f, float(.09 + .28 * std::abs(std::sin(i * .19 + .3)) +
                                                    .13 * std::abs(std::sin(i * .67)) + (i % 19 == 9 ? .60 : 0)));
            spectrum.peak[i] = std::min(1.f, spectrum.level[i] + (i % 9 == 4 ? .20f : .02f));
        }
        for (unsigned cell : {80u, 96u, 112u, 128u, 160u}) {
            for (double dpi : {1., 1.5, 2.}) {
                const double width = spec.columns * cell - 6., height = cell * 1.25 - 6.;
                const auto w = uint32_t(std::ceil(width * dpi)), h = uint32_t(std::ceil(height * dpi));
                std::vector<uint8_t> bytes(size_t(w) * h * 4 + 32, 0);
                std::fill(bytes.end() - 32, bytes.end(), uint8_t{0xa5});
                CdFrame frame{sizeof(frame), w, h, w * 4, size_t(w) * h * 4, bytes.data(), dpi};
                for (unsigned pass = 0; pass < 40; ++pass) {
                    check(renderer.render(spec.kind, settings, "10:08:30", true, spectrum, frame, {}), "Render failed");
                    if (renderer.ready() && pass >= 3) {
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                check(renderer.ready(), "The real chassis did not finish decoding");
                check(std::all_of(bytes.end() - 32, bytes.end(),
                                  [](uint8_t byte) {
                                      return byte == 0xa5;
                                  }),
                      "Render crossed the frame buffer guard");
                const auto name = std::string(spec.folder) + "-w" + std::to_string(cell) + "-dpi" +
                                  std::to_string(unsigned(dpi * 100)) + ".png";
                check(renderer.save(output / name, frame), "Could not save actual native preview");
                if (cell == 96 && dpi == 2) {
                    std::filesystem::copy_file(
                        output / name, root / "plugins/mechanical-pixels/packages" / spec.folder / "preview.png",
                        std::filesystem::copy_options::overwrite_existing);
                }
                const auto cached = renderer.cacheBuilds();
                check(renderer.render(spec.kind, settings, "11:22:33", false, {}, frame, {}), "Second render failed");
                check(renderer.cacheBuilds() == cached, "Live values rebuilt an unchanged chassis or matrix");
                ++count;
            }
        }
    }
    std::cout << "PASS " << count << " native layout/DPI renders, guarded frames and static cache reuse\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
}
