#include <CyberCanvas/Canvas.h>

#include <array>
#include <filesystem>
#include <iostream>
#include <limits>
#include <thread>
#include <type_traits>
#include <vector>

namespace {
struct Buffer {
    explicit Buffer(uint32_t width = 80, uint32_t height = 48, double dpi = 1)
        : pixels((width * 4 + 16) * height + 16, 0xcd),
          frame{width, height, width * 4 + 16, pixels.size() - 16, pixels.data(), dpi} {}

    std::array<uint8_t, 4> pixel(uint32_t x, uint32_t y) const {
        const auto* value = pixels.data() + size_t(y) * frame.stride + x * 4;
        return {value[0], value[1], value[2], value[3]};
    }

    bool paddingIntact() const {
        for (uint32_t y = 0; y < frame.height; ++y) {
            for (uint32_t x = frame.width * 4; x < frame.stride; ++x) {
                if (pixels[size_t(y) * frame.stride + x] != 0xcd) {
                    return false;
                }
            }
        }
        return std::all_of(pixels.end() - 16, pixels.end(), [](uint8_t byte) {
            return byte == 0xcd;
        });
    }

    std::vector<uint8_t> pixels;
    cybercanvas::FrameBuffer frame;
};
}  // namespace

int main() {
    bool passed = true;
    const auto check = [&](const char* label, bool value) {
        passed &= value;
        std::cout << (value ? "PASS " : "FAIL ") << label << '\n';
    };
    try {
        static_assert(!std::is_copy_constructible_v<cybercanvas::Canvas>);
        cybercanvas::Canvas canvas;
        Buffer output;
        auto invalid = output.frame;
        invalid.capacity = 1;
        check("short frame capacity is rejected", !canvas.valid(invalid));
        invalid = output.frame;
        invalid.device_pixel_ratio = std::numeric_limits<double>::quiet_NaN();
        check("invalid DPI is rejected", !canvas.valid(invalid));
        invalid = output.frame;
        invalid.stride = UINT32_MAX;
        invalid.capacity = UINT64_MAX;
        check("WIC output sizes cannot truncate to 32 bits", !canvas.valid(invalid));
        canvas.render(output.frame, {}, [](auto& draw, auto) {
            draw.box(0, 0, 20, 20, 0, 0xFF0000FF);
            draw.box(20, 0, 20, 20, 0, 0x00FF0080);
        });
        check("output is RGBA with premultiplied alpha",
              output.pixel(8, 8) == std::array<uint8_t, 4>{255, 0, 0, 255} &&
                  output.pixel(28, 8) == std::array<uint8_t, 4>{0, 128, 0, 128});
        std::array<uint8_t, 16> translucent{81, 116, 81, 128, 0, 52, 73, 97, 1, 0, 1, 1, 33, 28, 12, 49};
        cybercanvas::FrameBuffer source{4, 1, 16, translucent.size(), translucent.data(), 1};
        canvas.render(output.frame, {}, [&](auto& draw, auto) {
            draw.blit(source, {0, 0, 4, 1});
        });
        bool exact = true;
        for (uint32_t x = 0; x < 4; ++x) {
            const auto pixel = output.pixel(x, 0);
            exact &= std::equal(pixel.begin(), pixel.end(), translucent.begin() + x * 4);
        }
        check("readback preserves translucent premultiplied channel values exactly", exact);
        check("row padding and trailing storage are untouched", output.paddingIntact());
        bool clipsExact = true;
        for (const double dpi : {1., 1.25, 1.5, 2.}) {
            for (const double offset : {0., .25, 1.}) {
                for (const double rotation : {0., 13.}) {
                    for (const double scale : {1., -1., .75}) {
                        Buffer optimized(96, 80, dpi), reference(96, 80, dpi);
                        const auto paint = [&](auto& draw, bool native) {
                            draw.translate(40 + offset, 16 + offset);
                            draw.rotate(rotation);
                            draw.scale(scale, 1);
                            const auto content = [&] {
                                draw.gradientBox({-30, -8, 60, 48}, 7, 0x5080b0c0, 0x10203080);
                                draw.oval(0, 12, 16.25, 10.75, 0xf0805060);
                            };
                            if (native) {
                                draw.target->PushAxisAlignedClip(D2D1::RectF(-20, 0, 20, 32),
                                                                 D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                                content();
                                draw.target->PopAxisAlignedClip();
                            } else {
                                draw.clipped({-20, 0, 40, 32}, content);
                            }
                        };
                        canvas.render(optimized.frame, {}, [&](auto& draw, auto) {
                            paint(draw, false);
                        });
                        canvas.render(reference.frame, {}, [&](auto& draw, auto) {
                            paint(draw, true);
                        });
                        clipsExact &= optimized.pixels == reference.pixels;
                    }
                }
            }
        }
        check("pixel-aligned clip optimization preserves native antialiased pixels across DPI and transforms",
              clipsExact);
        bool unalignedExact = true;
        for (const uint32_t width : {1u, 2u, 3u, 4u, 5u, 7u, 9u, 17u}) {
            constexpr uint32_t height = 3;
            std::vector<uint8_t> expected(size_t(width) * height * 4);
            for (size_t i = 0; i < expected.size(); i += 4) {
                const auto alpha = uint8_t((i * 37 + width * 11) % 256);
                expected[i] = uint8_t((i * 17 + 5) % (unsigned(alpha) + 1));
                expected[i + 1] = uint8_t((i * 13 + 7) % (unsigned(alpha) + 1));
                expected[i + 2] = uint8_t((i * 29 + 3) % (unsigned(alpha) + 1));
                expected[i + 3] = alpha;
            }
            cybercanvas::FrameBuffer input{width, height, width * 4, expected.size(), expected.data(), 1};
            for (const uint32_t offset : {1u, 2u, 3u}) {
                const auto stride = width * 4 + 7;
                std::vector<uint8_t> bytes(offset + size_t(stride) * height + 13, 0xcd);
                cybercanvas::FrameBuffer target{width, height, stride, size_t(stride) * height, bytes.data() + offset,
                                                1};
                canvas.render(target, {}, [&](auto& draw, auto) {
                    draw.blit(input, {0, 0, double(width), height});
                });
                for (uint32_t y = 0; y < height; ++y) {
                    const auto* actual = target.rgba + size_t(y) * stride;
                    unalignedExact &= std::equal(actual, actual + width * 4, expected.data() + size_t(y) * width * 4);
                    unalignedExact &= std::all_of(actual + width * 4, actual + stride, [](uint8_t byte) {
                        return byte == 0xcd;
                    });
                }
                unalignedExact &= std::all_of(bytes.begin(), bytes.begin() + offset, [](uint8_t byte) {
                    return byte == 0xcd;
                });
                unalignedExact &= std::all_of(bytes.end() - 13, bytes.end(), [](uint8_t byte) {
                    return byte == 0xcd;
                });
            }
        }
        check("unaligned odd-width output preserves channels, tails and padding", unalignedExact);
        bool mtaExact = false;
        std::thread mta([&] {
            const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            if (FAILED(initialized)) {
                return;
            }
            try {
                cybercanvas::Canvas threaded;
                std::array<uint8_t, 20> expected{81, 116, 81, 128, 0,  52, 73, 97, 1, 0,
                                                 1,  1,   33, 28,  12, 49, 7,  4,  9, 13};
                std::array<uint8_t, 32> bytes;
                bytes.fill(0xcd);
                cybercanvas::FrameBuffer input{5, 1, 20, expected.size(), expected.data(), 1};
                cybercanvas::FrameBuffer result{5, 1, 27, 27, bytes.data() + 1, 1};
                threaded.render(result, {}, [&](auto& draw, auto) {
                    draw.blit(input, {0, 0, 5, 1});
                });
                mtaExact = std::equal(expected.begin(), expected.end(), bytes.begin() + 1) && bytes.front() == 0xcd &&
                           std::all_of(bytes.begin() + 21, bytes.end(), [](uint8_t byte) {
                               return byte == 0xcd;
                           });
            } catch (...) {
                mtaExact = false;
            }
            CoUninitialize();
        });
        mta.join();
        check("existing MTA retains exact readback and caller padding", mtaExact);
        canvas.render(output.frame, {}, [](auto& draw, auto) {
            draw.box(0, 0, 10, 10, 0, 0x0000FFFF);
        });
        check("reused WIC surface reads the current frame and clears old pixels",
              output.pixel(8, 8) == std::array<uint8_t, 4>{0, 0, 255, 255} && output.pixel(28, 8)[3] == 0);
        canvas.render(output.frame, {8, 6, 20, 20}, [](auto& draw, auto area) {
            if (area.x != 8 || area.y != 6) {
                throw std::runtime_error("Safe area lost");
            }
            draw.clipped({0, 0, 8, 8}, [&] {
                draw.box(-10, -10, 60, 60, 0, 0xFFFFFFFF);
            });
        });
        check("safe origin and nested clips constrain drawing",
              output.pixel(10, 8)[3] == 255 && output.pixel(7, 8)[3] == 0 && output.pixel(17, 8)[3] == 0);
        bool threw = false;
        try {
            canvas.render(output.frame, {}, [](auto& draw, auto) {
                draw.clipped({0, 0, 5, 5}, [] {
                    throw std::runtime_error("fixture");
                });
            });
        } catch (const std::runtime_error&) {
            threw = true;
        }
        canvas.render(output.frame, {}, [](auto& draw, auto) {
            draw.box(0, 0, 12, 12, 0, 0xFF0000FF);
        });
        check("exceptions unwind clipping and allow the next frame", threw && output.pixel(10, 10)[0] == 255);
        {
            auto abandoned = canvas.frame(output.frame);
            canvas.box(0, 0, 12, 12, 0, 0x0000FFFF);
        }
        canvas.render(output.frame, {}, [](auto& draw, auto) {
            draw.box(0, 0, 12, 12, 0, 0x00FF00FF);
        });
        check("abandoned frame scopes recover without an explicit finish", output.pixel(8, 8)[1] == 255);
        for (double dpi : {1., 1.5, 2.}) {
            Buffer scaled(uint32_t(80 * dpi), uint32_t(48 * dpi), dpi);
            canvas.render(scaled.frame, {}, [](auto& draw, auto) {
                draw.box(10, 10, 20, 10, 0, 0xFFFFFFFF);
            });
            check("logical coordinates survive target DPI changes",
                  scaled.pixel(uint32_t(20 * dpi), uint32_t(15 * dpi))[3] == 255 &&
                      scaled.pixel(uint32_t(5 * dpi), uint32_t(15 * dpi))[3] == 0 && scaled.paddingIntact());
        }
        canvas.render(output.frame, {}, [](auto& draw, auto) {
            cybercanvas::TextStyle text;
            text.size = 24;
            draw.text(L"Canvas", text, {0, 0, 80, 40}, 0xFFFFFFFF);
        });
        bool grayscale = true, coverage = false;
        for (uint32_t y = 0; y < output.frame.height; ++y) {
            for (uint32_t x = 0; x < output.frame.width; ++x) {
                const auto p = output.pixel(x, y);
                grayscale &= p[0] == p[1] && p[1] == p[2] && p[0] <= p[3];
                coverage |= p[3] > 0 && p[3] < 255;
            }
        }
        check("transparent text uses grayscale antialiasing", grayscale && coverage);
        canvas.render(output.frame, {}, [](auto& draw, auto) {
            draw.gradientBox({0, 0, 80, 48}, 0, 0xFF0000FF, 0x0000FFFF);
        });
        check("cached gradient paints distinct endpoints", output.pixel(40, 4)[0] > output.pixel(40, 42)[0]);
        const auto png =
            std::filesystem::temp_directory_path() / ("cybercanvas-" + std::to_string(GetCurrentProcessId()) + ".png");
        check("PNG export succeeds", canvas.save(png.wstring(), output.frame));
        canvas.render(output.frame, {}, [&](auto& draw, auto) {
            draw.image(png.wstring(), {0, 0, 80, 48});
        });
        check("decoded PNGs render with the original color order", output.pixel(40, 4)[0] > output.pixel(40, 42)[0]);
        std::filesystem::remove(png);
        Buffer detailed(80, 640, 2);
        canvas.render(detailed.frame, {}, [](auto& draw, auto area) {
            draw.box(0, 0, area.width, area.height, 0, 0x0000FFFF);
            draw.box(0, 159.5, area.width, .5, 0, 0xFF0000FF);
        });
        const auto detailedPng = png.parent_path() / (png.stem().wstring() + L"-dpi.png");
        check("high DPI image fixture exports", canvas.save(detailedPng.wstring(), detailed.frame));
        canvas.render(detailed.frame, {}, [&](auto& draw, auto area) {
            draw.image(detailedPng.wstring(), {0, 0, area.width, area.height});
        });
        check("images retain single physical pixel details at high DPI",
              detailed.pixel(40, 319) == std::array<uint8_t, 4>{255, 0, 0, 255} &&
                  detailed.pixel(40, 320) == std::array<uint8_t, 4>{0, 0, 255, 255});
        std::filesystem::remove(detailedPng);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return passed ? 0 : 1;
}
