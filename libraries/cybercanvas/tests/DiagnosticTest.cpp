#include <CyberCanvas/Images.h>
#include <CyberCanvas/Scene.h>

#include <filesystem>
#include <iostream>
#include <thread>

int main() {
    using namespace cybercanvas;
    int failures = 0;
    const auto check = [&](const char* name, bool passed) {
        std::cout << (passed ? "PASS " : "FAIL ") << name << '\n';
        failures += !passed;
    };
    try {
        Canvas canvas, other;
        Scene scene;
        View label;
        label.kind = ViewKind::Text;
        label.text = L"Measured text";
        scene.add(0, label);
        View slider;
        slider.kind = ViewKind::Slider;
        slider.key = 7;
        const auto node = scene.add(0, slider);
        std::vector<uint8_t> pixels(160 * 100 * 4);
        FrameBuffer frame{160, 100, 640, pixels.size(), pixels.data(), 1};
        scene.arrange(canvas, {0, 0, 160, 100});
        canvas.render(frame, {}, [&](auto&, auto) {
            scene.draw(canvas);
        });
        const auto baseline = canvas.stats();
        const auto layout = scene.stats();
        check("frame counters include conversion and physical surface payload",
              baseline.frames.count == 1 && baseline.frames.nanoseconds > 0 && baseline.pixelCopies.count == 1 &&
                  baseline.pixelCopies.nanoseconds > 0 && baseline.surfaceBytes == 160 * 100 * 4 &&
                  baseline.surfaceRebuilds == 1);
        check("layout and measurement durations are attributable to a scene",
              layout.layouts == 1 && layout.layoutNanoseconds > 0 && layout.measurements > 0 &&
                  layout.measureNanoseconds > 0);
        check("another Canvas does not inherit instance counters",
              other.stats().frames.count == 0 && other.stats().trackedBytes == 0);
        scene.value(node, .5);
        scene.arrange(canvas, {0, 0, 160, 100});
        canvas.render(frame, {}, [&](auto&, auto) {
            scene.draw(canvas);
        });
        check("paint-only update reuses text and layout",
              canvas.stats().textLayouts.count == baseline.textLayouts.count &&
                  canvas.stats().textCacheHits > baseline.textCacheHits && scene.stats().layouts == layout.layouts &&
                  scene.stats().measurements == layout.measurements);
        const auto before = canvas.stats();
        canvas.stats();
        scene.stats();
        check("diagnostic reads do not request new work",
              canvas.stats().frames.count == before.frames.count && !scene.needsPaint());
        std::vector<uint8_t> icon(8 * 8 * 4, 100);
        FrameBuffer source{8, 8, 32, icon.size(), icon.data(), 1};
        for (int i = 0; i < 2; ++i) {
            canvas.render(frame, {}, [&](auto&, auto) {
                canvas.pixels(L"icon", source, {4, 4, 16, 16});
            });
        }
        check("bitmap uploads and cache hits are distinct", canvas.stats().uploads.count == 1 &&
                                                                canvas.stats().pixelBytes == 256 &&
                                                                canvas.stats().bitmapCacheHits >= 1);
        const auto path = std::filesystem::temp_directory_path() /
                          (L"cybercanvas-diagnostics-" + std::to_wstring(GetCurrentProcessId()) + L".png");
        if (!canvas.save(path.wstring(), frame)) {
            return 1;
        }
        ImageStore images;
        auto first = images.request(path);
        auto shared = images.request(path);
        const auto wait = [](const ImageHandle& handle) {
            for (int i = 0; i < 1500 && handle.snapshot().state == ImageState::Loading; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        };
        wait(shared);
        const auto request = shared.stats();
        check("shared decode time is reported without an extra decode",
              request.state == ImageState::Ready && request.decodeNanoseconds > 0 &&
                  request.payloadBytes == pixels.size() && request.reused && request.sharingRequests == 2 &&
                  images.stats().decodes == 1 && images.stats().decodeNanoseconds == request.decodeNanoseconds);
        check("asynchronous snapshots retain immutable source metadata",
              shared.snapshot().source && *shared.snapshot().source == path.wstring());
        for (int i = 0; i < 2; ++i) {
            canvas.render(frame, {}, [&](auto&, auto) {
                canvas.image(*shared.snapshot().image, {0, 0, 160, 100});
            });
        }
        check("resource payload and upload are accounted separately from source pixels",
              canvas.stats().resourceBytes == pixels.size() && canvas.stats().uploads.count == 2 &&
                  canvas.stats().uploadedBytes == pixels.size() + 256);
        const auto peak = canvas.stats().peakTrackedBytes;
        canvas.clearImageUploads();
        check("eviction releases tracked resident payload and retains its peak",
              canvas.stats().resourceBytes == 0 && canvas.stats().peakTrackedBytes == peak &&
                  canvas.stats().trackedBytes < peak);
        first.cancel();
        check("released request does not retain a resource charge",
              first.stats().state == ImageState::Cancelled && first.stats().payloadBytes == 0);
        auto missing = images.request(path.wstring() + L".missing");
        wait(missing);
        check("failed decode timing and failures remain visible", missing.stats().state == ImageState::Failed &&
                                                                      missing.stats().decodeNanoseconds > 0 &&
                                                                      images.stats().failures == 1);
        std::filesystem::remove(path);
        return failures ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
