#include <CyberCanvas/Native.h>
#include <box2d/box2d.h>
#include <d2d1_1.h>
#include <d2d1effects.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <thorvg.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <source_location>
#include <vector>

namespace {
using cybercanvas::ComPtr;
using cybercanvas::require;
using Clock = std::chrono::steady_clock;
using Json = nlohmann::json;

void tvgCheck(tvg::Result result, std::source_location location = std::source_location::current()) {
    if (result != tvg::Result::Success) {
        throw std::runtime_error("ThorVG call failed: " + std::to_string(int(result)) + " at " +
                                 std::to_string(location.line()));
    }
}

void save(const std::filesystem::path& file, const std::vector<uint32_t>& pixels, uint32_t size) {
    ComPtr<IWICImagingFactory> factory;
    require(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
    ComPtr<IWICBitmap> source;
    require(factory->CreateBitmapFromMemory(size, size, GUID_WICPixelFormat32bppPBGRA, size * 4,
                                            UINT(pixels.size() * 4), (BYTE*)pixels.data(), &source));
    ComPtr<IWICFormatConverter> converter;
    require(factory->CreateFormatConverter(&converter));
    require(converter->Initialize(source.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                  WICBitmapPaletteTypeCustom));
    ComPtr<IWICStream> stream;
    require(factory->CreateStream(&stream));
    require(stream->InitializeFromFilename(file.c_str(), GENERIC_WRITE));
    ComPtr<IWICBitmapEncoder> encoder;
    require(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder));
    require(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> frame;
    require(encoder->CreateNewFrame(&frame, nullptr));
    require(frame->Initialize(nullptr));
    require(frame->SetSize(size, size));
    auto format = GUID_WICPixelFormat32bppBGRA;
    require(frame->SetPixelFormat(&format));
    require(frame->WriteSource(converter.Get(), nullptr));
    require(frame->Commit());
    require(encoder->Commit());
}

struct Cpu {
    std::unique_ptr<tvg::SwCanvas> canvas{tvg::SwCanvas::gen()};
    tvg::Picture* picture = nullptr;
    tvg::Scene* scene = tvg::Scene::gen();
    std::vector<uint32_t> result;
    uint32_t size;

    explicit Cpu(uint32_t edge) : result(size_t(edge) * edge), size(edge) {
        tvgCheck(canvas->target(result.data(), size, size, size, tvg::ColorSpace::ARGB8888));
        tvgCheck(canvas->add(scene));
    }

    void render(const std::vector<uint32_t>& input, double sigma) {
        if (picture) {
            tvgCheck(scene->remove(picture));
        }
        picture = tvg::Picture::gen();
        tvgCheck(picture->load(input.data(), size, size, tvg::ColorSpace::ARGB8888, true));
        tvgCheck(scene->add(picture));
        tvgCheck(scene->add(tvg::SceneEffect::Clear));
        tvgCheck(scene->add(tvg::SceneEffect::GaussianBlur, sigma, 0, 0, 100));
        tvgCheck(canvas->update());
        tvgCheck(canvas->draw(true));
        tvgCheck(canvas->sync());
    }
};

struct D2d {
    ComPtr<ID3D11Device> d3d;
    ComPtr<ID2D1Factory1> factory;
    ComPtr<ID2D1Device> device;
    ComPtr<ID2D1DeviceContext> context;
    ComPtr<ID2D1Bitmap1> input, target, readback;
    ComPtr<ID2D1Effect> blur;
    std::vector<uint32_t> result;
    uint32_t size;

    D2d(uint32_t edge, D3D_DRIVER_TYPE driver) : result(size_t(edge) * edge), size(edge) {
        require(D3D11CreateDevice(nullptr, driver, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                  D3D11_SDK_VERSION, &d3d, nullptr, nullptr));
        ComPtr<IDXGIDevice> dxgi;
        require(d3d.As(&dxgi));
        require(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf()));
        require(factory->CreateDevice(dxgi.Get(), &device));
        require(device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &context));
        const auto format = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
        auto props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE, format);
        require(context->CreateBitmap(D2D1::SizeU(size, size), nullptr, 0, props, &input));
        props.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET;
        require(context->CreateBitmap(D2D1::SizeU(size, size), nullptr, 0, props, &target));
        props.bitmapOptions = D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
        require(context->CreateBitmap(D2D1::SizeU(size, size), nullptr, 0, props, &readback));
        require(context->CreateEffect(CLSID_D2D1GaussianBlur, &blur));
        blur->SetInput(0, input.Get());
        require(blur->SetValue(D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION, D2D1_GAUSSIANBLUR_OPTIMIZATION_QUALITY));
        context->SetTarget(target.Get());
    }

    void render(const std::vector<uint32_t>& pixels, double sigma) {
        require(input->CopyFromMemory(nullptr, pixels.data(), size * 4));
        require(blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, float(sigma)));
        context->BeginDraw();
        context->Clear(D2D1::ColorF(0, 0));
        context->DrawImage(blur.Get());
        require(context->EndDraw());
        require(readback->CopyFromBitmap(nullptr, target.Get(), nullptr));
        D2D1_MAPPED_RECT map{};
        require(readback->Map(D2D1_MAP_OPTIONS_READ, &map));
        for (uint32_t y = 0; y < size; ++y) {
            std::memcpy(result.data() + size_t(y) * size, map.bits + size_t(y) * map.pitch, size * 4);
        }
        require(readback->Unmap());
    }
};

