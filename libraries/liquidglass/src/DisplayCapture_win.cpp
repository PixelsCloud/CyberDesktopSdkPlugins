#include <Windows.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <d3d11_4.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <windows.ui.composition.interop.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <thread>
#include <utility>

#include "LiquidGlass/Capture.h"
#include "LiquidGlass/Effect.h"
#include "libraries/liquidglass/src/GpuTimer_win.h"

namespace liquidglass {
namespace {
namespace capture = winrt::Windows::Graphics::Capture;
namespace composition = winrt::Windows::UI::Composition;
using Microsoft::WRL::ComPtr;
constexpr uint64_t kRegionPixels = 8 * 1024 * 1024;
constexpr uint64_t kFramePoolBytes = 256 * 1024 * 1024;

constexpr char kCompareShader[] = R"(
cbuffer Region : register(b0) {
    uint2 Origin; uint2 Extent;

};
Texture2D<float4> Current : register(t0);
Texture2D<float4> Previous : register(t1);
RWByteAddressBuffer Difference : register(u0);
[numthreads(8, 8, 1)]
void Compare(uint3 threadId : SV_DispatchThreadID) {
    uint2 start = threadId.xy * 4;
    if (any(start >= Extent)) return;
    // Only one bit is needed. A thread examines a 4x4 tile and publishes at
    // most one atomic, instead of serializing every changed desktop pixel on
    // the same four-byte address. Unchanged frames still examine every pixel.
    [unroll] for (uint y = 0; y < 4; ++y) {
        [unroll] for (uint x = 0; x < 4; ++x) {
            uint2 pixel = start + uint2(x, y);
            if (any(pixel >= Extent)) continue;
            const float3 current = Current.Load(int3(pixel + Origin, 0)).rgb;
            const float3 previous = Previous.Load(int3(pixel, 0)).rgb;
            if (any(abs(current - previous) > 0.1 / 255.0)) {
                uint ignored;
                Difference.InterlockedOr(0, 1, ignored);
                return;
            }
        }
    }
}
)";

// Dirty rectangles can include excluded visuals whose captured pixels stayed
// unchanged. Only a four-byte change flag is read back, on this worker.
class PixelComparison final {
public:
    explicit PixelComparison(ID3D11Device& device) {
        device.GetImmediateContext(&m_context);
        ComPtr<ID3D11Device5> modern;
        if (SUCCEEDED(device.QueryInterface(IID_PPV_ARGS(&modern))) && SUCCEEDED(m_context.As(&m_context4)) &&
            SUCCEEDED(modern->CreateFence(0, D3D11_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)))) {
            m_completed.attach(CreateEventW(nullptr, FALSE, FALSE, nullptr));
            winrt::check_bool(bool(m_completed));
        }
        ComPtr<ID3DBlob> shader, error;
        const auto compiled =
            D3DCompile(kCompareShader, sizeof(kCompareShader) - 1, "LiquidGlass.Compare", nullptr, nullptr, "Compare",
                       "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &shader, &error);
        if (FAILED(compiled)) {
            const auto detail =
                error ? std::string(static_cast<const char*>(error->GetBufferPointer()), error->GetBufferSize())
                      : "GPU comparison shader compilation failed";
            throw winrt::hresult_error(compiled, winrt::to_hstring(detail));
        }
        winrt::check_hresult(
            device.CreateComputeShader(shader->GetBufferPointer(), shader->GetBufferSize(), nullptr, &m_shader));
        D3D11_BUFFER_DESC description{};
        description.ByteWidth = 16;
        description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        winrt::check_hresult(device.CreateBuffer(&description, nullptr, &m_constants));
        description.ByteWidth = 4;
        description.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        description.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
        winrt::check_hresult(device.CreateBuffer(&description, nullptr, &m_flag));
        D3D11_UNORDERED_ACCESS_VIEW_DESC view{};
        view.Format = DXGI_FORMAT_R32_TYPELESS;
        view.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        view.Buffer.NumElements = 1;
        view.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
        winrt::check_hresult(device.CreateUnorderedAccessView(m_flag.Get(), &view, &m_view));
        description.BindFlags = description.MiscFlags = 0;
        description.Usage = D3D11_USAGE_STAGING;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        winrt::check_hresult(device.CreateBuffer(&description, nullptr, &m_readback));
    }

