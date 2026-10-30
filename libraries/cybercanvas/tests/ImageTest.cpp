#include <CyberCanvas/Images.h>
#include <CyberCanvas/Scene.h>

#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
#include <thread>

namespace {
void check(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
    std::cout << "PASS " << message << '\n';
}

cybercanvas::ImageSnapshot wait(const cybercanvas::ImageHandle& handle) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (;;) {
        auto result = handle.snapshot();
        if (result.state != cybercanvas::ImageState::Loading) {
            return result;
        }
        if (std::chrono::steady_clock::now() > deadline) {
            throw std::runtime_error("Image worker timed out");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void importedImages(cybercanvas::Canvas& canvas, uint64_t decodedId) {
    using namespace cybercanvas;
    std::vector<uint8_t> pixels(16 * 8 * 4);
    for (size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i + 2] = pixels[i + 3] = 128;
    }
    const auto* original = pixels.data();
    const auto image = ImageData::fromPremultipliedBgra(16, 8, std::move(pixels));
    const auto replacement = ImageData::fromPremultipliedBgra(16, 8, image->pixels());
    check(image->pixels().data() == original, "adopting decoded pixels avoids a second CPU buffer copy");
    check(image->id() != replacement->id() && image->id() != decodedId && replacement->id() != decodedId,
          "imported revisions and worker decodes share collision-free identities");
    const auto rejects = [](auto&& action) {
        try {
            action();
        } catch (const std::invalid_argument&) {
            return true;
        }
        return false;
    };
    check(rejects([] {
              ImageData::fromPremultipliedBgra(0, 8, {});
          }) &&
              rejects([] {
                  ImageData::fromPremultipliedBgra(4097, 1, {});
              }) &&
              rejects([] {
                  ImageData::fromPremultipliedBgra(16, 8, {1, 2, 3, 4});
              }),
          "import rejects empty, oversized and truncated buffers");
    for (double dpi : {1., 1.5, 2.}) {
        const uint32_t edge = uint32_t(40 * dpi);
        std::vector<uint8_t> output(size_t(edge) * edge * 4);
        const FrameBuffer frame{edge, edge, edge * 4, output.size(), output.data(), dpi};
        const auto uploads = canvas.stats().uploads.count;
        canvas.render(frame, {}, [&](auto& draw, auto) {
            draw.image(*image, {0, 0, 40, 40}, {ImageFit::Contain, 0, 1, .5});
        });
        const auto center = (size_t(edge / 2) * edge + edge / 2) * 4;
        check(output[center] == 64 && output[center + 3] == 64 && output[3] == 0,
              "imported premultiplied alpha and per-image opacity survive DPI scaling");
        canvas.render(frame, {}, [&](auto& draw, auto) {
            draw.image(*image, {0, 0, 40, 40}, {ImageFit::Contain});
            draw.image(*image, {0, 0, 40, 40}, {ImageFit::Contain, 0, 1, .25});
        });
        check(canvas.stats().uploads.count == uploads + 1 && canvas.imageUploadBytes() == 16 * 8 * 4,
              "crossfade and repeated frames share one upload, DPI rebuilds exactly once");
        canvas.render(frame, {}, [&](auto& draw, auto) {
            check(rejects([&] {
                      draw.image(*image, {0, 0, 40, 40}, {ImageFit::Cover, 0, 1, -1});
                  }) &&
                      rejects([&] {
                          draw.image(*image, {0, 0, 40, 40}, {ImageFit::Cover, 0, 1, NAN});
                      }),
                  "invalid image opacity is rejected without poisoning the frame");
            auto circle = draw.path();
            circle.moveTo({10, 20}).arcTo({30, 20}, {10, 10}).arcTo({10, 20}, {10, 10}).close();
            draw.masked(circle, [&] {
                draw.image(*replacement, {10, 10, 20, 20}, {ImageFit::Cover, 0, 1, 1, false});
            });
        });
        check(output[center] == 128 && output[center + 3] == 128 && output[3] == 0,
              "external geometry mask clips imported cover without double alpha");
    }
    canvas.clearImageUploads();
}

void indexedImages(cybercanvas::Canvas& canvas, const std::filesystem::path& directory) {
    using namespace cybercanvas;
    const auto path = directory / "indexed.png";
    std::vector<uint8_t> source(8 * 8 * 4);
    for (unsigned y = 0; y < 8; ++y) {
        for (unsigned x = 0; x < 8; ++x) {
            auto* pixel = source.data() + (y * 8 + x) * 4;
            const unsigned alpha = (x % 4) * 85;
            pixel[0] = uint8_t((x * 23 + y * 11) * alpha / 255);
            pixel[1] = uint8_t((255 - x * 23) * alpha / 255);
            pixel[2] = uint8_t(y * 31 * alpha / 255);
            pixel[3] = uint8_t(alpha);
        }
    }
    check(canvas.save(path.wstring(), {8, 8, 32, source.size(), source.data(), 1}), "create indexed style fixture");
    ImageStore store;
    const std::vector<uint32_t> palette{0, 0xff102029, 0xfff5e7cd, 0xff9bcdb4, 0xffc77d60};
    const auto image = wait(store.request(path, 0, 8, {}, palette));
    const auto reused = wait(store.request(path, 0, 8, {}, palette));
    const auto original = wait(store.request(path, 0, 8));
    check(image.state == ImageState::Ready && image.image == reused.image && original.state == ImageState::Ready &&
              image.image != original.image && image.image->pixels() != original.image->pixels(),
          "indexed and smooth resources retain independent cache identities without mutating source pixels");
    std::set<uint32_t> allowed(palette.begin(), palette.end());
    const auto checkPixels = [&](const std::vector<uint8_t>& bytes, bool rgba = false) {
        bool transparent = false, opaque = false, matching = true;
        for (size_t offset = 0; offset < bytes.size(); offset += 4) {
            const uint32_t color = uint32_t(bytes[offset + (rgba ? 2 : 0)]) | uint32_t(bytes[offset + 1]) << 8 |
                                   uint32_t(bytes[offset + (rgba ? 0 : 2)]) << 16 | uint32_t(bytes[offset + 3]) << 24;
            matching &= allowed.contains(color);
            transparent |= bytes[offset + 3] == 0;
            opaque |= bytes[offset + 3] == 255;
        }
        check(matching, "indexed pixels contain only explicit palette entries without interpolation colors");
        check(transparent && opaque, "indexed alpha threshold retains transparent and opaque silhouettes");
    };
    checkPixels(image.image->pixels());
    std::vector<uint8_t> output(32 * 32 * 4);
    canvas.render({32, 32, 128, output.size(), output.data(), 2}, {}, [&](auto& draw, auto) {
        draw.image(*image.image, {0, 0, 16, 16},
                   {.fit = ImageFit::Stretch, .clip = false, .sampling = ImageSampling::Nearest});
    });
    checkPixels(output, true);
    check(store.stats().decodedBytes == 8 * 8 * 4 * 2 && store.stats().failures == 0,
          "indexed decoding remains bounded and never materializes a full-resolution extra image");
    std::filesystem::remove(path);
}
}  // namespace

int main() {
    using namespace cybercanvas;
    try {
        Canvas canvas;
        const auto directory =
            std::filesystem::temp_directory_path() / (L"cybercanvas-images-" + std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directories(directory);
        indexedImages(canvas, directory);
        const auto path = directory / L"透明插画.png";
        std::vector<uint8_t> source(64 * 32 * 4);
        for (size_t y = 0; y < 32; ++y) {
            for (size_t x = 0; x < 64; ++x) {
                auto* pixel = source.data() + (y * 64 + x) * 4;
                pixel[x < 32 ? 0 : 2] = x < 32 ? 128 : 255;
                pixel[3] = x < 32 ? 128 : 255;
            }
        }
        const FrameBuffer fixture{64, 32, 256, source.size(), source.data(), 1};
        check(canvas.save(path.wstring(), fixture), "create transparent Unicode fixture");
        ImageStore store;
        auto first = store.request(path);
        auto second = store.request(directory / L"." / path.filename());
        auto decoded = wait(first);
        const auto duplicate = wait(second);
        check(decoded.state == ImageState::Ready && decoded.image == duplicate.image && store.stats().decodes == 1,
              "normalized requests share one decode and immutable pixels");
        check(decoded.image->width() == 64 && decoded.image->height() == 32 && decoded.image->pixels()[2] == 128 &&
                  decoded.image->pixels()[3] == 128,
              "WIC preserves premultiplied transparency and BGRA order");
        importedImages(canvas, decoded.image->id());
        first.cancel();
        check(first.snapshot().state == ImageState::Cancelled && second.snapshot().state == ImageState::Ready,
              "cancelling one consumer preserves other consumers");
        auto smaller = store.request(path, 0, 16);
        const auto thumbnail = wait(smaller);
        check(thumbnail.state == ImageState::Ready && thumbnail.image->width() == 16 && thumbnail.image->height() == 8,
              "maximum edge bounds decoded pixels while preserving aspect ratio");
        auto left = store.request(path, 0, 32, {0, 0, 32, 32});
        auto right = store.request(path, 0, 32, {32, 0, 32, 32});
        const auto leftImage = wait(left), rightImage = wait(right);
        check(leftImage.state == ImageState::Ready && rightImage.state == ImageState::Ready &&
                  leftImage.image != rightImage.image && leftImage.image->width() == 32 &&
                  leftImage.image->height() == 32 && leftImage.image->pixels()[3] == 128 &&
                  rightImage.image->pixels()[3] == 255 && rightImage.image->pixels()[0] == 255,
              "atlas regions preserve source coordinates and alpha without aliasing cached pixels");
        auto invalidRegion = store.request(path, 0, 32, {50, 0, 32, 32});
        check(wait(invalidRegion).state == ImageState::Failed, "out-of-source atlas regions fail asynchronously");
        ImageStore atlasBudget({4096, 64 * 1024 * 1024, 8});
        auto cropped = atlasBudget.request(path, 0, 16, {32, 0, 32, 32});
        const auto croppedImage = wait(cropped);
        check(croppedImage.state == ImageState::Ready && croppedImage.image->width() == 16 &&
                  croppedImage.image->height() == 16 && atlasBudget.stats().decodedBytes == 1024,
              "only the visible atlas region consumes decoded-memory budget");
        auto replacement = store.request(path, 1);
        check(wait(replacement).image->id() != decoded.image->id(),
              "resource revisions keep old and new identities separate");
        auto missing = store.request(directory / L"missing.png");
        check(wait(missing).state == ImageState::Failed,
              "missing files become failed resources rather than render exceptions");
        const auto corrupt = directory / L"broken.png";
        std::ofstream(corrupt, std::ios::binary) << "invalid image";
        auto broken = store.request(corrupt);
        check(wait(broken).state == ImageState::Failed, "corrupt codecs report a failed resource");
        ImageStore limited({8192, 64 * 1024 * 1024, 4});
        auto pinned = limited.request(path);
        auto external = wait(pinned);
        check(external.state == ImageState::Ready && limited.stats().decodedBytes == 8192,
              "decoded memory includes pixels held by consumers");
        pinned.cancel();
        auto refused = limited.request(path, 2);
        check(wait(refused).state == ImageState::Failed && limited.stats().peakDecodedBytes <= 8192,
              "external snapshots pin the hard decoded-memory budget");
        external = {};
        refused.cancel();
        auto retried = limited.request(path, 2);
        check(wait(retried).state == ImageState::Ready,
              "failed requests can recover with the same revision once consumers release memory");
        retried.cancel();
        limited.trim();
        auto recovered = limited.request(path, 3);
        check(wait(recovered).state == ImageState::Ready && limited.stats().decodedBytes == 8192,
              "released resources reclaim capacity and later requests recover");
        ImageStore entries({64 * 1024 * 1024, 64 * 1024 * 1024, 1});
        auto occupying = entries.request(path);
        wait(occupying);
        auto full = entries.request(path, 1);
        check(full.snapshot().state == ImageState::Failed && entries.stats().entries == 1,
              "active requests cannot exceed the entry budget");
        occupying.cancel();
        entries.trim();
        auto next = entries.request(path, 2);
        check(wait(next).state == ImageState::Ready, "eviction permits new requests after cancellation");
        const auto retained = wait(next);
        next.cancel();
        auto newKey = entries.request(path, 3);
        check(wait(newKey).state == ImageState::Ready && retained.image->pixels()[3] == 128 &&
                  entries.stats().entries == 1,
              "retained transition pixels do not pin obsolete cache keys or block new DPI requests");
        bool replacementsReady = true;
        for (uint64_t revision = 10; revision < 80; ++revision) {
            auto abandoned = store.request(path, revision);
            abandoned.cancel();
            auto renewed = store.request(path, revision);
            replacementsReady &= wait(renewed).state == ImageState::Ready;
        }
        check(replacementsReady, "immediate cancellation and re-request never inherit a cancelled job");

        // Deleting the source proves drawing only consumes already decoded pixels.
        std::filesystem::remove(path);
        std::vector<uint8_t> output(100 * 100 * 4);
        FrameBuffer frame{100, 100, 400, output.size(), output.data(), 1};
        const auto draw = [&](ImagePlacement placement) {
            canvas.render(frame, {}, [&](auto&, auto) {
                canvas.image(*decoded.image, {0, 0, 100, 100}, placement);
            });
        };
        draw({ImageFit::Contain});
        check(output[3] == 0 && output[(50 * 100 + 10) * 4 + 3] == 128,
              "contain letterboxes without reading the source file");
        draw({ImageFit::Cover});
        check(output[(10 * 100 + 10) * 4 + 3] > 0, "cover fills the destination and clips overflow");
        draw({ImageFit::Stretch, 1, 2});
        check(output[(50 * 100 + 50) * 4 + 2] == 255 && output[(50 * 100 + 50) * 4] == 0,
              "sprite frames and stretch use the selected source cell");
        check(canvas.imageUploadBytes() == 8192, "repeated placement reuses one uploaded bitmap");
        for (double dpr : {1., 1.25, 1.5, 2., 3.}) {
            output.resize(size_t(100 * dpr) * size_t(100 * dpr) * 4);
            frame = {uint32_t(100 * dpr), uint32_t(100 * dpr), uint32_t(400 * dpr), output.size(), output.data(), dpr};
            canvas.render(frame, {}, [&](auto&, auto) {
                canvas.image(*decoded.image, {0, 0, 100, 100});
            });
            check(canvas.imageUploadBytes() == 8192 && output[3] == 0,
                  "DPI rebuilds upload from decoded memory without redecoding");
        }
        bool cachedAfterDeletion = true;
        canvas.render(frame, {}, [&](auto&, auto) {
            for (uint64_t revision = 10; revision < 80; ++revision) {
                auto cached = store.request(path, revision);
                auto resource = wait(cached);
                cachedAfterDeletion &= resource.state == ImageState::Ready;
                if (resource.image) {
                    canvas.image(*resource.image, {0, 0, 100, 100});
                }
            }
        });
        check(cachedAfterDeletion, "cached revisions survive deletion of the source file");
        check(canvas.imageUploadBytes() == 64 * 8192, "uploaded bitmap LRU stays bounded across seventy resources");
        Scene scene;
        View image;
        image.kind = ViewKind::Image;
        image.imageCells = 2;
        const auto node = scene.add(0, image);
        scene.imageResource(node, decoded);
        scene.arrange(canvas, {0, 0, 100, 100}, 3);
        const auto paint = [&] {
            canvas.render(frame, {}, [&](auto&, auto) {
                scene.draw(canvas);
            });
        };
        paint();
        scene.imageResource(node, decoded);
        check(!scene.needsPaint(), "identical scene resource snapshots remain idle");
        const auto layouts = scene.stats().layouts;
        scene.imageResource(node, {ImageState::Loading});
        paint();
        scene.imageResource(node, {ImageState::Failed, {}, "missing"});
        paint();
        check(!scene.needsLayout() && scene.stats().layouts == layouts,
              "loading and failed placeholders only request painting");
        scene.remove(node);
        canvas.clearImageUploads();
        check(canvas.imageUploadBytes() == 0, "uploaded resources can be explicitly released");
        ImageHandle survivor;
        {
            ImageStore temporary;
            survivor = temporary.request(corrupt);
        }
        check(survivor.snapshot().state != ImageState::Loading,
              "store shutdown joins the worker and settles surviving handles");
        std::filesystem::remove(corrupt);
        std::filesystem::remove(directory);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
