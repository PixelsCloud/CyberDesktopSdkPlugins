#include <CyberCanvas/AnimatedImages.h>
#include <CyberCanvas/Native.h>

#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

namespace {
using namespace cybercanvas;
bool passed = true;

void check(const char* name, bool value) {
    passed &= value;
    std::cout << (value ? "PASS " : "FAIL ") << name << '\n';
}

AnimationSnapshot ready(const AnimationHandle& handle) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (;;) {
        auto value = handle.snapshot();
        if (value.state != ImageState::Loading) {
            if (value.state == ImageState::Failed) {
                std::cout << "decoder error: " << value.error << '\n';
            }
            return value;
        }
        if (std::chrono::steady_clock::now() > end) {
            throw std::runtime_error("Animation test timed out");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

void png(const std::filesystem::path& path, uint32_t width, uint32_t height, const std::vector<uint8_t>& pixels) {
    ComPtr<IWICImagingFactory> factory;
    require(
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf())));
    ComPtr<IWICStream> stream;
    require(factory->CreateStream(stream.GetAddressOf()));
    require(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE));
    ComPtr<IWICBitmapEncoder> encoder;
    require(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf()));
    require(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> frame;
    require(encoder->CreateNewFrame(frame.GetAddressOf(), nullptr));
    require(frame->Initialize(nullptr));
    require(frame->SetSize(width, height));
    auto format = GUID_WICPixelFormat32bppBGRA;
    require(frame->SetPixelFormat(&format));
    require(frame->WritePixels(height, width * 4, UINT(pixels.size()), const_cast<BYTE*>(pixels.data())));
    require(frame->Commit());
    require(encoder->Commit());
}

void gif(const std::filesystem::path& path) {
    // Original tiny fixture: four palette colors, one clear code before every
    // pixel so codes always remain three bits. WIC is the decoder under test.
    std::vector<uint8_t> bytes{'G', 'I', 'F', '8', '9', 'a', 4, 0,   2, 0, 0x81, 0,  0,
                               0,   0,   0,   255, 0,   0,   0, 255, 0, 0, 0,    255};
    const auto append = [&](int left, int width, int height, int color, int disposal, int delay) {
        const std::array<uint8_t, 18> prefix{0x21,
                                             0xf9,
                                             4,
                                             uint8_t(disposal * 4 + 1),
                                             uint8_t(delay),
                                             0,
                                             0,
                                             0,
                                             0x2c,
                                             uint8_t(left),
                                             0,
                                             0,
                                             0,
                                             uint8_t(width),
                                             0,
                                             uint8_t(height),
                                             0,
                                             0};
        bytes.insert(bytes.end(), prefix.begin(), prefix.end());
        std::vector<uint8_t> packed;
        unsigned bits = 0, count = 0;
        const auto code = [&](unsigned value) {
            bits |= value << count;
            count += 3;
            while (count >= 8) {
                packed.push_back(uint8_t(bits));
                bits >>= 8;
                count -= 8;
            }
        };
        for (int i = 0; i < width * height; ++i) {
            code(4);
            code(unsigned(color));
        }
        code(5);
        if (count) {
            packed.push_back(uint8_t(bits));
        }
        bytes.push_back(2);
        bytes.push_back(uint8_t(packed.size()));
        bytes.insert(bytes.end(), packed.begin(), packed.end());
        bytes.push_back(0);
    };
    append(0, 4, 2, 1, 1, 2);
    append(0, 2, 2, 2, 2, 3);
    append(2, 2, 2, 3, 3, 4);
    append(0, 1, 1, 2, 1, 5);
    bytes.push_back(0x3b);
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}

uint32_t pixel(const AnimationSnapshot& frame, uint32_t x, uint32_t y) {
    if (!frame.image) {
        return 0;
    }
    const auto& p = frame.image->pixels();
    const size_t i = (size_t(y) * frame.image->width() + x) * 4;
    return uint32_t(p[i + 2]) << 24 | uint32_t(p[i + 1]) << 16 | uint32_t(p[i]) << 8 | p[i + 3];
}

class SlowSource : public AnimationSource {
public:
    explicit SlowSource(AssetContext context) : context_(std::move(context)) {}

    const AnimationInfo& info() const override {
        return info_;
    }

    std::shared_ptr<const ImageData> frame(uint32_t index, uint32_t width, uint32_t height) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        auto lease = context_.budget->reserve(uint64_t(width) * height * 4);
        std::vector<uint8_t> pixels(size_t(width) * height * 4, uint8_t(index * 50));
        return MemoryBudget::attach(ImageData::fromPremultipliedBgra(width, height, std::move(pixels)),
                                    std::move(lease));
    }

private:
    AssetContext context_;
    AnimationInfo info_{4, 4, {.1, .2, .3, .4}};
};
}  // namespace