    bool probe(ID3D11ShaderResourceView& current, ID3D11ShaderResourceView* previous, Region area) {
        const std::array<UINT, 4> constants{UINT(area.x), UINT(area.y), UINT(area.width), UINT(area.height)};
        m_context->UpdateSubresource(m_constants.Get(), 0, nullptr, constants.data(), 0, 0);
        const UINT clear[4]{};
        m_context->ClearUnorderedAccessViewUint(m_view.Get(), clear);
        ID3D11Buffer* buffers[] = {m_constants.Get()};
        ID3D11ShaderResourceView* inputs[] = {&current, previous};
        ID3D11UnorderedAccessView* outputs[] = {m_view.Get()};
        m_context->CSSetShader(m_shader.Get(), nullptr, 0);
        m_context->CSSetConstantBuffers(0, 1, buffers);
        m_context->CSSetShaderResources(0, 2, inputs);
        m_context->CSSetUnorderedAccessViews(0, 1, outputs, nullptr);
        m_context->Dispatch((UINT(area.width) + 31) / 32, (UINT(area.height) + 31) / 32, 1);
        ID3D11ShaderResourceView* emptyInputs[2]{};
        ID3D11UnorderedAccessView* emptyOutputs[1]{};
        m_context->CSSetShaderResources(0, 2, emptyInputs);
        m_context->CSSetUnorderedAccessViews(0, 1, emptyOutputs, nullptr);
        m_context->CSSetShader(nullptr, nullptr, 0);
        m_context->CopyResource(m_readback.Get(), m_flag.Get());
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
        if (m_fence) {
            winrt::check_hresult(m_context4->Signal(m_fence.Get(), ++m_fenceValue));
            winrt::check_hresult(m_fence->SetEventOnCompletion(m_fenceValue, m_completed.get()));
        }
        m_context->Flush();
        if (m_fence) {
            const auto result = WaitForSingleObject(m_completed.get(), 50);
            if (result == WAIT_FAILED) {
                winrt::throw_last_error();
            }
            if (result != WAIT_OBJECT_0) {
                throw std::runtime_error("GPU change comparison exceeded its 50 ms readback deadline");
            }
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        while (true) {
            const auto result =
                m_context->Map(m_readback.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
            if (result != DXGI_ERROR_WAS_STILL_DRAWING) {
                winrt::check_hresult(result);
                break;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                throw std::runtime_error("GPU change comparison exceeded its 50 ms readback deadline");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const UINT flags = *static_cast<const UINT*>(mapped.pData);
        m_context->Unmap(m_readback.Get(), 0);
        return bool(flags & 1);
    }

private:
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<ID3D11DeviceContext4> m_context4;
    ComPtr<ID3D11Fence> m_fence;
    winrt::handle m_completed;
    uint64_t m_fenceValue = 0;
    ComPtr<ID3D11ComputeShader> m_shader;
    ComPtr<ID3D11Buffer> m_constants, m_flag, m_readback;
    ComPtr<ID3D11UnorderedAccessView> m_view;
};

bool contains(Region outer, Region inner) {
    return inner.x >= outer.x && inner.y >= outer.y && inner.x + inner.width <= outer.x + outer.width &&
           inner.y + inner.height <= outer.y + outer.height;
}

bool intersects(Region first, Region second) {
    return first.x < second.x + second.width && first.y < second.y + second.height &&
           first.x + first.width > second.x && first.y + first.height > second.y;
}

Region padded(Region region, int halo) {
    return {region.x - halo, region.y - halo, region.width + 2 * halo, region.height + 2 * halo};
}

Region clipped(Region first, Region second) {
    const int x = std::max(first.x, second.x), y = std::max(first.y, second.y);
    return {x, y, std::max(0, std::min(first.x + first.width, second.x + second.width) - x),
            std::max(0, std::min(first.y + first.height, second.y + second.height) - y)};
}

struct Wake {
    std::mutex mutex;
    std::condition_variable_any condition;
    bool pending = true;
    std::atomic_uint64_t framePoolBytes{0};
    std::atomic_uint64_t recoveryRevision{0};
    std::atomic_bool resetRequested{false};
    std::atomic_bool suspended{false};

    void post() {
        {
            std::lock_guard lock(mutex);
            pending = true;
        }
        condition.notify_one();
    }
};

struct Frame {
    explicit Frame(capture::Direct3D11CaptureFrame frame) : value(std::move(frame)) {}

    Frame(Frame&& other) noexcept : value(std::move(other.value)) {}

    Frame& operator=(Frame&& other) noexcept {
        std::swap(value, other.value);
        return *this;
    }

    ~Frame() {
        try {
            if (value) {
                value.Close();
            }
        } catch (...) {
        }
    }

    capture::Direct3D11CaptureFrame value{nullptr};
};

struct FrameMailbox {
    std::mutex mutex;
    std::optional<Frame> latest;
    std::string error;
    std::weak_ptr<Wake> wake;
    std::atomic_bool active{true};
};

// Each output owns one two-buffer GPU frame pool. Its callback only transfers
// the latest frame; all texture work is serialized on the coordinator worker.
class Output final {
public:
    Output(uintptr_t monitor, const winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice& device,
           std::weak_ptr<Wake> wake)
        : m_mailbox(std::make_shared<FrameMailbox>()) {
        m_mailbox->wake = std::move(wake);
        regions.reserve(1024);
        MONITORINFO information{sizeof(information)};
        winrt::check_bool(GetMonitorInfoW(reinterpret_cast<HMONITOR>(monitor), &information));
        const auto& area = information.rcMonitor;
        bounds = {area.left, area.top, area.right - area.left, area.bottom - area.top};
        const auto factory = winrt::get_activation_factory<capture::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        winrt::check_hresult(factory->CreateForMonitor(reinterpret_cast<HMONITOR>(monitor),
                                                       winrt::guid_of<capture::GraphicsCaptureItem>(),
                                                       winrt::put_abi(m_item)));
        const auto size = m_item.Size();
        if (size.Width < 1 || size.Height < 1 || uint64_t(size.Width) * size.Height * 8 > kFramePoolBytes) {
            throw std::runtime_error("Display capture exceeds the 256 MiB shared GPU frame-pool budget");
        }
        bytes = uint64_t(size.Width) * size.Height * 8;
        m_pool = capture::Direct3D11CaptureFramePool::CreateFreeThreaded(
            device, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
        m_session = m_pool.CreateCaptureSession(m_item);
        m_display = m_session.try_as<capture::IDisplayGraphicsCaptureSession>();
        if (!m_display) {
            throw std::runtime_error("This Windows version does not support capture-session window exclusion");
        }
        m_session.IsCursorCaptureEnabled(false);
        m_session.IsBorderRequired(false);
        m_session.MinUpdateInterval(std::chrono::milliseconds(125));
        m_session.DirtyRegionMode(capture::GraphicsCaptureDirtyRegionMode::ReportOnly);
        const std::weak_ptr<FrameMailbox> weak = m_mailbox;
        m_arrived = m_pool.FrameArrived(winrt::auto_revoke, [weak](const auto& sender, const auto&) {
            const auto mailbox = weak.lock();
            if (!mailbox || !mailbox->active.load()) {
                return;
            }
            try {
                auto value = sender.TryGetNextFrame();
                if (!value) {
                    return;
                }
                {
                    std::lock_guard lock(mailbox->mutex);
                    mailbox->latest.emplace(std::move(value));
                }
            } catch (const winrt::hresult_error& error) {
                std::lock_guard lock(mailbox->mutex);
                mailbox->error = winrt::to_string(error.message());
            }
            if (auto wake = mailbox->wake.lock()) {
                wake->post();
            }
        });
        m_closed = m_item.Closed(winrt::auto_revoke, [weak](const auto&, const auto&) {
            const auto mailbox = weak.lock();
            if (!mailbox || !mailbox->active.load()) {
                return;
            }
            {
                std::lock_guard lock(mailbox->mutex);
                mailbox->error = "Captured display became unavailable";
            }
            if (auto wake = mailbox->wake.lock()) {
                wake->post();
            }
        });
    }

    ~Output() {
        m_mailbox->active.store(false);
        m_arrived.revoke();
        m_closed.revoke();
        try {
            m_session.Close();
            m_pool.Close();
        } catch (...) {
        }
    }

    void exclude(const std::vector<uintptr_t>& windows, uint64_t revision) {
        if (m_started && m_exclusions == windows && m_exclusionRevision == revision) {
            return;
        }
        std::vector<winrt::Windows::UI::WindowId> values;
        values.reserve(windows.size());
        for (auto window : windows) {
            if (IsWindow(reinterpret_cast<HWND>(window))) {
                values.push_back({uint64_t(window)});
            }
        }
        m_iteration = m_display.SetWindowExclusionList(values);
        static const bool trace = GetEnvironmentVariableW(L"CYBER_LIQUID_TRACE", nullptr, 0) > 0;
        static std::atomic_int tracesRemaining{8};
        if (trace && tracesRemaining.fetch_sub(1) > 0) {
            std::fprintf(stderr, "GPU exclusions %zu configuration %llu\n", values.size(), m_iteration);
            for (const auto& value : values) {
                const auto window = reinterpret_cast<HWND>(uintptr_t(value.Value));
                RECT placement{};
                GetWindowRect(window, &placement);
                std::fprintf(stderr, "excluded %llx at %ld,%ld %ldx%ld\n", value.Value, placement.left, placement.top,
                             placement.right - placement.left, placement.bottom - placement.top);
            }
        }
        m_exclusions = windows;
        m_exclusionRevision = revision;
        if (!m_started) {
            m_session.StartCapture();
            m_started = true;
        }
    }

    bool take(ID2D1DeviceContext& context, ID3D11Device& device) {
        std::optional<Frame> next;
        {
            std::lock_guard lock(m_mailbox->mutex);
            if (!m_mailbox->error.empty()) {
                throw std::runtime_error(m_mailbox->error);
            }
            next = std::exchange(m_mailbox->latest, std::nullopt);
        }
        if (!next || next->value.ConfigurationIteration() < m_iteration) {
            return false;
        }
        const auto size = next->value.ContentSize();
        if (size.Width != bounds.width || size.Height != bounds.height) {
            throw std::runtime_error("Display capture size changed; recreate its output resources");
        }
        ComPtr<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess> access;
        winrt::check_hresult(
            static_cast<IUnknown*>(winrt::get_abi(next->value.Surface()))->QueryInterface(IID_PPV_ARGS(&access)));
        ComPtr<IDXGISurface> surface;
        winrt::check_hresult(access->GetInterface(IID_PPV_ARGS(&surface)));
        ComPtr<IUnknown> identity;
        winrt::check_hresult(surface.As(&identity));
        auto found = std::find_if(m_cache.begin(), m_cache.end(), [&identity](const auto& entry) {
            return entry.identity.Get() == identity.Get();
        });
        if (found == m_cache.end()) {
            found = m_cache.begin() + m_cacheIndex;
            m_cacheIndex = (m_cacheIndex + 1) % m_cache.size();
            *found = {};
            found->identity = std::move(identity);
            const auto properties = D2D1::BitmapProperties1(
                D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
            winrt::check_hresult(context.CreateBitmapFromDxgiSurface(surface.Get(), &properties, &found->bitmap));
            winrt::check_hresult(surface.As(&found->texture));
            winrt::check_hresult(device.CreateShaderResourceView(found->texture.Get(), nullptr, &found->view));
        }
        regions.clear();
        const auto dirty = next->value.DirtyRegions();
        if (dirty.Size() > 1024) {
            regions.push_back(bounds);
        } else {
            for (const auto& area : dirty) {
                regions.push_back({bounds.x + area.X, bounds.y + area.Y, area.Width, area.Height});
            }
        }
        m_bitmap = found->bitmap;
        m_texture = found->texture;
        m_view = found->view;
        m_frame = std::move(next);
        return true;
    }

    ID2D1Bitmap1* bitmap() const {
        return m_frame && m_frame->value.ConfigurationIteration() >= m_iteration ? m_bitmap.Get() : nullptr;
    }

    ID3D11ShaderResourceView& view() const {
        return *m_view.Get();
    }

    ID3D11Texture2D& texture() const {
        return *m_texture.Get();
    }

    Region bounds;
    uint64_t bytes = 0;
    std::vector<Region> regions;

private:
    std::shared_ptr<FrameMailbox> m_mailbox;
    capture::GraphicsCaptureItem m_item{nullptr};
    capture::Direct3D11CaptureFramePool m_pool{nullptr};
    capture::GraphicsCaptureSession m_session{nullptr};
    capture::IDisplayGraphicsCaptureSession m_display{nullptr};
    capture::Direct3D11CaptureFramePool::FrameArrived_revoker m_arrived;
    capture::GraphicsCaptureItem::Closed_revoker m_closed;
    std::optional<Frame> m_frame;
    ComPtr<ID2D1Bitmap1> m_bitmap;
    ComPtr<ID3D11Texture2D> m_texture;
    ComPtr<ID3D11ShaderResourceView> m_view;

    struct CachedTexture {
        ComPtr<IUnknown> identity;
        ComPtr<ID2D1Bitmap1> bitmap;
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11ShaderResourceView> view;
    };

    std::array<CachedTexture, 2> m_cache;
    size_t m_cacheIndex = 0;
    std::vector<uintptr_t> m_exclusions;
    uint64_t m_iteration = 0;
    uint64_t m_exclusionRevision = 0;
    bool m_started = false;
};
}  // namespace

struct CaptureSource::Impl {
    mutable std::mutex mutex;
    Region panel, captured;
    Parameters parameters;
    float dpi = 1, corner = 24;
    bool attachedTop = false;
    int halo = 128, renderedWidth = 0, renderedHeight = 0, surfaceWidth = 0, surfaceHeight = 0;
    float appliedScaleX = -1, appliedScaleY = -1;
    uint64_t contentRevision = 0, allocatedBytes = 0;
    uintptr_t monitor = 0;
    std::atomic_bool active{true}, ready{false}, dirty{true};
    std::atomic_bool opticalDirty{true};
    std::string error;
    std::weak_ptr<Wake> wake;
    std::function<void()> notify;
    composition::CompositionSurfaceBrush brush{nullptr};
    composition::CompositionDrawingSurface surface{nullptr};
    ComPtr<ID3D11Texture2D> cached;
    ComPtr<ID3D11ShaderResourceView> cachedView;
    std::unique_ptr<EffectTarget> optical;
    std::shared_ptr<const ShadowImage> shadow;
    std::unique_ptr<detail::GpuTimer> timer;
    CaptureStatistics statistics;
    uint32_t statisticsRemaining = 0;
    uint64_t statisticsRevision = 0;

    struct Measurement {
        Impl& source;
        uint64_t revision = 0, copiedBytes = 0, assetReadbackBytes = 0;
        uint64_t compareWall = 0, drawWall = 0, acquireWall = 0, commitWall = 0;
        bool enabled = false, compared = false, changed = true, succeeded = false;
        detail::GpuInterval gpu;
        std::chrono::steady_clock::time_point began{};

        explicit Measurement(Impl& value) : source(value) {
            std::lock_guard lock(source.mutex);
            enabled = source.statisticsRemaining > 0;
            revision = source.statisticsRevision;
            if (enabled) {
                --source.statisticsRemaining;
                ++source.statistics.captures;
                began = std::chrono::steady_clock::now();
            }
        }

        ~Measurement() {
            if (!enabled) {
                return;
            }
            std::lock_guard lock(source.mutex);
            if (revision == source.statisticsRevision) {
                source.statistics.gpuCopyBytes += copiedBytes;
                source.statistics.unchanged += !changed;
                source.statistics.failed += !succeeded;
                source.statistics.readbackBytes += compared ? 4 : 0;
                source.statistics.assetReadbackBytes += assetReadbackBytes;
                source.statistics.gpuSamples += gpu.valid;
                source.statistics.gpuNanoseconds += gpu.nanoseconds;
                source.statistics.gpuDisjointSamples += gpu.disjoint;
                source.statistics.gpuTimeouts += gpu.timedOut;
                source.statistics.compareWallMicroseconds += compareWall;
                source.statistics.drawWallMicroseconds += drawWall;
                source.statistics.surfaceAcquireWallMicroseconds += acquireWall;
                source.statistics.surfaceCommitWallMicroseconds += commitWall;
                source.statistics.captureWallMicroseconds += uint64_t(
                    std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - began)
                        .count());
            }
        }
    };

    void changed() const {
        if (auto receiver = wake.lock()) {
            receiver->post();
        }
    }
};

struct CaptureCoordinator::Impl {
    composition::Compositor compositor{nullptr};
    composition::CompositionGraphicsDevice graphics{nullptr};
    std::shared_ptr<Wake> wake = std::make_shared<Wake>();
    std::mutex recordsMutex;
    std::vector<std::weak_ptr<CaptureSource::Impl>> records;
    std::vector<uintptr_t> exclusions;
    uint64_t exclusionRevision = 0;
    std::map<uintptr_t, std::unique_ptr<Output>> outputs;
    ComPtr<ID3D11Device> device;
    ComPtr<ID2D1Device> d2dDevice;
    ComPtr<ID2D1DeviceContext> d2d;
    std::unique_ptr<PixelComparison> comparison;
    std::unique_ptr<Effect> optics;
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice native{nullptr};
    std::jthread worker;
    std::function<void()> beforeCompose;

    Impl(void* compositorAbi, std::function<void()> observer) : beforeCompose(std::move(observer)) {
        winrt::copy_from_abi(compositor, compositorAbi);
        worker = std::jthread([this](std::stop_token stop) {
            run(stop);
        });
    }

    ~Impl() {
        worker.request_stop();
        wake->post();
        worker.join();
    }

    std::vector<std::shared_ptr<CaptureSource::Impl>> sources() {
        std::vector<std::shared_ptr<CaptureSource::Impl>> result;
        std::lock_guard lock(recordsMutex);
        std::erase_if(records, [](const auto& value) {
            return value.expired();
        });
        for (const auto& value : records) {
            if (auto source = value.lock()) {
                result.push_back(std::move(source));
            }
        }
        return result;
    }

    void initialize() {
        if (device) {
            return;
        }
        winrt::check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                               D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &device,
                                               nullptr, nullptr));
        ComPtr<IDXGIDevice> dxgi;
        winrt::check_hresult(device.As(&dxgi));
        ComPtr<ID3D11DeviceContext> immediate;
        device->GetImmediateContext(&immediate);
        ComPtr<ID3D11Multithread> protection;
        winrt::check_hresult(immediate.As(&protection));
        protection->SetMultithreadProtected(TRUE);
        ComPtr<IInspectable> identity;
        winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(), &identity));
        winrt::copy_from_abi(native, identity.Get());
        comparison = std::make_unique<PixelComparison>(*device.Get());
        ComPtr<ID2D1Factory1> factory;
        winrt::check_hresult(D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, factory.GetAddressOf()));
        winrt::check_hresult(factory->CreateDevice(dxgi.Get(), &d2dDevice));
        winrt::check_hresult(d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &d2d));
        d2dDevice->SetMaximumTextureMemory(64 * 1024 * 1024);
        optics = std::make_unique<Effect>();
        optics->initialize(device.Get(), d2d.Get());
        auto interop = compositor.as<ABI::Windows::UI::Composition::ICompositorInterop>();
        winrt::check_hresult(interop->CreateGraphicsDevice(
            d2dDevice.Get(),
            reinterpret_cast<ABI::Windows::UI::Composition::ICompositionGraphicsDevice**>(winrt::put_abi(graphics))));
    }

    void fail(CaptureSource::Impl& source, std::string error) {
        bool changed = false;
        {
            std::lock_guard lock(source.mutex);
            changed = source.error != error || source.ready.load();
            source.error = std::move(error);
        }
        source.ready.store(false);
        if (changed && source.active.load() && source.notify) {
            source.notify();
        }
    }

    void release(CaptureSource::Impl& source) noexcept {
        source.ready.store(false);
        try {
            if (source.brush && source.surface) {
                source.brush.Surface(nullptr);
            }
        } catch (const winrt::hresult_error&) {
        }
        source.surface = nullptr;
        source.optical.reset();
        source.timer.reset();
        source.cachedView.Reset();
        source.cached.Reset();
        {
            std::lock_guard lock(source.mutex);
            source.captured = {};
            source.renderedWidth = source.renderedHeight = source.surfaceWidth = source.surfaceHeight = 0;
            source.allocatedBytes = 0;
            source.shadow.reset();
        }
        source.dirty.store(true);
    }

    uint64_t draw(CaptureSource::Impl& source, const Output& output, Region region, Region panel, Parameters parameters,
                  float dpi, float corner, bool attachedTop, bool updateInput,
                  CaptureSource::Impl::Measurement* sample) {
        Region previous;
        int previousWidth = 0, previousHeight = 0;
        {
            std::lock_guard lock(source.mutex);
            previous = source.captured;
            previousWidth = source.renderedWidth;
            previousHeight = source.renderedHeight;
        }
        const bool resizedInput = !source.cached || previous.width != region.width || previous.height != region.height;
        if (resizedInput) {
            source.cachedView.Reset();
            source.cached.Reset();
            D3D11_TEXTURE2D_DESC description{};
            description.Width = UINT(region.width);
            description.Height = UINT(region.height);
            description.MipLevels = description.ArraySize = description.SampleDesc.Count = 1;
            description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            winrt::check_hresult(device->CreateTexture2D(&description, nullptr, &source.cached));
            winrt::check_hresult(device->CreateShaderResourceView(source.cached.Get(), nullptr, &source.cachedView));
        }
        uint64_t copiedBytes = 0;
        if (updateInput || resizedInput) {
            ComPtr<ID3D11DeviceContext> context;
            device->GetImmediateContext(&context);
            const D3D11_BOX copy{UINT(region.x - output.bounds.x),
                                 UINT(region.y - output.bounds.y),
                                 0,
                                 UINT(region.x + region.width - output.bounds.x),
                                 UINT(region.y + region.height - output.bounds.y),
                                 1};
            context->CopySubresourceRegion(source.cached.Get(), 0, 0, 0, 0, &output.texture(), 0, &copy);
            ++source.contentRevision;
            copiedBytes += uint64_t(region.width) * region.height * 4;
        }
        if (!source.optical) {
            source.optical = std::make_unique<EffectTarget>();
        }
        const EffectGeometry geometry{
            panel.width, panel.height, float(panel.x - region.x), float(panel.y - region.y), dpi, corner, attachedTop};
        optics->render(*source.optical, source.cached.Get(), geometry, parameters, source.contentRevision);
        if (sample) {
            sample->assetReadbackBytes += source.optical->assetReadbackBytes();
        }
        const int width = source.optical->width(), height = source.optical->height();
        const bool resized = source.surfaceWidth != width || source.surfaceHeight != height;
        const bool initializeSurface = !source.surface || resized;
        if (!source.surface) {
            using namespace winrt::Windows::Graphics::DirectX;
            source.surface =
                graphics.CreateDrawingSurface({float(width), float(height)}, DirectXPixelFormat::B8G8R8A8UIntNormalized,
                                              DirectXAlphaMode::Premultiplied);
            if (!source.brush) {
                source.brush = compositor.CreateSurfaceBrush();
                source.brush.Stretch(composition::CompositionStretch::None);
                source.brush.HorizontalAlignmentRatio(0);
                source.brush.VerticalAlignmentRatio(0);
            }
            source.brush.Surface(source.surface);
        } else if (resized) {
            source.surface.Resize({width, height});
        }
        auto interop = source.surface.as<ABI::Windows::UI::Composition::ICompositionDrawingSurfaceInterop>();
        ComPtr<ID2D1DeviceContext> target;
        POINT offset{};
        const RECT changed{0, 0, panel.width, panel.height};
        const auto acquire = sample ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        winrt::check_hresult(
            interop->BeginDraw(initializeSurface ? nullptr : &changed, IID_PPV_ARGS(&target), &offset));
        if (sample) {
            sample->acquireWall += uint64_t(
                std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - acquire)
                    .count());
        }
        target->SetTransform(D2D1::Matrix3x2F::Translation(float(offset.x), float(offset.y)));
        if (initializeSurface) {
            target->Clear(D2D1::ColorF(0, 0, 0, 0));
        }
        const auto area = D2D1::RectF(0, 0, float(panel.width), float(panel.height));
        // Replace transparent edge pixels too: source-over would retain the
        // previous contour during resize and accumulate its coverage over time.
        target->DrawImage(static_cast<ID2D1Bitmap1*>(source.optical->bitmap()), D2D1::Point2F(), area,
                          D2D1_INTERPOLATION_MODE_LINEAR, D2D1_COMPOSITE_MODE_SOURCE_COPY);
        const auto commit = sample ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        winrt::check_hresult(interop->EndDraw());
        if (sample) {
            sample->commitWall += uint64_t(
                std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - commit)
                    .count());
        }
        copiedBytes += uint64_t(panel.width) * panel.height * 4;
        bool shadowChanged = false;
        {
            std::lock_guard lock(source.mutex);
            source.captured = region;
            const auto shadow = source.optical->shadowImage();
            shadowChanged = source.shadow != shadow;
            source.shadow = shadow;
            source.renderedWidth = panel.width;
            source.renderedHeight = panel.height;
            source.surfaceWidth = width;
            source.surfaceHeight = height;
            source.allocatedBytes = uint64_t(region.width) * region.height * 4 + uint64_t(width) * height * 4 +
                                    source.optical->allocatedBytes();
            source.error.clear();
        }
        const bool wasReady = source.ready.exchange(true);
        if (source.active.load() &&
            (!wasReady || resized || shadowChanged || previousWidth != panel.width || previousHeight != panel.height) &&
            source.notify) {
            source.notify();
        }
        return copiedBytes;
    }

    bool update(const std::vector<std::shared_ptr<CaptureSource::Impl>>& consumers) {
        std::set<uintptr_t> needed;
        std::vector<std::shared_ptr<CaptureSource::Impl>> visible;
        uint64_t pixels = 0;
        for (const auto& source : consumers) {
            Region panel;
            int halo = 0;
            {
                std::lock_guard lock(source->mutex);
                panel = source->panel;
                halo = source->halo;
            }
            if (!source->active.load() || panel.width < 1 || panel.height < 1) {
                release(*source);
                continue;
            }
            const auto region = padded(panel, halo);
            pixels += uint64_t(region.width) * region.height;
            if (pixels > kRegionPixels) {
                release(*source);
                fail(*source, "Shared glass regions exceed the 128 MiB GPU workspace budget");
                continue;
            }
            const RECT bounds{panel.x, panel.y, panel.x + panel.width, panel.y + panel.height};
            needed.insert(uintptr_t(MonitorFromRect(&bounds, MONITOR_DEFAULTTONEAREST)));
            visible.push_back(source);
        }
        std::erase_if(outputs, [&needed](const auto& value) {
            return !needed.contains(value.first);
        });
        if (needed.empty()) {
            wake->framePoolBytes.store(0);
            comparison.reset();
            optics.reset();
            graphics = nullptr;
            native = nullptr;
            d2d.Reset();
            d2dDevice.Reset();
            device.Reset();
            return false;
        }
        uint64_t plannedBytes = 0;
        for (auto monitor : needed) {
            MONITORINFO information{sizeof(information)};
            winrt::check_bool(GetMonitorInfoW(reinterpret_cast<HMONITOR>(monitor), &information));
            const auto& bounds = information.rcMonitor;
            plannedBytes += uint64_t(bounds.right - bounds.left) * (bounds.bottom - bounds.top) * 8;
            if (plannedBytes > kFramePoolBytes) {
                throw std::runtime_error("Shared display capture exceeds the 256 MiB GPU frame-pool budget");
            }
        }
        initialize();
        std::vector<uintptr_t> excluded;
        uint64_t revision = 0;
        {
            std::lock_guard lock(recordsMutex);
            excluded = exclusions;
            revision = exclusionRevision;
        }
        uint64_t outputBytes = 0;
        for (auto monitor : needed) {
            auto& output = outputs[monitor];
            if (!output) {
                output = std::make_unique<Output>(monitor, native, wake);
            }
            outputBytes += output->bytes;
            if (outputBytes > kFramePoolBytes) {
                throw std::runtime_error("Shared display capture exceeds the 256 MiB GPU frame-pool budget");
            }
            output->exclude(excluded, revision);
            const bool fresh = output->take(*d2d.Get(), *device.Get());
            if (!output->bitmap()) {
                continue;
            }
            for (const auto& source : visible) {
                if (!source->active.load()) {
                    continue;
                }
                Region panel, previous;
                Parameters parameters;
                float dpi = 1, corner = 0;
                bool attachedTop = false;
                int halo = 0;
                {
                    std::lock_guard lock(source->mutex);
                    panel = source->panel;
                    previous = source->captured;
                    parameters = source->parameters;
                    dpi = source->dpi;
                    corner = source->corner;
                    attachedTop = source->attachedTop;
                    halo = source->halo;
                }
                if (panel.width < 1 || panel.height < 1 || !contains(output->bounds, panel)) {
                    continue;
                }
                const auto region = clipped(padded(panel, halo), output->bounds);
                const bool dirty = source->dirty.exchange(false) || !source->ready.load() || !contains(previous, panel);
                const bool opticalDirty = source->opticalDirty.exchange(false);
                const bool changed =
                    fresh && std::any_of(output->regions.begin(), output->regions.end(), [region](Region area) {
                        return intersects(area, region);
                    });
                if (dirty || changed || opticalDirty) {
                    if (beforeCompose) {
                        beforeCompose();
                    }
                    if (!source->active.load()) {
                        continue;
                    }
                    CaptureSource::Impl::Measurement sample(*source);
                    if (sample.enabled) {
                        if (!source->timer) {
                            source->timer = std::make_unique<detail::GpuTimer>(device.Get());
                        }
                        source->timer->begin();
                    }
                    sample.changed = dirty || changed;
                    const bool canCompare = !dirty && source->cachedView && previous == region;
                    if (sample.changed && canCompare) {
                        sample.compared = true;
                        const Region relative{region.x - output->bounds.x, region.y - output->bounds.y, region.width,
                                              region.height};
                        const auto began =
                            sample.enabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
                        sample.changed = comparison->probe(output->view(), source->cachedView.Get(), relative);
                        if (sample.enabled) {
                            sample.compareWall = uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
                                                              std::chrono::steady_clock::now() - began)
                                                              .count());
                        }
                    }
                    const auto drawBegan =
                        sample.enabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
                    if (sample.changed) {
                        sample.copiedBytes = draw(*source, *output, region, panel, parameters, dpi, corner, attachedTop,
                                                  true, sample.enabled ? &sample : nullptr);
                    } else if (opticalDirty) {
                        sample.changed = true;
                        sample.copiedBytes = draw(*source, *output, region, panel, parameters, dpi, corner, attachedTop,
                                                  false, sample.enabled ? &sample : nullptr);
                    }
                    if (sample.enabled) {
                        sample.drawWall = uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
                                                       std::chrono::steady_clock::now() - drawBegan)
                                                       .count());
                        sample.gpu = source->timer->end();
                    }
                    sample.succeeded = true;
                }
            }
        }
        wake->framePoolBytes.store(outputBytes);
        return !visible.empty() && std::all_of(visible.begin(), visible.end(), [](const auto& source) {
            return source->ready.load();
        });
    }

    void resetResources(const std::vector<std::shared_ptr<CaptureSource::Impl>>& active) {
        outputs.clear();
        wake->framePoolBytes.store(0);
        for (const auto& source : active) {
            const bool wasReady = source->ready.load();
            release(*source);
            if (wasReady && source->active.load() && source->notify) {
                source->notify();
            }
        }
        graphics = nullptr;
        comparison.reset();
        optics.reset();
        native = nullptr;
        d2d.Reset();
        d2dDevice.Reset();
        device.Reset();
    }

    void run(std::stop_token stop) {
        const auto previousDpi = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        const auto apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        int failures = 0;
        uint64_t recoveryRevision = 0;
        using Clock = std::chrono::steady_clock;
        std::optional<Clock::time_point> retryAt, startupBy;
        while (!stop.stop_requested()) {
            {
                std::unique_lock lock(wake->mutex);
                const auto deadline = retryAt ? retryAt : startupBy;
                if (deadline) {
                    wake->condition.wait_until(lock, stop, *deadline, [this] {
                        return wake->pending;
                    });
                } else {
                    wake->condition.wait(lock, stop, [this] {
                        return wake->pending;
                    });
                }
                wake->pending = false;
            }
            if (stop.stop_requested()) {
                break;
            }
            const auto active = sources();
            const auto revision = wake->recoveryRevision.load();
            const bool reset = wake->resetRequested.exchange(false);
            if (revision != recoveryRevision || reset) {
                recoveryRevision = revision;
                failures = 0;
                retryAt.reset();
                startupBy.reset();
                if (reset) {
                    resetResources(active);
                }
            }
            const bool visible =
                !wake->suspended.load() && std::any_of(active.begin(), active.end(), [](const auto& source) {
                    std::lock_guard lock(source->mutex);
                    return source->active.load() && source->panel.width > 0 && source->panel.height > 0;
                });
            if (!visible) {
                resetResources(active);
                failures = 0;
                retryAt.reset();
                startupBy.reset();
                continue;
            }
            if (failures >= 3 || (retryAt && Clock::now() < *retryAt)) {
                continue;
            }
            retryAt.reset();
            std::string error;
            try {
                winrt::check_hresult(apartment);
                if (update(active)) {
                    // Initialization and empty frame callbacks are not recovery.
                    failures = 0;
                    startupBy.reset();
                } else if (!startupBy) {
                    startupBy = Clock::now() + std::chrono::seconds(3);
                } else if (Clock::now() >= *startupBy) {
                    throw std::runtime_error("Display capture did not deliver valid glass output within 3 seconds");
                }
            } catch (const winrt::hresult_error& failure) {
                error = winrt::to_string(failure.message()) + " (HRESULT " + std::to_string(failure.code().value) + ")";
            } catch (const std::exception& failure) {
                error = failure.what();
            }
            if (!error.empty()) {
                resetResources(active);
                for (const auto& source : active) {
                    fail(*source, error);
                }
                startupBy.reset();
                if (++failures < 3) {
                    retryAt = Clock::now() + std::chrono::seconds(failures);
                }
            }
        }
        outputs.clear();
        wake->framePoolBytes.store(0);
        for (const auto& source : sources()) {
            release(*source);
        }
        graphics = nullptr;
        comparison.reset();
        optics.reset();
        native = nullptr;
        d2d.Reset();
        d2dDevice.Reset();
        device.Reset();
        if (SUCCEEDED(apartment)) {
            CoUninitialize();
        }
        if (previousDpi) {
            SetThreadDpiAwarenessContext(previousDpi);
        }
    }
};