template <class Renderer>
Json measure(Renderer& renderer, std::vector<uint32_t>& pixels, const std::filesystem::path& output,
             const std::string& name, double seconds, double warmup, unsigned repetitions) {
    Json cases = Json::array();
    for (const auto mode : {"content", "parameters"}) {
        for (unsigned repeat = 0; repeat < repetitions; ++repeat) {
            std::cout << name << ' ' << mode << " repetition " << repeat + 1 << std::endl;
            std::vector<double> samples;
            std::ofstream raw(output / (name + "-" + mode + "-" + std::to_string(repeat + 1) + ".csv"));
            raw << "elapsed_s,duration_ms\n";
            const auto start = Clock::now();
            uint64_t frame = 0;
            do {
                if (std::string_view(mode) == "content") {
                    pixels[pixels.size() / 2 + renderer.size / 2] ^= 0x00000100;
                }
                const auto before = Clock::now();
                renderer.render(pixels, std::string_view(mode) == "parameters" ? 5. + frame++ % 3 : 6.);
                const auto elapsed = std::chrono::duration<double>(before - start).count();
                if (elapsed >= warmup) {
                    const auto duration = std::chrono::duration<double, std::milli>(Clock::now() - before).count();
                    samples.push_back(duration);
                    raw << elapsed << ',' << duration << '\n';
                }
            } while (std::chrono::duration<double>(Clock::now() - start).count() < warmup + seconds ||
                     samples.size() < 30);
            std::sort(samples.begin(), samples.end());
            cases.push_back({{"mode", mode},
                             {"repeat", repeat + 1},
                             {"samples", samples.size()},
                             {"p50_ms", samples[samples.size() / 2]},
                             {"p95_ms", samples[size_t((samples.size() - 1) * .95)]},
                             {"p99_ms", samples[size_t((samples.size() - 1) * .99)]},
                             {"max_ms", samples.back()}});
        }
    }
    renderer.render(pixels, 6.);
    save(output / (name + ".png"), renderer.result, renderer.size);
    bool premultiplied = true;
    for (auto pixel : renderer.result) {
        const auto alpha = pixel >> 24;
        premultiplied &= (pixel & 255) <= alpha && ((pixel >> 8) & 255) <= alpha && ((pixel >> 16) & 255) <= alpha;
    }
    const auto center = renderer.result[size_t(renderer.size / 2) * renderer.size + renderer.size / 2];
    const bool correct = premultiplied && (center >> 24) > 240 && ((center >> 16) & 255) > (center & 255) &&
                         (renderer.result.front() >> 24) == 0;
    return {{"backend", name}, {"passed", correct}, {"cases", cases}, {"center_bgra", center}};
}
}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2 || argc > 6) {
            throw std::invalid_argument(
                "Usage: rendering_probe OUTPUT [seconds-per-case] [warmup] [repetitions] [backend]");
        }
        const auto output = std::filesystem::path(argv[1]);
        std::filesystem::create_directories(output);
        const double seconds = argc > 2 ? std::stod(argv[2]) : 2;
        const double warmup = argc > 3 ? std::stod(argv[3]) : 0;
        const unsigned repetitions = argc > 4 ? std::stoul(argv[4]) : 1;
        const std::string selected = argc > 5 ? argv[5] : "all";
        const std::vector<std::string> selections{"all",     "cpu-256",  "cpu-512", "gpu-256",
                                                  "gpu-512", "warp-256", "warp-512"};
        if (std::find(selections.begin(), selections.end(), selected) == selections.end()) {
            throw std::invalid_argument("Unknown backend selection");
        }
        if (!std::isfinite(seconds) || seconds < .1 || seconds > 600) {
            throw std::invalid_argument("Invalid sample duration");
        }
        if (!std::isfinite(warmup) || warmup < 0 || warmup > 60 || !repetitions || repetitions > 10) {
            throw std::invalid_argument("Invalid warmup/repetitions");
        }
        require(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        tvgCheck(tvg::Initializer::init(0));
        Json report{{"box2d", "3.1.1"}, {"thorvg", "1.0.6"}, {"effects", Json::array()}};
        report["sample_seconds"] = seconds;
        report["warmup_seconds"] = warmup;
        report["repetitions"] = repetitions;
        report["selection"] = selected;
        report["complete"] = false;
        const auto checkpoint = [&] {
            std::ofstream(output / "report.json") << report.dump(2);
        };
        checkpoint();
        auto definition = b2DefaultWorldDef();
        definition.gravity = {0, 10};
        const auto world = b2CreateWorld(&definition);
        auto bodyDefinition = b2DefaultBodyDef();
        bodyDefinition.type = b2_dynamicBody;
        const auto body = b2CreateBody(world, &bodyDefinition);
        auto shapeDefinition = b2DefaultShapeDef();
        b2Circle circle{{0, 0}, .1f};
        b2CreateCircleShape(body, &shapeDefinition, &circle);
        for (int i = 0; i < 60; ++i) {
            b2World_Step(world, 1.f / 60, 4);
        }
        report["fall_y"] = b2Body_GetPosition(body).y;
        report["physics_passed"] = std::abs(b2Body_GetPosition(body).y - 5) < .1;
        b2DestroyWorld(world);
        bool passed = report["physics_passed"].get<bool>();
        for (uint32_t edge : {256, 512}) {
            std::vector<uint32_t> pixels(size_t(edge) * edge);
            for (uint32_t y = edge / 4; y < 3 * edge / 4; ++y) {
                for (uint32_t x = edge / 4; x < 3 * edge / 4; ++x) {
                    pixels[size_t(y) * edge + x] = 0xffff6020;
                }
            }
            const auto cpuName = "cpu-" + std::to_string(edge);
            if (selected == "all" || selected == cpuName) {
                Cpu cpu(edge);
                auto result = measure(cpu, pixels, output, cpuName, seconds, warmup, repetitions);
                passed &= result["passed"].get<bool>();
                report["effects"].push_back(result);
                checkpoint();
            }
            for (const auto driver : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP}) {
                const auto name =
                    std::string(driver == D3D_DRIVER_TYPE_HARDWARE ? "gpu-" : "warp-") + std::to_string(edge);
                if (selected != "all" && selected != name) {
                    continue;
                }
                try {
                    D2d d2d(edge, driver);
                    auto result = measure(d2d, pixels, output, name, seconds, warmup, repetitions);
                    passed &= result["passed"].get<bool>();
                    report["effects"].push_back(result);
                } catch (const std::exception& error) {
                    passed = false;
                    report["effects"].push_back({{"backend", name}, {"error", error.what()}, {"available", false}});
                }
                checkpoint();
            }
        }
        report["passed"] = passed;
        report["complete"] = true;
        checkpoint();
        tvgCheck(tvg::Initializer::term());
        CoUninitialize();
        std::cout << report.dump(2) << '\n';
        return passed ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