int main() {
    try {
        require(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        const auto root = std::filesystem::current_path() / "animation-fixtures";
        std::filesystem::create_directories(root);
        gif(root / "disposal.gif");
        png(root / "one.png", 2, 2, {0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255});
        png(root / "two.png", 2, 2, {255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255});
        png(root / "atlas.png", 4, 1, {0, 0, 255, 255, 0, 0, 255, 255, 255, 0, 0, 255, 255, 0, 0, 255});
        {
            std::ofstream truncated(root / "truncated.gif", std::ios::binary);
            truncated << "GIF89a";
            std::ofstream oversized(root / "oversized.gif", std::ios::binary);
            oversized.seekp(8 * 1024 * 1024);
            oversized.put('x');
        }
        auto budget = std::make_shared<MemoryBudget>(16384);
        std::shared_ptr<const ImageData> retained;
        AnimationHandle survives;
        {
            AnimationStore store(budget);
            auto animated = store.request(root, rasterAnimation({.files = {"disposal.gif"}}));
            auto state = ready(animated);
            check("GIF metadata and first frame",
                  state.info && state.info->frameEnds.size() == 4 && pixel(state, 3, 1) == 0xff0000ff);
            animated.seek(.02);
            state = ready(animated);
            check("GIF local frame preserves previous pixels",
                  pixel(state, 0, 0) == 0x00ff00ff && pixel(state, 3, 1) == 0xff0000ff);
            animated.seek(.06);
            state = ready(animated);
            check("GIF disposal background clears only previous rectangle",
                  pixel(state, 0, 1) == 0 && pixel(state, 3, 1) == 0x0000ffff);
            animated.seek(.10);
            state = ready(animated);
            check("GIF disposal previous restores pre-frame canvas",
                  pixel(state, 0, 0) == 0x00ff00ff && pixel(state, 0, 1) == 0 && pixel(state, 3, 1) == 0xff0000ff);
            animated.seek(.025);
            state = ready(animated);
            check("backward seek reconstructs GIF disposal",
                  pixel(state, 0, 1) == 0x00ff00ff && pixel(state, 3, 1) == 0xff0000ff);
            animated.seek(.025, 8, 4);
            check("GIF output dimensions follow requested DPI size", ready(animated).image->width() == 8);
            auto sequence =
                store.request(root, rasterAnimation({RasterAnimationKind::Sequence, {"one.png", "two.png"}, {.1, .2}}));
            ready(sequence);
            sequence.seek(.15);
            state = ready(sequence);
            check("PNG sequence uses per-frame duration", state.frame == 1 && pixel(state, 0, 0) == 0x0000ffff);
            retained = state.image;
            auto atlas = store.request(root, rasterAnimation({RasterAnimationKind::Atlas, {"atlas.png"}, {.1}, 2, 1}));
            ready(atlas);
            atlas.seek(.1);
            state = ready(atlas);
            check("atlas crops independent cells",
                  state.image && state.image->width() == 2 && pixel(state, 0, 0) == 0x0000ffff);
            auto slow = store.request(root, [](const AssetContext& context) {
                return std::make_unique<SlowSource>(context);
            });
            slow.seek(.1);
            slow.seek(.3);
            state = ready(slow);
            check("coalesced requests publish only newest frame", state.frame == 3);
            FramePlayer player(slow, Repeat::Once);
            player.seek(0);
            player.tick(1000);
            player.tick(1100);
            player.visible(false);
            const double position = player.position();
            player.tick(6000);
            player.visible(true);
            player.tick(10000);
            check("hidden resume establishes fresh anchor", player.position() == position);
            player.advance(.25);
            player.advance(.25);
            check("completion is deferred and reported once",
                  player.takeCompleted() && !player.takeCompleted() && !player.active());
            player.seek(0);
            player.reducedMotion(true);
            player.advance(.2);
            check("reduced motion freezes decoration", player.position() == 0 && !player.active());
            auto outside = store.request(root, rasterAnimation({.files = {"../animation-fixtures/../../outside.gif"}}));
            check("outside package path is rejected", ready(outside).state == ImageState::Failed);
            auto missing = store.request(root, rasterAnimation({.files = {"missing.gif"}}));
            check("missing asset has explicit failure", ready(missing).state == ImageState::Failed);
            for (const auto* name : {"truncated.gif", "oversized.gif", "one.png:extra"}) {
                auto invalid = store.request(root, rasterAnimation({.files = {name}}));
                check("truncated oversized and ADS assets are rejected", ready(invalid).state == ImageState::Failed);
            }
            auto absolute =
                store.request(root, rasterAnimation({.files = {std::filesystem::absolute(root / "one.png")}}));
            check("absolute paths are rejected even inside the package", ready(absolute).state == ImageState::Failed);
            auto afterErrors = store.request(root, rasterAnimation({.files = {"disposal.gif"}}));
            check("raster worker recovers after invalid input", ready(afterErrors).state == ImageState::Ready);
            auto cancelled = store.request(root, rasterAnimation({.files = {"disposal.gif"}}));
            cancelled.cancel();
            check("cancelled handle has no callback or image", cancelled.snapshot().state == ImageState::Cancelled);
            survives = sequence;
            check("decoded memory is accounted", budget->used() > 0 && budget->peak() <= budget->maximum());
        }
        check("store teardown cancels surviving handles", survives.snapshot().state == ImageState::Cancelled);
        check("retained snapshots remain accounted after unload",
              retained && budget->used() == retained->pixels().size());
        retained.reset();
        check("final snapshot releases memory lease", budget->used() == 0);
        AnimationStore tiny(std::make_shared<MemoryBudget>(16));
        auto failure = tiny.request(root, rasterAnimation({.files = {"disposal.gif"}}));
        check("GIF working buffers enforce budget before allocation", ready(failure).state == ImageState::Failed);
        CoUninitialize();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return passed ? 0 : 1;
}
