#include <sdk/Canvas.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <thread>

#include "plugins/mechanical-pixels/tools/TestHost.h"
#include "plugins/streetlink-desk/MatrixFace.h"
#include "plugins/streetlink-desk/Scene.h"

namespace {
void check(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct Buffer {
    CdFrame frame{};
    std::vector<uint8_t> data;

    Buffer(unsigned width, unsigned height, double dpi) {
        frame.struct_size = sizeof(CdFrame);
        frame.width = uint32_t(width * dpi);
        frame.height = uint32_t(height * dpi);
        frame.device_pixel_ratio = dpi;
        frame.stride = frame.width * 4 + 32;
        data.assign(size_t(frame.stride) * frame.height + 64, 0xa5);
        frame.rgba = data.data() + 32;
        frame.capacity = size_t(frame.stride) * frame.height;
    }

    void validate(double inset) const {
        for (unsigned i = 0; i < 32; ++i) {
            check(data[i] == 0xa5 && data[data.size() - 1 - i] == 0xa5, "Frame guard overwritten");
        }
        unsigned left = frame.width, right = 0, top = frame.height, bottom = 0, visible = 0;
        for (unsigned y = 0; y < frame.height; ++y) {
            for (unsigned x = 0; x < frame.width; ++x) {
                if (frame.rgba[size_t(y) * frame.stride + x * 4 + 3] > 32) {
                    left = std::min(left, x);
                    right = std::max(right, x);
                    top = std::min(top, y);
                    bottom = std::max(bottom, y);
                    ++visible;
                }
            }
            for (unsigned x = frame.width * 4; x < frame.stride; ++x) {
                check(frame.rgba[size_t(y) * frame.stride + x] == 0xa5, "Frame row padding overwritten");
            }
        }
        check(visible > frame.width * frame.height / 10, "Missing housing");
        const double minimum = inset * frame.device_pixel_ratio - 1;
        check(left >= minimum && top >= minimum && frame.width - right - 1 >= minimum &&
                  frame.height - bottom - 1 >= minimum,
              "Artwork violates the per-edge margin");
        check(std::abs(double(left) - (frame.width - right - 1)) < 3, "Horizontal silhouette is not centered");
        check(left / frame.device_pixel_ratio <= 9 && (frame.width - right - 1) / frame.device_pixel_ratio <= 9,
              "Horizontal end margins differ from Pixels");
    }
};
}  // namespace

int main(int argc, char** argv) try {
    check(argc == 2, "Usage: cd_streetlink_verify repository");
    const auto repo = std::filesystem::absolute(argv[1]);
    const auto output = repo / "build/relwithdebinfo/Testing/streetlink-desk";
    std::filesystem::create_directories(output);
    std::ifstream file(repo / "plugins/streetlink-desk/catalog.json");
    const auto catalog = nlohmann::json::parse(file);
    nlohmann::json results = nlohmann::json::array();
    std::vector<std::unique_ptr<mechanicalpixels::testing::Host>> hosts;
    std::vector<std::unique_ptr<streetlink::MatrixFace>> faces;
    for (const auto& item : catalog) {
        if (!item.at("nativeClock").get<bool>()) {
            continue;
        }
        auto host = std::make_unique<mechanicalpixels::testing::Host>();
        const bool round = item.at("folder").get<std::string>().find("round") != std::string::npos;
        const auto& spec = mechanicalpixels::kCatalog[round ? 1 : 0];
        const auto directory = (repo / "build/relwithdebinfo/plugins" / item.at("id").get<std::string>()).string();
        auto context = host->context(spec, directory.c_str());
        faces.push_back(std::make_unique<streetlink::MatrixFace>(context));
        hosts.push_back(std::move(host));
    }
    for (unsigned index = 0; index < faces.size(); ++index) {
        auto& face = *faces[index];
        const auto folder = catalog[index].at("folder").get<std::string>();
        const auto kind = folder.find("round") != std::string::npos ? mechanicalpixels::Kind::ClockRound
                                                                    : mechanicalpixels::Kind::ClockSquare;
        auto settings = mechanicalpixels::defaults(kind);
        const CdSettingValue colors[]{{sizeof(CdSettingValue), CD_SETTING_COLOR, "text_color", 0xffffffff},
                                      {sizeof(CdSettingValue), CD_SETTING_COLOR, "seconds_color", 0xffffffff}};
        check(settings.apply(colors, 2) == CD_OK, "Defaults rejected");
        for (const double dpi : {1., 1.25, 1.5, 2., 1.}) {
            for (const unsigned width : {320u, 400u}) {
                Buffer buffer(width, 100, dpi);
                const CdRect safe{0, 0, double(width), 100};
                for (unsigned attempt = 0; attempt < 200; ++attempt) {
                    check(face.render(kind, settings, "21:37:08", true, {}, buffer.frame, safe), "Rendering rejected");
                    if (face.ready()) {
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    hosts[index]->advance(33);
                }
                check(face.ready(), "Image decoding did not finish");
                check(face.render(kind, settings, "21:37:08", true, {}, buffer.frame, safe), "Loaded frame rejected");
                buffer.validate(6);
                const auto builds = face.cacheBuilds();
                auto before = buffer.data;
                check(face.render(kind, settings, "21:37:09", true, {}, buffer.frame, safe), "Next tick rejected");
                check(buffer.data != before, "Time is baked into the image");
                check(face.cacheBuilds() == builds, "Every clock tick rebuilds the static housing");
                check(face.referencedBytes() < uint64_t(buffer.frame.width) * buffer.frame.height * 6,
                      "Full-resolution source retained instead of current-DPI image");
                const auto png =
                    output / (folder + "-" + std::to_string(width) + "-" + std::to_string(int(dpi * 100)) + ".png");
                check(face.save(png, buffer.frame), "Saving native verification failed");
                face.pause(true);
                face.pause(false);
                check(face.render(kind, settings, "12:00:00", false, {}, buffer.frame, safe), "Resume rejected");
                buffer.validate(6);
                results.push_back({{"folder", folder},
                                   {"width", width},
                                   {"dpi", dpi},
                                   {"cacheBuilds", builds},
                                   {"imageBytes", face.referencedBytes()},
                                   {"passed", true}});
            }
        }
    }
    std::ofstream report(output / "native-results.json");
    report << results.dump(2) << '\n';
    std::cout << results.size() << " native size/DPI/cache/stride/resume cases passed\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << "FAIL " << e.what() << '\n';
    return 1;
}