CaptureSource::CaptureSource(std::shared_ptr<Impl> state) : m_impl(std::move(state)) {}

CaptureSource::~CaptureSource() {
    m_impl->active.store(false);
    m_impl->changed();
}

void CaptureSource::configure(const Parameters& requested, float dpiScale, float corner, bool attachedTop) {
    auto parameters = requested;
    parameters.normalize();
    dpiScale = std::isfinite(dpiScale) ? std::clamp(dpiScale, .25f, 8.f) : 1;
    corner = std::isfinite(corner) ? std::max(0.f, corner) : 0;
    const int halo = std::max(8, int(std::ceil(dpiScale * (parameters.blurSigma() * 3 + 4))));
    {
        std::lock_guard lock(m_impl->mutex);
        if (m_impl->parameters == parameters && m_impl->dpi == dpiScale && m_impl->corner == corner &&
            m_impl->halo == halo && m_impl->attachedTop == attachedTop) {
            return;
        }
        m_impl->parameters = parameters;
        m_impl->dpi = dpiScale;
        m_impl->corner = corner;
        m_impl->attachedTop = attachedTop;
        if (m_impl->halo != halo) {
            m_impl->halo = halo;
            m_impl->dirty.store(true);
        }
    }
    m_impl->opticalDirty.store(true);
    m_impl->changed();
}

