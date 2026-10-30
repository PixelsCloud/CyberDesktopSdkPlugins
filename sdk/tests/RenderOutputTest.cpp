#include <CyberCanvas/GpuInterop.h>
#include <CyberCanvas/Sprites.h>
#include <d3d11_1.h>
#include <sdk/Canvas.h>
#include <sdk/Images.h>
#include <sdk/RenderOutput.h>
#include <sdk/services/CanvasGpu.h>
#include <sdk/services/RenderImages.h>
#include <sdk/services/RenderMemory.h>

#include <array>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <thread>

namespace {
void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct Host {
    CdServiceHostApi api{};
    CdFrame* main = nullptr;
    void* target = nullptr;
    uint64_t epoch = 1, painted = 0, unchanged = 0, copied = 0;
    cybercanvas::MemoryBudget budget{1024 * 1024};
    bool legacy = false;

    explicit Host(uint64_t bytes = 1024 * 1024) : budget(bytes) {
        api.input.media.base.struct_size = sizeof(api);
        api.input.media.base.abi_version = CD_PLUGIN_ABI_VERSION;
        api.query_service = query;
    }

    CdCreateContext create() {
        return {sizeof(CdCreateContext), CD_PLUGIN_ABI_VERSION, "test", "test", "", &api.input.media.base, this};
    }

    static CdResult CD_CALL query(void* context, const char* id, uint32_t version, void* output, uint32_t capacity) {
        if (version != 1) {
            return CD_ERROR_VERSION;
        }
        if (!std::strcmp(id, CD_SERVICE_RENDER_MEMORY)) {
            if (capacity < sizeof(CdRenderMemoryApi)) {
                return CD_ERROR_BUFFER;
            }
            *static_cast<CdRenderMemoryApi*>(output) = {
                sizeof(CdRenderMemoryApi), 1,
                [](void* context, uint64_t bytes) noexcept -> void* {
                    try {
                        return cybercanvas::makeMemoryLease(static_cast<Host*>(context)->budget.reserve(bytes))
                            .Detach();
                    } catch (...) {
                        return nullptr;
                    }
                },
                context};
            return CD_OK;
        }
        if (!std::strcmp(id, CD_SERVICE_CANVAS_GPU)) {
            if (capacity < sizeof(CdCanvasGpuApi)) {
                return CD_ERROR_BUFFER;
            }
            *static_cast<CdCanvasGpuApi*>(output) = {sizeof(CdCanvasGpuApi), 1,
                                                     [](void*, uint32_t enabled) -> CdResult {
                                                         return enabled == 1 ? CD_OK : CD_ERROR_ARGUMENT;
                                                     },
                                                     [](void* context) -> void* {
                                                         return static_cast<Host*>(context)->target;
                                                     },
                                                     [](void* context) -> uint32_t {
                                                         return static_cast<Host*>(context)->target != nullptr;
                                                     }};
            return CD_OK;
        }
        if (std::strcmp(id, CD_SERVICE_RENDER_OUTPUT) || capacity < sizeof(CdRenderOutputApi)) {
            return CD_ERROR_VERSION;
        }
        *static_cast<CdRenderOutputApi*>(output) = {
            sizeof(CdRenderOutputApi), 1,
            [](void* context) -> CdResult {
                return static_cast<Host*>(context)->legacy ? CD_ERROR_VERSION : CD_OK;
            },
            [](void* context, const CdFrame* frame, CdRenderSession* output) -> CdResult {
                auto& host = *static_cast<Host*>(context);
                *output = {sizeof(CdRenderSession), 1};
                if (host.main && frame->rgba == host.main->rgba && frame->width == host.main->width &&
                    frame->height == host.main->height) {
                    output->backend = host.target ? CD_OUTPUT_GPU : CD_OUTPUT_CPU;
                    output->target = host.target;
                    output->host_draw_scope = host.target != nullptr;
                    output->output_epoch = host.epoch * 2 + (host.target ? 1 : 0);
                }
                return CD_OK;
            },
            [](void* context, const CdRenderReport* report) -> CdResult {
                auto& host = *static_cast<Host*>(context);
                if (!report->output_epoch) {
                    return CD_ERROR_ARGUMENT;
                }
                host.painted += report->result == CD_OUTPUT_PAINTED;
                host.unchanged += report->result == CD_OUTPUT_UNCHANGED;
                host.copied += report->pixel_copy_bytes;
                return CD_OK;
            }};
        return CD_OK;
    }
};

std::vector<uint8_t> capture(ID3D11Device* device, ID3D11DeviceContext* immediate, ID3D11ShaderResourceView* view) {
    cybercanvas::ComPtr<ID3D11Resource> resource;
    cybercanvas::ComPtr<ID3D11Texture2D> texture, staging;
    view->GetResource(&resource);
    cybercanvas::require(resource.As(&texture));
    D3D11_TEXTURE2D_DESC description;
    texture->GetDesc(&description);
    description.Usage = D3D11_USAGE_STAGING;
    description.BindFlags = 0;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    cybercanvas::require(device->CreateTexture2D(&description, nullptr, &staging));
    immediate->CopyResource(staging.Get(), texture.Get());
    D3D11_MAPPED_SUBRESOURCE map{};
    cybercanvas::require(immediate->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map));
    std::vector<uint8_t> result(size_t(description.Width) * description.Height * 4);
    for (unsigned y = 0; y < description.Height; ++y) {
        std::memcpy(result.data() + y * description.Width * 4,
                    static_cast<const uint8_t*>(map.pData) + y * map.RowPitch, description.Width * 4);
    }
    immediate->Unmap(staging.Get(), 0);
    return result;
}

