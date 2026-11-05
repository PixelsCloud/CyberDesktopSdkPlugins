#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>

#include "plugins/clock-suite/Clock.h"
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

void modernSymmetry(Renderer& renderer, const std::filesystem::path& out) {
    const Time time{2026, 11, 4, 3, 23, 1, 38, 0};
    int states = 0;
    for (const auto display : {Display::Pixel, Display::PixelRound}) {
        for (const int columns : {2, 4}) {
            Spec spec{"com.cyberdesktop.clock.pixel.test", false, 1, columns, false, display};
            auto appearance = options(defaults(spec), {});
            for (const int unit : {72, 88, 104}) {
                const int width = columns * unit, height = unit * 5 / 4;
                for (const double scale : {70., 85., 100.}) {
                    for (const bool seconds : {false, true}) {
                        for (const int captions : {0, 1, 2, 3}) {
                            appearance.sizePercent = scale;
                            appearance.seconds = seconds;
                            appearance.date = (captions & 1) != 0;
                            appearance.weekday = (captions & 2) != 0;
                            const bool footer = captions || seconds && columns == 2;
                            const double bottom = footer ? std::clamp(height * .18, 12., 22.) : 0;
                            const Rect box{6, 6, width - 12., height - 12. - bottom - (footer ? 6 : 0)};
                            const int glyphColumns = seconds && columns == 4 ? 25 : 17;
                            const auto grid = arrangePixelGrid(glyphColumns, box, scale);
                            check(grid.glyphRow == grid.rows - grid.glyphRow - 7,
                                  "same number of inactive rows above and below digits");
                            check(grid.glyphColumn == grid.columns - grid.glyphColumn - glyphColumns,
                                  "same number of inactive columns left and right of digits");
                            const double top = grid.glyphRow * grid.pitchY;
                            const double bottomGap = box.height - ((grid.glyphRow + 6) * grid.pitchY + grid.side);
                            const double left = grid.glyphColumn * grid.pitchX;
                            const double rightGap =
                                box.width - ((grid.glyphColumn + glyphColumns - 1) * grid.pitchX + grid.side);
                            check(std::abs(top - bottomGap) < .0001 && std::abs(left - rightGap) < .0001,
                                  "glyph and full dot envelope share a center");
                            for (const double dpi : {1., 1.25, 1.5, 2.}) {
                                auto pixels = draw(renderer, spec, appearance, time, width, height, dpi);
                                const int w = int(width * dpi), h = int(height * dpi);
                                for (const bool active : {false, true}) {
                                    int x0 = w, y0 = h, x1 = -1, y1 = -1;
                                    const auto color =
                                        active ? appearance.appearance.foreground : appearance.appearance.track;
                                    for (int y = int(std::ceil(box.y * dpi)); y < int((box.y + box.height) * dpi);
                                         ++y) {
                                        for (int x = int(std::ceil(box.x * dpi)); x < int((box.x + box.width) * dpi);
                                             ++x) {
                                            const auto* pixel = &pixels[(size_t(y) * w + x) * 4];
                                            if (std::abs(int(pixel[0]) - int(color >> 24)) < 4 &&
                                                std::abs(int(pixel[1]) - int((color >> 16) & 255)) < 4 &&
                                                std::abs(int(pixel[2]) - int((color >> 8) & 255)) < 4) {
                                                x0 = std::min(x0, x);
                                                x1 = std::max(x1, x);
                                                y0 = std::min(y0, y);
                                                y1 = std::max(y1, y);
                                            }
                                        }
                                    }
                                    check(x1 >= x0 && y1 >= y0, "visible active and inactive pixel envelopes");
                                    check(std::abs((x0 + x1 + 1) - (box.x * 2 + box.width) * dpi) <= 2.01 &&
                                              std::abs((y0 + y1 + 1) - (box.y * 2 + box.height) * dpi) <= 2.01,
                                          "rasterized digits and grid centered to a physical pixel");
                                }
                                ++states;
                                if (unit == 88 && columns == 2 && scale == 100 && !seconds && !captions && dpi == 2) {
                                    CdFrame frame{sizeof(frame), uint32_t(w),   uint32_t(h), uint32_t(w * 4),
                                                  pixels.size(), pixels.data(), dpi};
                                    check(renderer.savePng(
                                              (out / (display == Display::Pixel ? L"modern-pixel-symmetric.png"
                                                                                : L"modern-round-symmetric.png"))
                                                  .wstring(),
                                              frame, {}),
                                          "save modern symmetric 23:01 sample");
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    std::cout << "PASS " << states << " modern pixel size/scale/footer/DPI states\n";
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        const std::filesystem::path out = argc > 1 ? argv[1] : L".electronic-previews";
        std::filesystem::create_directories(out);
        Renderer renderer;
        modernSymmetry(renderer, out);
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