std::shared_ptr<const ShadowImage> CaptureSource::shadowImage() const {
    std::lock_guard lock(m_impl->mutex);
    return m_impl->ready.load() ? m_impl->shadow : nullptr;
}

bool CaptureSource::synchronize(Region panel) {
    Region captured;
    int width = 0, height = 0;
    bool changed = false;
    bool monitorChanged = false;
    {
        std::lock_guard lock(m_impl->mutex);
        changed = m_impl->panel != panel;
        if (changed && panel.width > 0 && panel.height > 0) {
            const RECT bounds{panel.x, panel.y, panel.x + panel.width, panel.y + panel.height};
            const auto monitor = uintptr_t(MonitorFromRect(&bounds, MONITOR_DEFAULTTONEAREST));
            monitorChanged = m_impl->monitor != 0 && m_impl->monitor != monitor;
            m_impl->monitor = monitor;
        }
        m_impl->panel = panel;
        captured = m_impl->captured;
        width = m_impl->renderedWidth;
        height = m_impl->renderedHeight;
    }
    if (changed) {
        if (monitorChanged) {
            if (auto wake = m_impl->wake.lock()) {
                ++wake->recoveryRevision;
            }
        }
        m_impl->opticalDirty.store(true);
        if (!contains(captured, panel)) {
            m_impl->dirty.store(true);
        }
        m_impl->changed();
    }
    if (!m_impl->ready.load() || width < 1 || height < 1) {
        return false;
    }
    const float scaleX = float(panel.width) / width, scaleY = float(panel.height) / height;
    if (m_impl->appliedScaleX != scaleX || m_impl->appliedScaleY != scaleY) {
        m_impl->brush.Scale({scaleX, scaleY});
        m_impl->appliedScaleX = scaleX;
        m_impl->appliedScaleY = scaleY;
    }
    return true;
}