void run(D3D_DRIVER_TYPE driver) {
    cybercanvas::ComPtr<ID3D11Device> device;
    cybercanvas::ComPtr<ID3D11DeviceContext> immediate;
    cybercanvas::require(D3D11CreateDevice(nullptr, driver, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                           D3D11_SDK_VERSION, &device, nullptr, &immediate));
    Host host;
    cdsdk::Canvas canvas, nested;
    canvas.bindGpu(host.create());
    nested.bindGpu(host.create());
    cdsdk::FrameCache cache;
    cache.bind(host.create());
    std::vector<uint8_t> bytes(64 * 64 * 4, 0xcd), childBytes(8 * 8 * 4);
    CdFrame frame{sizeof(CdFrame), 64, 64, 256, bytes.size(), bytes.data(), 1};
    CdFrame child{sizeof(CdFrame), 8, 8, 32, childBytes.size(), childBytes.data(), 1};
    host.main = &frame;
    cybercanvas::GpuSurface surface;
    unsigned calls = 0;
    auto paint = [&](const CdFrame& output) -> CdResult {
        ++calls;
        canvas.render(output, {}, [&](auto& drawing, auto) {
            drawing.box(0, 0, 64, 64, 0, 0x2266aaff);
            nested.render(child, {}, [](auto& drawing, auto) {
                drawing.box(0, 0, 8, 8, 0, 0xff0000ff);
            });
            nested.drawIn(drawing, {10, 10, 8, 8}, [](auto& drawing) {
                drawing.box(0, 0, 8, 8, 0, 0x33dd55ff);
            });
        });
        return CD_OK;
    };
    auto gpu = [&](uint64_t content) {
        auto* view = surface.render(device.Get(), immediate.Get(), 64, 64, 1, [&](void* target, bool managed) {
            check(managed, "Engine owns the GPU draw scope");
            host.target = target;
            const auto result = cache.render(frame, content, paint);
            host.target = nullptr;
            return result == CD_OK;
        });
        check(view != nullptr, "GPU primary output");
        return capture(device.Get(), immediate.Get(), view);
    };
    const D3D11_VIEWPORT viewport{3, 5, 17, 19, 0, 1};
    immediate->RSSetViewports(1, &viewport);
    const auto first = gpu(1);
    check(calls == 1 && host.copied == 0 &&
              std::all_of(bytes.begin(), bytes.end(),
                          [](auto value) {
                              return value == 0xcd;
                          }),
          "GPU output performs no CPU frame write or copy");
    check(first[0] == 0xaa && first[1] == 0x66 && first[2] == 0x22 && first[3] == 255,
          "Nested CPU frame cannot clear the main GPU output");
    check(first[(12 * 64 + 12) * 4 + 1] == 0xdd, "Borrowed producer draws without clearing outer output");
    check(gpu(1) == first && calls == 1 && host.unchanged == 1, "Retained GPU version avoids repaint");
    check(cache.render(frame, 1, paint) == CD_OK && calls == 2, "CPU capture has an independent cache");
    check(cache.render(frame, 1, paint) == CD_OK && calls == 2, "Repeated CPU capture reuses pixels");
    check(gpu(1) == first && calls == 2, "CPU capture leaves retained GPU content valid");
    gpu(2);
    check(calls == 3, "Content revision repaints GPU");
    ++host.epoch;
    gpu(2);
    check(calls == 4, "Output epoch invalidates retained output");
    D3D11_VIEWPORT restored{};
    UINT count = 1;
    immediate->RSGetViewports(&count, &restored);
    check(count == 1 && restored.TopLeftX == 3 && restored.Width == 17, "Interop restores compositor state");
    host.target = nullptr;
    canvas.begin(frame, {});
    canvas.box(0, 0, 64, 64, 0, 0x123456ff);
    canvas.end(frame);
    check(bytes[0] == 0x12 && bytes[3] == 255, "Low-level begin/end shares the output contract");
    const auto budgetBefore = host.budget.used();
    cybercanvas::SurfaceImage held;
    canvas.render(frame, {}, [&](auto& c, auto) {
        cybercanvas::OffscreenSurface temporary;
        c.renderLayer(temporary, {16, 16, 1}, [](auto& child) {
            child.box(0, 0, 16, 16, 0, 0x123456ff);
        });
        held = temporary.snapshot();
    });
    check(host.budget.used() == budgetBefore + 16 * 16 * 4, "CPU snapshot retains its lease after surface teardown");
    held.Reset();
    check(host.budget.used() == budgetBefore, "Last CPU bitmap owner releases resource accounting");
    cybercanvas::OffscreenSurface atlas;
    const auto sprites = [&](const CdFrame& output) {
        canvas.render(output, {}, [&](auto& c, auto) {
            if (!host.target && atlas.bitmap()) {
                check(!atlas.matches(c.target.Get(), {4, 4, 2}), "GPU material is not a valid CPU surface cache");
            }
            c.renderLayer(atlas, {4, 4, 2}, [](auto& layer) {
                for (unsigned y = 0; y < 4; ++y) {
                    for (unsigned x = 0; x < 4; ++x) {
                        layer.box(x * .5, y * .5, .5, .5, 0, x % 2 ? 0x0000ffff : 0xff0000ff);
                    }
                }
            });
            check(atlas.matches(c.target.Get(), {4, 4, 2}), "Rebuilt surface matches the actual output domain and DPI");
            const std::array<D2D1_RECT_F, 1> destination{{{0, 0, 8, 8}}};
            const std::array<D2D1_RECT_U, 1> source{{{0, 0, 4, 4}}};
            const std::array<float, 1> opacity{.5f};
            c.sprites(atlas.bitmap(), destination, source, {}, cybercanvas::ImageSampling::Nearest, opacity);
        });
    };
    auto* spriteView = surface.render(device.Get(), immediate.Get(), 64, 64, 1, [&](void* target, bool) {
        host.target = target;
        sprites(frame);
        host.target = nullptr;
        return true;
    });
    check(spriteView != nullptr, "GPU sprite sampling");
    const auto spritePixels = capture(device.Get(), immediate.Get(), spriteView);
    sprites(frame);
    for (size_t i = 0; i < bytes.size(); i += 4) {
        check(std::abs(int(spritePixels[i]) - bytes[i + 2]) <= 1 &&
                  std::abs(int(spritePixels[i + 1]) - bytes[i + 1]) <= 1 &&
                  std::abs(int(spritePixels[i + 2]) - bytes[i]) <= 1 &&
                  std::abs(int(spritePixels[i + 3]) - bytes[i + 3]) <= 1,
              "Nearest sampling and opacity match the DPI-aware WIC fallback");
    }
    Host legacy;
    cybercanvas::PrimitiveSprites primitives;
    int rasterizations = 0;
    const auto primitivesPaint = [&](const CdFrame& output) {
        canvas.render(output, {}, [&](auto& drawing, auto) {
            primitives.begin(drawing, 4);
            for (unsigned i = 0; i < 200; ++i) {
                primitives.add(drawing, i % 80, {double(i % 12 * 5 + 2), double(i / 12 * 3 + 2)}, [&](auto& layer) {
                    ++rasterizations;
                    layer.box(-1, -1, 2, 2, 0, 0x123456ff);
                });
            }
            primitives.flush(drawing);
        });
    };
    primitivesPaint(frame);
    check(rasterizations == 80, "Repeated vector primitives rasterize only once per material");
    primitivesPaint(frame);
    check(rasterizations == 80, "Large material palette persists across unchanged frames");
    legacy.legacy = true;
    cdsdk::Canvas old;
    old.bindGpu(legacy.create());
    old.render(frame, {}, [](auto& c, auto) {
        c.box(0, 0, 64, 64, 0, 0xabcdefFF);
    });
    check(bytes[0] == 0xab, "Older optional GPU service retains CPU compatibility");
    std::cout << "PASS " << (driver == D3D_DRIVER_TYPE_WARP ? "WARP" : "hardware")
              << " output sessions, nested drawing, cache versions, capture and D3D state\n";
}

