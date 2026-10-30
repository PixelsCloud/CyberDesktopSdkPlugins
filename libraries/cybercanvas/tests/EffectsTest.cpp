#include <CyberCanvas/Effects.h>
#include <CyberCanvas/EffectsDrawing.h>
#include <CyberCanvas/MemoryBudget.h>
#include <CyberCanvas/Native.h>
#include <d3d11.h>

#include <algorithm>
#include <array>
#include <iostream>
#include <thread>

namespace cybercanvas {
void injectEffectDeviceLossForTest();
void useFullEffectSurfaceForTest(bool enabled);
}  // namespace cybercanvas

namespace {
bool passed = true;

void check(const char* name, bool value) {
    passed &= value;
    std::cout << (value ? "PASS " : "FAIL ") << name << '\n';
}

template <class F>
bool rejects(F action) {
    try {
        action();
        return false;
    } catch (const std::exception&) {
        return true;
    }
}

uint8_t channel(const cybercanvas::EffectResult& result, int x, int y, unsigned channel) {
    x -= int(result.offset.x * result.devicePixelRatio);
    y -= int(result.offset.y * result.devicePixelRatio);
    return result.image->pixels()[(size_t(y) * result.image->width() + x) * 4 + channel];
}

std::shared_ptr<const cybercanvas::ImageData> policyPicture(uint32_t width, uint32_t height) {
    std::vector<uint8_t> pixels(size_t(width) * height * 4);
    for (uint32_t y = height / 4; y < height * 3 / 4; ++y) {
        for (uint32_t x = width / 4; x < width * 3 / 4; ++x) {
            auto* pixel = pixels.data() + (size_t(y) * width + x) * 4;
            pixel[0] = 32;
            pixel[1] = 64;
            pixel[2] = 96;
            pixel[3] = 128;
        }
    }
    return cybercanvas::ImageData::fromPremultipliedBgra(width, height, std::move(pixels));
}

void policyEquivalent(const cybercanvas::EffectResult& actual, const cybercanvas::ImageData& input,
                      const cybercanvas::EffectStyle& style, double dpi, cybercanvas::EffectBackend backend) {
    cybercanvas::EffectSurface reference(backend);
    const std::array chain{style};
    const auto expected = reference.render(input, chain, dpi);
    check("selected backend preserves independently rendered pixels and geometry",
          actual.image->pixels() == expected.image->pixels() && actual.image->width() == expected.image->width() &&
              actual.image->height() == expected.image->height() && actual.offset.x == expected.offset.x &&
              actual.offset.y == expected.offset.y);
}

void verifyAutomaticBackendPolicy() {
    using namespace cybercanvas;
    ComPtr<ID3D11Device> hardware;
    const bool hasHardware =
        SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                    nullptr, 0, D3D11_SDK_VERSION, &hardware, nullptr, nullptr));
    D3D11_FEATURE_DATA_D3D11_OPTIONS2 options{};
    const bool uma =
        hasHardware &&
        SUCCEEDED(hardware->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS2, &options, sizeof(options))) &&
        options.UnifiedMemoryArchitecture;
    const auto largeBackend = hasHardware && !uma ? EffectBackend::Hardware : EffectBackend::Warp;
    const auto input = policyPicture(500, 500);
    EffectSurface automatic;
    std::array styles{EffectStyle{EffectKind::Blur, 2, {}}};
    const auto initial = automatic.render(*input, styles);
    const auto retainedPixels = initial.image->pixels();
    check("512 by 512 padded output uses WARP", initial.image->width() == 512 && initial.image->height() == 512 &&
                                                    automatic.stats().backend == EffectBackend::Warp);
    policyEquivalent(initial, *input, styles[0], 1, EffectBackend::Warp);
    const auto cached = automatic.render(*input, styles);
    check("unchanged small output reuses snapshot and device",
          cached.image == initial.image && automatic.stats().cacheHits == 1 && automatic.stats().deviceRebuilds == 1);

    styles[0].sigma = 2.1;
    const auto expanded = automatic.render(*input, styles);
    check("effect padding can cross the backend threshold", expanded.image->width() == 514 &&
                                                                automatic.stats().backend == largeBackend &&
                                                                automatic.stats().deviceRebuilds == 2);
    policyEquivalent(expanded, *input, styles[0], 1, largeBackend);
    styles[0].sigma = 2;
    const auto restored = automatic.render(*input, styles);
    check("returning below threshold rebuilds correct backend", automatic.stats().backend == EffectBackend::Warp &&
                                                                    automatic.stats().deviceRebuilds == 3 &&
                                                                    restored.image->pixels() == retainedPixels);

    const auto scaled = automatic.render(*input, styles, 2);
    check("DPI uses physical padded output size", scaled.image->width() == 524 &&
                                                      automatic.stats().backend == largeBackend &&
                                                      automatic.stats().deviceRebuilds == 4);
    policyEquivalent(scaled, *input, styles[0], 2, largeBackend);
    const auto wide = policyPicture(1024, 128);
    styles[0].sigma = 0;
    const auto thin = automatic.render(*wide, styles);
    check("small pixel area is independent of aspect ratio", automatic.stats().backend == EffectBackend::Warp &&
                                                                 automatic.stats().deviceRebuilds == 5 &&
                                                                 thin.image->width() == 1024);
    policyEquivalent(thin, *wide, styles[0], 1, EffectBackend::Warp);
    check("published snapshots survive backend and size changes", initial.image->pixels() == retainedPixels);

    for (const auto backend : {EffectBackend::Warp, EffectBackend::Hardware}) {
        if (backend == EffectBackend::Hardware && !hasHardware) {
            std::cout << "SKIP explicit hardware unavailable\n";
            continue;
        }
        EffectSurface explicitSurface(backend);
        for (const double dpi : {1., 2., 1.}) {
            styles[0].sigma = 2;
            explicitSurface.render(*input, styles, dpi);
            check("explicit backend is stable across threshold changes",
                  explicitSurface.stats().backend == backend && explicitSurface.stats().deviceRebuilds == 1);
        }
    }
}
}  // namespace

