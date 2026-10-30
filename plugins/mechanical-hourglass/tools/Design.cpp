#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>

#include "plugins/mechanical-hourglass/MechanicalHourglass.h"

namespace {
using namespace mechanicalhourglass;

void check(bool passed, const char* message) {
    if (!passed) {
        throw std::runtime_error(message);
    }
}

void render(Renderer& renderer, const Spec& spec, const Model& model, const PhysicalSand* sand,
            const cdsdk::Settings& settings, double width, double height, double dpi,
            const std::filesystem::path& file) {
    const auto w = uint32_t(std::ceil(width * dpi)), h = uint32_t(std::ceil(height * dpi));
    std::vector<uint8_t> pixels(size_t(w) * h * 4 + 32);
    std::fill(pixels.end() - 32, pixels.end(), uint8_t{0xa5});
    CdFrame frame{sizeof(frame), w, h, w * 4, size_t(w) * h * 4, pixels.data(), dpi};
    for (unsigned pass = 0; pass < 200; ++pass) {
        check(renderer.render(spec.columns == 2, model, sand, settings, frame), "Native render failed");
        if (renderer.ready()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(renderer.ready(), "Sprites failed to decode");
    check(std::all_of(pixels.end() - 32, pixels.end(),
                      [](auto p) {
                          return p == 0xa5;
                      }),
          "Buffer guard changed");
    check(renderer.save(file, frame), "Native PNG export failed");
    const auto builds = renderer.cacheBuilds();
    check(renderer.render(spec.columns == 2, model, sand, settings, frame) && renderer.cacheBuilds() == builds,
          "Unchanged native frame rebuilt its material cache");
}
}  // namespace

int wmain(int argc, wchar_t** argv) try {
    check(argc == 3, "Expected repository and fixed output directory");
    const std::filesystem::path root(argv[1]), output(argv[2]);
    unsigned count = 0;
    for (const auto& spec : mechanicalhourglass::kCatalog) {
        const auto path = (root / "build/relwithdebinfo/plugins" / spec.id).u8string();
        const std::string directory(reinterpret_cast<const char*>(path.data()), path.size());
        CdHostApi host{};
        host.struct_size = sizeof(host);
        host.abi_version = 1;
        CdCreateContext context{sizeof(context), 1, spec.id, "design", directory.c_str(), &host};
        Renderer renderer(context);
        auto settings = defaults(spec.columns == 2);
        Model model;
        PhysicalSand sand;
        // Real simulation at a representative elapsed time, only in this tool.
        for (unsigned frame = 0; frame < 240; ++frame) {
            model.advance(.05, true);
            sand.advance(.05, model);
        }
        for (unsigned cell : {80u, 96u, 112u, 128u, 160u}) {
            for (double dpi : {1., 1.25, 1.5, 2.}) {
                const auto file = output / (std::string(spec.folder) + "-w" + std::to_string(cell) + "-dpi" +
                                            std::to_string(unsigned(dpi * 100)) + ".png");
                render(renderer, spec, model, &sand, settings, spec.columns * cell - 6., cell * 1.25 - 6., dpi, file);
                if (cell == 96 && dpi == 2) {
                    std::filesystem::copy_file(
                        file, root / "plugins/mechanical-hourglass/packages" / spec.folder / "preview.png",
                        std::filesystem::copy_options::overwrite_existing);
                }
                ++count;
            }
        }
        for (double turning : {0., .31, .5, .69, .88}) {
            Model flip;
            flip.reset(5);
            flip.advance(5 + turning, true);
            const auto file =
                output / (std::string(spec.folder) + "-flip-" + std::to_string(int(flip.angle())) + ".png");
            render(renderer, spec, flip, nullptr, settings, spec.columns * 320 - 6., 394., 2, file);
            ++count;
        }
        render(renderer, spec, model, &sand, settings, spec.columns * 480 - 6., 594., 2,
               output / (std::string(spec.folder) + "-detail.png"));
    }
    std::cout << "PASS " << count << " native layout/DPI and flip renders, guarded frames and static cache reuse\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
}