void imageRecovery() {
    const auto directory = std::filesystem::temp_directory_path() / "cybercanvas-render-output-contract";
    std::filesystem::create_directories(directory);
    const auto path = directory / "recover.png";
    cdsdk::Canvas canvas;
    std::vector<uint8_t> pixels(8 * 8 * 4, 255);
    CdFrame frame{sizeof(CdFrame), 8, 8, 32, pixels.size(), pixels.data(), 1};
    check(canvas.save(path.wstring(), frame), "Write fixed recovery fixture");
    Host host(256);
    auto blocked = host.budget.reserve(256);
    auto store = std::make_shared<cybercanvas::ImageStore>();
    std::shared_ptr<const cybercanvas::ImageData> retained;
    {
        cdsdk::Images images(store);
        images.bind(host.create());
        images.set("recover", path, 1, 8);
        const auto wait = [&](cybercanvas::ImageState state) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            do {
                images.poll();
                if (images.get("recover").state == state) {
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            } while (std::chrono::steady_clock::now() < deadline);
            const auto current = images.get("recover");
            throw std::runtime_error("Image budget recovery timed out: expected=" + std::to_string(int(state)) +
                                     " actual=" + std::to_string(int(current.state)) +
                                     " used=" + std::to_string(host.budget.used()) + " error=" + current.error);
        };
        wait(cybercanvas::ImageState::Failed);
        auto other = store->request(path, 1, 8);
        blocked.reset();
        wait(cybercanvas::ImageState::Ready);
        check(other.snapshot().state == cybercanvas::ImageState::Ready, "Retry repairs all shared failed handles");
        retained = images.get("recover").image;
        other.cancel();
        images.clear();
        store->trim();
    }
    store.reset();
    check(host.budget.used() == 256, "Decoded image lease survives its store");
    retained.reset();
    check(host.budget.used() == 0, "Final decoded image reference releases the host lease");
    std::filesystem::remove(path);
    std::filesystem::remove(directory);
    std::cout << "PASS shared image budget recovery and cross-owner lease lifetime\n";
}
}  // namespace

int main() try {
    run(D3D_DRIVER_TYPE_HARDWARE);
    run(D3D_DRIVER_TYPE_WARP);
    imageRecovery();
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