void CaptureSource::setActive(bool active) {
    if (m_impl->active.exchange(active) != active) {
        if (active) {
            if (auto wake = m_impl->wake.lock()) {
                ++wake->recoveryRevision;
            }
        }
        m_impl->dirty.store(true);
        m_impl->changed();
    }
}

void* CaptureSource::brush() const {
    return m_impl->ready.load() ? winrt::get_abi(m_impl->brush) : nullptr;
}

std::string CaptureSource::error() const {
    std::lock_guard lock(m_impl->mutex);
    return m_impl->error;
}

void CaptureSource::sampleStatistics(uint32_t attemptLimit) {
    std::lock_guard lock(m_impl->mutex);
    m_impl->statistics = {};
    m_impl->statisticsRemaining = std::min(attemptLimit, 1024u);
    ++m_impl->statisticsRevision;
}

CaptureStatistics CaptureSource::statistics() const {
    std::lock_guard lock(m_impl->mutex);
    auto result = m_impl->statistics;
    result.retainedRegionBytes = m_impl->allocatedBytes;
    if (const auto receiver = m_impl->wake.lock()) {
        result.sharedFramePoolBytes = receiver->framePoolBytes.load();
    }
    return result;
}

CaptureCoordinator::CaptureCoordinator(void* compositor, std::function<void()> beforeCompose)
    : m_impl(std::make_unique<Impl>(compositor, std::move(beforeCompose))) {}

