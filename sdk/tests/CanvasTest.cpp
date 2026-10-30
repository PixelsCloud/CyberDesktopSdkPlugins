#include <CyberCanvas/Animation.h>
#include <CyberCanvas/Images.h>
#include <sdk/Canvas.h>
#include <sdk/Language.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>
#include <vector>

namespace {
void savedBackgrounds(cdsdk::Canvas& canvas) {
    const auto directory = std::filesystem::temp_directory_path() /
                           (L"cybercanvas-sdk-backgrounds-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    std::vector<uint8_t> source{80, 40, 20, 128, 0, 0, 0, 0, 193, 193, 193, 193,
                                20, 30, 40, 255, 1, 0, 1, 1, 193, 193, 193, 193};
    const auto retained = source;
    CdFrame padded{sizeof(CdFrame), 2, 2, 12, source.size(), source.data(), 1.5};
    cybercanvas::ImageStore images;
    for (const uint8_t alpha : {uint8_t(0), uint8_t(128), uint8_t(255)}) {
        const CdColor background{40, 80, 160, alpha};
        const auto path = directory / (std::to_wstring(alpha) + L".png");
        if (!canvas.save(path.wstring(), padded, background) || source != retained) {
            throw std::runtime_error("PNG export changed source pixels or row padding");
        }
        auto request = images.request(path);
        auto decoded = request.snapshot();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (decoded.state == cybercanvas::ImageState::Loading && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            decoded = request.snapshot();
        }
        if (decoded.state != cybercanvas::ImageState::Ready || decoded.image->width() != 2 ||
            decoded.image->height() != 2) {
            throw std::runtime_error("PNG background fixture did not decode");
        }
        for (size_t y = 0; y < 2; ++y) {
            for (size_t x = 0; x < 2; ++x) {
                const auto* input = source.data() + y * 12 + x * 4;
                const auto* output = decoded.image->pixels().data() + (y * 2 + x) * 4;
                const int remainder = 255 - input[3];
                if (output[3] != input[3] + int(alpha) * remainder / 255) {
                    throw std::runtime_error("PNG export used incorrect background alpha");
                }
                const int rgb[]{background.r, background.g, background.b};
                for (size_t channel = 0; channel < 3; ++channel) {
                    const int expected = input[channel] + (rgb[channel] * alpha / 255) * remainder / 255;
                    // PNG stores straight alpha; allow only its final integer round trip.
                    if (std::abs(int(output[2 - channel]) - expected) > 1) {
                        throw std::runtime_error("PNG export used unpremultiplied background channels");
                    }
                }
            }
        }
        std::filesystem::remove(path);
    }
    std::filesystem::remove(directory);
    auto invalid = padded;
    invalid.capacity = 1;
    if (canvas.save((directory / L"invalid.png").wstring(), invalid, {0, 0, 0, 255})) {
        throw std::runtime_error("PNG export accepted an invalid buffer");
    }
    std::cout << "PASS transparent, translucent and opaque PNG backgrounds preserve premultiplied pixels and padding\n";
}
}  // namespace

int main() {
    std::vector<uint8_t> pixels(80 * 48 * 4);
    CdFrame output{sizeof(CdFrame), 80, 48, 80 * 4, pixels.size(), pixels.data(), 1};
    try {
        cdsdk::Canvas canvas;
        savedBackgrounds(canvas);
        auto invalid = output;
        invalid.struct_size = 0;
        if (canvas.valid(invalid)) {
            return 1;
        }
        canvas.render(output, {8, 8, 48, 32}, [](auto& draw, auto area) {
            draw.box(0, 0, area.width, area.height, 0, 0x224466FF);
            draw.text(L"画布", 12, {0, 0, area.width, area.height}, 0xFFFFFFFF);
        });
        if (pixels[0] != 0 || pixels[3] != 0 || pixels[(10 * 80 + 10) * 4 + 3] != 255) {
            return 1;
        }
        cdsdk::LanguageScope english("en");
        auto frame = canvas.frame(output);
        canvas.text(L"设置", 12, {0, 0, 80, 48}, 0xFFFFFFFF);
        frame.finish();
        auto path = canvas.path();
        path.moveTo({0, 0}).lineTo({10, 0}).lineTo({10, 10}).lineTo({0, 10}).close();
        cybercanvas::TextStyle style;
        style.size = 12;
        style.wrap = true;
        style.weight = cybercanvas::FontWeight::SemiBold;
        const auto size = canvas.measureText(L"设置", style, {80, 48});
        if (size.width <= 0 || size.height <= 0) {
            return 1;
        }
        canvas.render(output, {}, [&](auto& draw, auto) {
            cybercanvas::Tween<cybercanvas::Pose> tween({}, {.translation = {8, 8}, .opacity = .5});
            draw.posed(tween.sample(1).value, [&] {
                draw.fill(path, 0xFF0000FF);
            });
            draw.text(L"设置", style, {24, 0, 48, 48}, 0xFFFFFFFF);
        });
        const auto index = (12 * 80 + 12) * 4;
        if (pixels[index] != 128 || pixels[index + 3] != 128 || pixels[3] != 0) {
            return 1;
        }
        std::cout << "PASS SDK frame validation, safe content, language, text measurement, paths, scoped state and "
                     "animation\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
