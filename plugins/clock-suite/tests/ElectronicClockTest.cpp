#include <Windows.h>

#include <algorithm>
#include <filesystem>
#include <iostream>

#include "Clock.h"
using namespace clockplugin;

namespace {
int checks = 0;

void check(bool ok, const char* text) {
    ++checks;
    if (!ok)
        throw std::runtime_error(text);
}

std::vector<uint8_t> draw(Renderer& renderer, Spec spec, Options options, Time time, int width, int height,
                          double dpr) {
    const uint32_t w = uint32_t(width * dpr), h = uint32_t(height * dpr);
    std::vector<uint8_t> bytes(size_t(w) * h * 4);
    CdFrame frame{sizeof(frame), w, h, w * 4, bytes.size(), bytes.data(), dpr};
    check(renderer.render(spec, options, time, frame), "render electronic clock");
    for (size_t i = 0; i < bytes.size(); i += 4)
        check(bytes[i] <= bytes[i + 3] && bytes[i + 1] <= bytes[i + 3] && bytes[i + 2] <= bytes[i + 3],
              "premultiplied RGBA");
    return bytes;
}

void equalMatrixMargins(Renderer& renderer, Spec spec, Options options, Time time, int width, int height, double dpr) {
    options.glowPercent = 0;
    options.date = options.weekday = false;
    if (spec.columns == 2)
        options.seconds = false;
    const auto pixels = draw(renderer, spec, options, time, width, height, dpr);
    const int w = int(width * dpr), h = int(height * dpr);
    int left = w, top = h, right = -1, bottom = -1;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const auto* p = &pixels[(size_t(y) * w + x) * 4];
            if (p[0] >= 43 && p[0] <= 50 && p[1] >= 34 && p[1] <= 41 && p[2] >= 45 && p[2] <= 52 && p[3] == 255) {
                left = std::min(left, x);
                right = std::max(right, x);
                top = std::min(top, y);
                bottom = std::max(bottom, y);
            }
        }
    check(right >= left && bottom >= top, "inactive matrix visible");
    const double margins[]{left + .5 - dpr, top + .5 - dpr, (width - 3) * dpr - right - .5,
                           (height - 4) * dpr - bottom - .5};
    check(*std::max_element(std::begin(margins), std::end(margins)) -
                  *std::min_element(std::begin(margins), std::end(margins)) <=
              1.01,
          "matrix has equal background margins on all four sides");
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        const std::filesystem::path out = argc > 1 ? argv[1] : L"electronic-previews";
        std::filesystem::create_directories(out);
        Renderer renderer;
        Time time{2026, 10, 13, 2, 22, 58, 30, 0};
        for (const auto display : {Display::Lcd, Display::Pixel, Display::PixelRound})
            for (int columns : {2, 4}) {
                Spec spec{"electronic", false, 1, columns, false, display};
                const auto family = display == Display::Lcd     ? L"lcd"
                                    : display == Display::Pixel ? L"pixel"
                                                                : L"pixelround";
                const auto name = std::wstring(family) + L"-1x" + std::to_wstring(columns);
                auto settings = defaults(spec);
                auto options = clockplugin::options(settings, {43, 41, 36, 255});
                auto glowing = draw(renderer, spec, options, time, columns * 77, 99, 2);
                auto flat = options;
                flat.glowPercent = 0;
                check(glowing != draw(renderer, spec, flat, time, columns * 77, 99, 2), "glow changes visible output");
                const auto alpha = [&](int x, int y) {
                    return glowing[(size_t(y) * columns * 77 * 2 + x) * 4 + 3];
                };
                check(alpha(0, 99) == 0 && alpha(2, 99) == 255 && alpha(columns * 77, 2) == 255,
                      "outer card uses spectrum face inset");
                if (display == Display::PixelRound) {
                    auto square = spec;
                    square.display = Display::Pixel;
                    check(glowing != draw(renderer, square, options, time, columns * 77, 99, 2),
                          "round dots differ from square dots");
                }
                auto next = time;
                next.minute = 59;
                check(glowing != draw(renderer, spec, options, next, columns * 77, 99, 2),
                      "minute update changes digits");
                check(glowing == draw(renderer, spec, options, time, columns * 77, 99, 2),
                      "deterministic static glow, no animation");
                const uint32_t w = uint32_t(columns * 77 * 2), h = 198;
                CdFrame frame{sizeof(frame), w, h, w * 4, glowing.size(), glowing.data(), 2};
                check(renderer.savePng((out / (name + L".png")).wstring(), frame, {0, 0, 0, 0}), "preview PNG");
                auto twelve = options;
                twelve.twelveHour = true;
                check(glowing != draw(renderer, spec, twelve, time, columns * 77, 99, 2), "12 hour format");
                for (const int cellHeight : {72, 129, 180}) {
                    auto tall = draw(renderer, spec, options, time, columns * 65, cellHeight, 1.5);
                    CdFrame f{sizeof(f),
                              uint32_t(columns * 65 * 1.5),
                              uint32_t(cellHeight * 1.5),
                              uint32_t(columns * 65 * 1.5) * 4,
                              tall.size(),
                              tall.data(),
                              1.5};
                    check(renderer.savePng((out / (name + L"-" + std::to_wstring(cellHeight) + L".png")).wstring(), f,
                                           {0, 0, 0, 0}),
                          "tall rectangular preview");
                    if (display != Display::Lcd)
                        for (double dpi : {1., 1.5, 2.})
                            equalMatrixMargins(renderer, spec, options, time, columns * 65, cellHeight, dpi);
                }
                for (int digit = 0; digit < 10; ++digit) {
                    Time sample = time;
                    sample.hour = digit;
                    sample.minute = digit * 6;
                    sample.second = digit * 6;
                    draw(renderer, spec, options, sample, columns * 77, 99, 1);
                }
                for (const auto text : {"en", "zh-CN"}) {
                    CdSettingValue language{sizeof(CdSettingValue), CD_SETTING_CHOICE, "language", 0, 0, text};
                    check(settings.apply(&language, 1) == CD_OK, "language setting");
                    auto translated = clockplugin::options(settings, {43, 41, 36, 255});
                    translated.date = translated.weekday = true;
                    draw(renderer, spec, translated, time, columns * 77, 99, 2);
                }
                CdSettingValue bad{sizeof(CdSettingValue), CD_SETTING_NUMBER, "glow_strength", 0, 101};
                check(settings.apply(&bad, 1) == CD_ERROR_ARGUMENT, "glow outside declared range rejected");
            }
        std::cout << "Electronic clock rendering, glow, aspect ratios and "
                     "settings: PASS\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