CaptureCoordinator::~CaptureCoordinator() = default;

std::shared_ptr<CaptureSource> CaptureCoordinator::subscribe(std::function<void()> ready) {
    auto source = std::make_shared<CaptureSource::Impl>();
    source->wake = m_impl->wake;
    source->notify = std::move(ready);
    {
        std::lock_guard lock(m_impl->recordsMutex);
        m_impl->records.push_back(source);
    }
    ++m_impl->wake->recoveryRevision;
    source->changed();
    return std::shared_ptr<CaptureSource>(new CaptureSource(std::move(source)));
}

void CaptureCoordinator::exclude(std::vector<uintptr_t> windows) {
    std::sort(windows.begin(), windows.end());
    windows.erase(std::unique(windows.begin(), windows.end()), windows.end());
    {
        std::lock_guard lock(m_impl->recordsMutex);
        if (m_impl->exclusions == windows) {
            return;
        }
        m_impl->exclusions = std::move(windows);
    }
    invalidate();
}

void CaptureCoordinator::invalidate() {
    {
        std::lock_guard lock(m_impl->recordsMutex);
        ++m_impl->exclusionRevision;
    }
    for (const auto& source : m_impl->sources()) {
        source->dirty.store(true);
    }
    m_impl->wake->post();
}

void CaptureCoordinator::reset() {
    m_impl->wake->resetRequested.store(true);
    ++m_impl->wake->recoveryRevision;
    m_impl->wake->post();
}

void CaptureCoordinator::setSuspended(bool suspended) {
    if (m_impl->wake->suspended.exchange(suspended) != suspended) {
        if (!suspended) {
            ++m_impl->wake->recoveryRevision;
        }
        m_impl->wake->post();
    }
}
}  // namespace liquidglass
