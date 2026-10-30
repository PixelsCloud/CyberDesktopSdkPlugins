#include <CyberCanvas/Canvas.h>
#include <CyberCanvas/Vector.h>
#include <wincrypt.h>

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

void write(const std::filesystem::path& path, const std::string& value) {
    std::ofstream file(path, std::ios::binary);
    file << value;
}

AnimationSnapshot ready(const AnimationHandle& handle) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (;;) {
        auto state = handle.snapshot();
        if (state.state != ImageState::Loading) {
            if (!state.error.empty()) {
                std::cout << state.error << '\n';
            }
            return state;
        }
        if (std::chrono::steady_clock::now() > end) {
            throw std::runtime_error("Vector test timed out");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

uint32_t pixel(const AnimationSnapshot& frame, uint32_t x, uint32_t y) {
    if (!frame.image) {
        return 0;
    }
    const auto& p = frame.image->pixels();
    const size_t i = (size_t(y) * frame.image->width() + x) * 4;
    return uint32_t(p[i + 2]) << 24 | uint32_t(p[i + 1]) << 16 | uint32_t(p[i]) << 8 | p[i + 3];
}
}  // namespace

int main() {
    try {
        const auto root = std::filesystem::current_path() / "vector-fixtures";
        std::filesystem::create_directories(root);
        write(
            root / "shape.svg",
            R"(<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64" viewBox="0 0 64 64"><rect x="8" y="8" width="48" height="48" fill="#ff2200"/></svg>)");
        write(
            root / "gradient.svg",
            R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64"><defs><linearGradient id="g"><stop offset="0" stop-color="#ff0000"/><stop offset="1" stop-color="#0000ff"/></linearGradient><clipPath id="c"><circle cx="32" cy="32" r="24"/></clipPath></defs><rect width="64" height="64" fill="url(#g)" clip-path="url(#c)"/></svg>)svg");
        const std::string lottie =
            R"({"v":"5.7.4","fr":10,"ip":0,"op":10,"w":64,"h":64,"nm":"original test","ddd":0,"assets":[],"layers":[{"ddd":0,"ind":1,"ty":4,"nm":"moving square","sr":1,"ks":{"o":{"a":0,"k":100},"r":{"a":0,"k":0},"p":{"a":1,"k":[{"t":0,"s":[12,32,0],"e":[52,32,0],"i":{"x":1,"y":1},"o":{"x":0,"y":0}},{"t":10,"s":[52,32,0]}]},"a":{"a":0,"k":[0,0,0]},"s":{"a":0,"k":[100,100,100]}},"shapes":[{"ty":"rc","d":1,"s":{"a":0,"k":[16,16]},"p":{"a":0,"k":[0,0]},"r":{"a":0,"k":0}},{"ty":"fl","c":{"a":0,"k":[0,1,0,1]},"o":{"a":0,"k":100},"r":1}],"ip":0,"op":10,"st":0,"bm":0}]})";
        write(root / "move.json", lottie);
        Canvas fixtureCanvas;
        std::vector<uint8_t> fixturePixels(32 * 32 * 4);
        const FrameBuffer fixtureFrame{32, 32, 128, fixturePixels.size(), fixturePixels.data(), 1};
        fixtureCanvas.render(fixtureFrame, {}, [](Canvas& canvas, Rect area) {
            canvas.rectangle(area, 0xff0000ff);
        });
        check("original PNG fixture saved", fixtureCanvas.save((root / "red.png").wstring(), fixtureFrame));
        const std::string imageLottie =
            R"({"v":"5.7.4","fr":10,"ip":0,"op":10,"w":64,"h":64,"ddd":0,"assets":[{"id":"red","w":32,"h":32,"u":"","p":"red.png","e":0}],"layers":[{"ddd":0,"ind":1,"ty":2,"refId":"red","sr":1,"ks":{"o":{"a":0,"k":100},"r":{"a":0,"k":0},"p":{"a":0,"k":[32,32,0]},"a":{"a":0,"k":[16,16,0]},"s":{"a":0,"k":[100,100,100]}},"ip":0,"op":10,"st":0,"bm":0}]})";
        write(root / "image.json", imageLottie);
        std::ifstream pngFile(root / "red.png", std::ios::binary);
        const std::vector<uint8_t> pngBytes{std::istreambuf_iterator<char>(pngFile), {}};
        DWORD encodedSize = 0;
        require(CryptBinaryToStringA(pngBytes.data(), DWORD(pngBytes.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                                     nullptr, &encodedSize)
                    ? S_OK
                    : E_FAIL);
        std::string encoded(encodedSize, '\0');
        require(CryptBinaryToStringA(pngBytes.data(), DWORD(pngBytes.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                                     encoded.data(), &encodedSize)
                    ? S_OK
                    : E_FAIL);
        encoded.resize(std::strlen(encoded.c_str()));
        auto embedded = imageLottie;
        embedded.replace(embedded.find("red.png"), 7, "data:image/png;base64," + encoded);
        write(root / "embedded.json", embedded);
        auto external = imageLottie;
        external.replace(external.find("red.png"), 7, "../red.png");
        write(root / "escaped.json", external);
        write(
            root / "masked.json",
            R"({"v":"5.7.4","fr":10,"ip":0,"op":10,"w":64,"h":64,"ddd":0,"assets":[],"layers":[{"ddd":0,"ind":1,"ty":4,"sr":1,"ks":{"o":{"a":0,"k":100},"r":{"a":0,"k":0},"p":{"a":0,"k":[0,0,0]},"a":{"a":0,"k":[0,0,0]},"s":{"a":0,"k":[100,100,100]}},"masksProperties":[{"inv":false,"mode":"a","pt":{"a":0,"k":{"i":[[0,0],[0,0],[0,0],[0,0]],"o":[[0,0],[0,0],[0,0],[0,0]],"v":[[8,8],[56,8],[56,40],[8,40]],"c":true}},"o":{"a":0,"k":100},"x":{"a":0,"k":0}}],"shapes":[{"ty":"rc","d":1,"s":{"a":0,"k":[64,64]},"p":{"a":0,"k":[32,32]},"r":{"a":0,"k":0}},{"ty":"gf","o":{"a":0,"k":100},"r":1,"t":1,"s":{"a":0,"k":[0,32]},"e":{"a":0,"k":[64,32]},"g":{"p":2,"k":{"a":0,"k":[0,1,0,0,1,0,0,1]}}}],"ip":0,"op":10,"st":0,"bm":0}]})");
        write(
            root / "external.svg",
            R"(<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64"><image href="https://example.invalid/image.png" width="64" height="64"/></svg>)");
        write(
            root / "entity.svg",
            R"(<!DOCTYPE svg [<!ENTITY ext SYSTEM "file:///c:/windows/win.ini">]><svg width="64" height="64">&ext;</svg>)");
        write(root / "script.svg", R"(<svg width="64" height="64"><script>alert(1)</script></svg>)");
        auto budget = std::make_shared<MemoryBudget>(1024 * 1024);
        std::shared_ptr<const ImageData> retained;
        {
            AnimationStore store(budget);
            auto svg = store.request(root, vectorAnimation("shape.svg", VectorKind::Svg));
            auto state = ready(svg);
            check("SVG colors are premultiplied BGRA", pixel(state, 32, 32) == 0xff2200ff && pixel(state, 0, 0) == 0);
            svg.seek(0, 128, 64);
            state = ready(svg);
            check("SVG aspect fit is centered", pixel(state, 64, 32) == 0xff2200ff && pixel(state, 20, 32) == 0);
            auto gradient = store.request(root, vectorAnimation("gradient.svg", VectorKind::Svg));
            state = ready(gradient);
            check("SVG gradient and clip compose", (pixel(state, 12, 32) >> 24) > 150 &&
                                                       ((pixel(state, 52, 32) >> 8) & 255) > 150 &&
                                                       pixel(state, 2, 2) == 0);
            auto moving = store.request(root, vectorAnimation("move.json", VectorKind::Lottie));
            state = ready(moving);
            check("Lottie first frame and duration", state.info && state.info->frameEnds.size() == 10 &&
                                                         state.info->duration() == 1 &&
                                                         pixel(state, 12, 32) == 0x00ff00ff);
            moving.seek(.8);
            state = ready(moving);
            check("Lottie transform animation advances",
                  state.frame == 8 && pixel(state, 44, 32) == 0x00ff00ff && pixel(state, 12, 32) == 0);
            moving.seek(.2, 128, 128);
            state = ready(moving);
            check("Lottie seek and DPI rerender are independent",
                  state.image && state.image->width() == 128 && pixel(state, 40, 64) == 0x00ff00ff);
            retained = state.image;
            for (const auto* file : {"image.json", "embedded.json"}) {
                auto image = store.request(root, vectorAnimation(file, VectorKind::Lottie));
                const auto decoded = ready(image);
                check("Lottie package and embedded PNG resolve through WIC", decoded.state == ImageState::Ready &&
                                                                                 pixel(decoded, 32, 32) == 0xff0000ff &&
                                                                                 pixel(decoded, 4, 4) == 0);
            }
            auto escaped = store.request(root, vectorAnimation("escaped.json", VectorKind::Lottie));
            check("Lottie image cannot escape package", ready(escaped).state == ImageState::Failed);
            auto masked = store.request(root, vectorAnimation("masked.json", VectorKind::Lottie));
            const auto mask = ready(masked);
            check("Lottie gradient and mask have expected coverage",
                  mask.state == ImageState::Ready && (pixel(mask, 16, 20) >> 24) > 150 &&
                      ((pixel(mask, 48, 20) >> 8) & 255) > 150 && pixel(mask, 32, 50) == 0);
            for (const auto file : {"external.svg", "entity.svg", "script.svg"}) {
                auto rejected = store.request(root, vectorAnimation(file, VectorKind::Svg));
                check("unsupported/external SVG fails explicitly", ready(rejected).state == ImageState::Failed);
            }
            auto malformed = store.request(root, vectorAnimation("shape.svg", VectorKind::Lottie));
            check("wrong format fails without crashing worker", ready(malformed).state == ImageState::Failed);
            auto again = store.request(root, vectorAnimation("shape.svg", VectorKind::Svg));
            check("worker recovers after decoder error", ready(again).state == ImageState::Ready);
        }
        check("vector unload retains only published snapshot bytes",
              retained && budget->used() == retained->pixels().size());
        retained.reset();
        check("vector resources release all explicit leases", budget->used() == 0);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return passed ? 0 : 1;
}