int main() {
    using namespace cybercanvas;
    std::cout << std::unitbuf;
    try {
        require(CoInitializeEx(nullptr, COINIT_MULTITHREADED));

        struct ComScope {
            ~ComScope() {
                CoUninitialize();
            }
        } com;

        verifyAutomaticBackendPolicy();

        std::vector<uint8_t> pixels(64 * 64 * 4);
        for (int y = 8; y < 56; ++y) {
            for (int x = 8; x < 56; ++x) {
                for (int c = 0; c < 4; ++c) {
                    pixels[(y * 64 + x) * 4 + c] = 255;
                }
            }
        }
        const auto source = ImageData::fromPremultipliedBgra(64, 64, pixels);
        auto changedPixels = pixels;
        changedPixels[(32 * 64 + 32) * 4 + 2] = 0;
        const auto changedSource = ImageData::fromPremultipliedBgra(64, 64, std::move(changedPixels));
        std::vector<uint8_t> sparsePixels(128 * 128 * 4);
        for (int y = 32; y < 96; ++y) {
            for (int x = 32; x < 96; ++x) {
                auto* pixel = sparsePixels.data() + (size_t(y) * 128 + x) * 4;
                pixel[0] = 32;
                pixel[1] = 64;
                pixel[2] = 96;
                pixel[3] = (x + y) % 7 ? 255 : 128;
            }
        }
        const auto sparseSource = ImageData::fromPremultipliedBgra(128, 128, std::move(sparsePixels));
        std::vector<EffectBackend> backends{EffectBackend::Automatic, EffectBackend::Warp};
        {
            ComPtr<ID3D11Device> hardware;
            const auto available =
                D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr,
                                  0, D3D11_SDK_VERSION, &hardware, nullptr, nullptr);
            if (SUCCEEDED(available)) {
                backends.push_back(EffectBackend::Hardware);
            } else {
                std::cout << "SKIP explicit hardware: device creation unavailable, HRESULT " << available << '\n';
            }
        }
        for (const auto backend : backends) {
            // The unbounded D2D output is an independent reference for the output
            // clipping optimization. Keep its complete input and QUALITY kernel.
            // WARP may round separable-filter intermediates differently for different
            // render regions; allow two 8-bit levels, but no geometric displacement.
            EffectSurface clipped(backend), full(backend);
            bool equivalent = true;
            unsigned maximumDifference = 0;
            for (const auto& sample : {source, sparseSource}) {
                for (const auto kind :
                     {EffectKind::Blur, EffectKind::Shadow, EffectKind::Glow, EffectKind::InnerShadow}) {
                    for (const double dpi : {1., 1.25, 2.}) {
                        for (const double sigma : {0., 2., 7.}) {
                            const std::array chain{EffectStyle{kind, sigma, {.75, -1.25}, 0x603020a0, true}};
                            useFullEffectSurfaceForTest(true);
                            const auto expected = full.render(*sample, chain, dpi);
                            useFullEffectSurfaceForTest(false);
                            const auto actual = clipped.render(*sample, chain, dpi);
                            equivalent &= actual.offset.x == expected.offset.x &&
                                          actual.offset.y == expected.offset.y &&
                                          actual.image->width() == expected.image->width() &&
                                          actual.image->height() == expected.image->height();
                            const auto& reference = expected.image->pixels();
                            const auto& pixels = actual.image->pixels();
                            for (size_t i = 0; i < pixels.size(); ++i) {
                                maximumDifference =
                                    std::max(maximumDifference, unsigned(std::abs(int(pixels[i]) - int(reference[i]))));
                            }
                        }
                    }
                }
            }
            check("output clipping preserves full-kernel geometry and 8-bit precision",
                  equivalent && maximumDifference <= 2);
            std::cout << "output clip maximum channel difference: " << maximumDifference << '\n';
            // A fresh native graph is the reference for retained-node invalidation.
            // Exercise content, numeric parameters, topology, size and empty chains.
            EffectSurface reused(backend);
            const std::array<std::vector<EffectStyle>, 9> chains{{
                {{EffectKind::Blur, 2, {}}},
                {{EffectKind::Blur, 2, {}}},
                {{EffectKind::Glow, 2, {}, 0xff000080, true}},
                {{EffectKind::Glow, 2, {}, 0x0000ffff, true}},
                {{EffectKind::Shadow, 3, {2, -1}, 0x00ff00c0, false}},
                {{EffectKind::InnerShadow, 3, {-2, 1}, 0x00000080, true}},
                {{EffectKind::Blur, 1, {}}, {EffectKind::Glow, 2, {}, 0xff00ffff, false}},
                {},
                {{EffectKind::Blur, 2, {}}},
            }};
            for (size_t i = 0; i < chains.size(); ++i) {
                EffectSurface fresh(backend);
                const auto& input = i % 2 && i != 3 ? changedSource : source;
                const double dpi = i >= 6 ? 2 : 1;
                const auto expected = fresh.render(*input, chains[i], dpi);
                const auto actual = reused.render(*input, chains[i], dpi);
                check("retained native graph matches fresh rendering after dependency changes",
                      actual.image->pixels() == expected.image->pixels() && actual.offset.x == expected.offset.x &&
                          actual.offset.y == expected.offset.y);
            }
            EffectSurface surface(backend);
            const auto identity = surface.render(*source, {});
            check("empty effect chain preserves pixels", identity.image->pixels() == pixels);
            std::array styles{EffectStyle{EffectKind::Shadow, 2, {0, 0}, 0x000000ff}};
            auto result = surface.render(*source, styles);
            check("effect padding expands with a negative logical origin",
                  result.offset.x == -6 && result.image->width() == 76);
            check("shadow behind source preserves opaque center",
                  channel(result, 32, 32, 3) == 255 && channel(result, 32, 32, 2) == 255);
            check("shadow creates translucent pixels outside original shape",
                  channel(result, 5, 32, 3) > 0 && channel(result, 5, 32, 2) == 0);
            const auto count = surface.stats().renders;
            const auto cached = surface.render(*source, styles);
            check("unchanged content reuses immutable result",
                  cached.image == result.image && surface.stats().renders == count && surface.stats().cacheHits == 1);
            styles[0].includeSource = false;
            const auto shadowOnly = surface.render(*source, styles);
            check("effect-only output excludes original fill", channel(shadowOnly, 32, 32, 2) == 0);
            styles[0].includeSource = true;
            styles[0].kind = EffectKind::Glow;
            styles[0].color = 0xff0000ff;
            const auto glowing = surface.render(*source, styles);
            check("glow uses RGBA color correctly", channel(glowing, 5, 32, 2) > 0 && channel(glowing, 5, 32, 0) == 0);
            styles[0] = {EffectKind::InnerShadow, 3, {0, 0}, 0x000000ff};
            const auto inner = surface.render(*source, styles);
            check("inner shadow darkens inside without growing alpha",
                  channel(inner, 8, 32, 2) < 230 && channel(inner, 32, 32, 2) > 245 && channel(inner, 5, 32, 3) == 0);
            styles[0] = {EffectKind::Blur, 2, {0, 0}};
            const auto blurred = surface.render(*source, styles);
            check("blur smooths alpha edge", channel(blurred, 8, 32, 3) < 240 && channel(blurred, 5, 32, 3) > 0);
            const auto highDpi = surface.render(*source, styles, 2);
            check("sigma and padding follow actual DPI", highDpi.offset.x == -6 && highDpi.image->width() == 88);
            const auto retained = result.image->pixels();
            surface.reset();
            check("reset preserves previously published snapshots", result.image->pixels() == retained);
            surface.render(*source, styles);
            check("reset rebuilds the effect device", surface.stats().deviceRebuilds == 2);
            const auto retainedAfterReset = surface.render(*source, styles);
            const auto rebuilds = surface.stats().deviceRebuilds;
            styles[0].sigma = 3;
            injectEffectDeviceLossForTest();
            check("injected device removal fails with a diagnostic", rejects([&] {
                      surface.render(*source, styles);
                  }));
            check("device failure releases native surfaces", surface.stats().explicitSurfaceBytes == 0);
            check("published CPU pixels survive device removal", !retainedAfterReset.image->pixels().empty());
            const auto recovered = surface.render(*source, styles);
            check("next explicit request rebuilds device after removal",
                  recovered.image && surface.stats().deviceRebuilds == rebuilds + 1);
            bool foreignRejected = false;
            std::thread foreign([&] {
                foreignRejected = rejects([&] {
                    surface.render(*source, styles);
                });
            });
            foreign.join();
            check("effect device cannot cross threads", foreignRejected);
            const auto transparent = ImageData::fromPremultipliedBgra(64, 64, std::vector<uint8_t>(64 * 64 * 4));
            result = surface.render(*transparent, styles);
            check("empty image remains transparent",
                  std::all_of(result.image->pixels().begin(), result.image->pixels().end(), [](uint8_t c) {
                      return c == 0;
                  }));
            check("invalid DPI rejected", rejects([&] {
                      surface.render(*source, styles, 0);
                  }));
        }
        EffectSurface bounded(EffectBackend::Warp, 1024);
        check("surface budget checked before allocation", rejects([&] {
                  bounded.render(*source, {});
              }));
        Canvas canvas;
        ShadowPainter painter;
        std::vector<uint8_t> target(128 * 128 * 4);
        for (int i = 0; i < 2; ++i) {
            canvas.render({128, 128, 512, target.size(), target.data(), 1}, {}, [&](Canvas& draw, Rect) {
                painter.ellipse(draw, {20. + i * 10, 20, 40, 20}, 2, {1, 2}, 0x00000080);
            });
        }
        check("moving ellipse shadow reuses effect pixels", painter.stats().renders == 1);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return passed ? 0 : 1;
}
