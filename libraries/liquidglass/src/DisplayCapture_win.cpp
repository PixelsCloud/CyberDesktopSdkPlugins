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
#include "libraries/liquidglass/src/CaptureSession_win.h"
#include "libraries/liquidglass/src/GpuTimer_win.h"
#include "libraries/liquidglass/src/PixelComparison_win.h"
#include "libraries/liquidglass/src/WindowStack_win.h"

namespace liquidglass {
namespace {
namespace capture = winrt::Windows::Graphics::Capture;
namespace composition = winrt::Windows::UI::Composition;
using Microsoft::WRL::ComPtr;
constexpr uint64_t kRegionPixels = 8 * 1024 * 1024;
constexpr uint64_t kFramePoolBytes = 256 * 1024 * 1024;

using detail::PixelComparison;

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
    bool framePending = false;
    std::atomic_uint64_t framePoolBytes{0};
    std::atomic_uint64_t recoveryRevision{0};
    std::atomic_bool resetRequested{false};
    std::atomic_bool suspended{false};

    void post(bool frame = false) {
        {
            std::lock_guard lock(mutex);
            if (frame) {
                framePending = true;
            } else {
                pending = true;
            }
        }
        condition.notify_one();
    }
};

// Shared modern display session or a bounded, event-driven window stack.
class Output final {
public:
    Output(uintptr_t monitor, const winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice& device,
           std::weak_ptr<Wake> wake) {
        MONITORINFO information{sizeof(information)};
        winrt::check_bool(GetMonitorInfoW(reinterpret_cast<HMONITOR>(monitor), &information));
        const auto& area = information.rcMonitor;
        bounds = {area.left, area.top, area.right - area.left, area.bottom - area.top};
        const auto notify = [wake] {
            if (const auto receiver = wake.lock()) {
                receiver->post(true);
            }
        };
        {
            detail::CaptureSession probe(winrt::get_abi(device), monitor, true, notify, true);
            m_capabilities = probe.capabilities();
        }
        if (!m_capabilities.exclusion || GetEnvironmentVariableW(L"CYBER_LIQUID_FORCE_WINDOW_STACK", nullptr, 0)) {
            m_session.reset();
            m_stack = std::make_unique<detail::WindowStack>(winrt::get_abi(device), bounds, notify);
        } else {
            m_session = std::make_unique<detail::CaptureSession>(winrt::get_abi(device), monitor, true, notify);
            bytes = m_session->bytes();
        }
    }

    void exclude(const std::vector<uintptr_t>& windows, uint64_t revision) {
        if (m_exclusions != windows || m_revision != revision) {
            m_exclusions = windows;
            m_revision = revision;
        }
        if (m_session) {
            m_session->start(windows);
        }
    }

    void configureRegions(std::vector<Region> areas) {
        m_regions = std::move(areas);
    }

    bool take(ID2D1DeviceContext& context, ID3D11Device& device, bool consumeFrame, uint64_t budget) {
        if (m_stack) {
            m_stack->configure(m_regions, m_exclusions);
            const bool fresh = m_stack->take(context, device, consumeFrame, budget);
            bytes = m_stack->bytes();
            regions = fresh ? m_regions : std::vector<Region>{};
            return fresh;
        }
        const bool fresh = consumeFrame && m_session->take(context, device);
        regions.clear();
        if (fresh) {
            for (auto area : m_session->changes()) {
                area.x += bounds.x;
                area.y += bounds.y;
                regions.push_back(area);
            }
        }
        return fresh;
    }

    ID2D1Bitmap1* bitmap() const {
        return m_stack ? m_stack->bitmap() : m_session->bitmap();
    }

    ID3D11ShaderResourceView& view() const {
        return *(m_stack ? m_stack->view() : m_session->view());
    }

    ID3D11Texture2D& texture() const {
        return *(m_stack ? m_stack->texture() : m_session->texture());
    }

    const char* backend() const {
        return m_stack ? m_stack->backend() : "windows-display-wgc-session-exclusion";
    }

    std::string error() const {
        return m_stack ? m_stack->error() : std::string{};
    }

    CaptureCapabilities capabilities() const {
        auto value = m_session ? m_session->capabilities() : m_capabilities;
        value.publishedGpu = m_stack && m_stack->usesPublishedFrames();
        return value;
    }

    Region bounds;
    uint64_t bytes = 0;
    std::vector<Region> regions;

private:
    std::unique_ptr<detail::CaptureSession> m_session;
    std::unique_ptr<detail::WindowStack> m_stack;
    std::vector<uintptr_t> m_exclusions;
    std::vector<Region> m_regions;
    uint64_t m_revision = 0;
    CaptureCapabilities m_capabilities;
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
    std::string backend;
    CaptureCapabilities capabilities;
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
                began = std::chrono::steady_clock::now();
            }
        }

        ~Measurement() {
            if (!enabled) {
                return;
            }
            std::lock_guard lock(source.mutex);
            if (revision == source.statisticsRevision) {
                ++source.statistics.captures;
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
            source.backend = output.backend();
            source.capabilities = output.capabilities();
        }
        const bool wasReady = source.ready.exchange(true);
        if (source.active.load() &&
            (!wasReady || resized || shadowChanged || previousWidth != panel.width || previousHeight != panel.height) &&
            source.notify) {
            source.notify();
        }
        return copiedBytes;
    }

    bool update(const std::vector<std::shared_ptr<CaptureSource::Impl>>& consumers, bool consumeFrame) {
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
        uint64_t retainedBytes = 0;
        for (const auto& [monitor, output] : outputs) {
            if (output) {
                retainedBytes += output->bytes;
            }
        }
        for (auto monitor : needed) {
            auto& output = outputs[monitor];
            if (!output) {
                output = std::make_unique<Output>(monitor, native, wake);
            }
            output->exclude(excluded, revision);
            std::vector<Region> areas;
            for (const auto& source : visible) {
                std::lock_guard lock(source->mutex);
                if (contains(output->bounds, source->panel)) {
                    areas.push_back(clipped(padded(source->panel, source->halo), output->bounds));
                }
            }
            output->configureRegions(std::move(areas));
            const auto otherBytes = retainedBytes - std::min(retainedBytes, output->bytes);
            const bool fresh = output->take(*d2d.Get(), *device.Get(), consumeFrame,
                                            otherBytes < kFramePoolBytes ? kFramePoolBytes - otherBytes : 0);
            retainedBytes = otherBytes + output->bytes;
            outputBytes += output->bytes;
            if (outputBytes > kFramePoolBytes) {
                throw std::runtime_error("Shared background capture exceeds the 256 MiB GPU budget");
            }
            if (!output->bitmap()) {
                const auto error = output->error();
                if (!error.empty()) {
                    for (const auto& source : visible) {
                        Region panel;
                        {
                            std::lock_guard lock(source->mutex);
                            panel = source->panel;
                        }
                        if (contains(output->bounds, panel)) {
                            {
                                std::lock_guard lock(source->mutex);
                                source->capabilities = output->capabilities();
                            }
                            release(*source);
                            fail(*source, error);
                        }
                    }
                }
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
                    }
                    sample.changed = dirty || changed;
                    const bool canCompare = !dirty && source->cachedView && previous == region;
                    if (sample.changed && canCompare) {
                        sample.compared = true;
                        const Region relative{region.x - output->bounds.x, region.y - output->bounds.y, region.width,
                                              region.height};
                        const auto began =
                            sample.enabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
                        sample.changed = comparison->probe(output->view(), source->cachedView.Get(), relative,
                                                           sample.enabled ? source->timer.get() : nullptr,
                                                           sample.enabled ? &sample.gpu : nullptr);
                        if (sample.enabled) {
                            sample.compareWall = uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
                                                              std::chrono::steady_clock::now() - began)
                                                              .count());
                        }
                    }
                    const auto drawBegan =
                        sample.enabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
                    if (sample.enabled && (sample.changed || opticalDirty)) {
                        source->timer->begin();
                    }
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
                        if (sample.changed) {
                            const auto drawing = source->timer->end();
                            sample.gpu = {sample.gpu.nanoseconds + drawing.nanoseconds,
                                          drawing.valid && (!sample.compared || sample.gpu.valid),
                                          sample.gpu.disjoint || drawing.disjoint,
                                          sample.gpu.timedOut || drawing.timedOut};
                        }
                    }
                    sample.succeeded = true;
                }
            }
        }
        wake->framePoolBytes.store(outputBytes);
        return !visible.empty() && std::all_of(visible.begin(), visible.end(), [](const auto& source) {
            std::lock_guard lock(source->mutex);
            return source->ready.load() || !source->error.empty();
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
        auto nextFrame = Clock::time_point::min();
        while (!stop.stop_requested()) {
            bool consumeFrame = false;
            {
                std::unique_lock lock(wake->mutex);
                auto deadline = retryAt ? retryAt : startupBy;
                if (wake->framePending && (!deadline || nextFrame < *deadline)) {
                    deadline = nextFrame;
                }
                const auto pending = [&] {
                    return wake->pending || (wake->framePending && Clock::now() >= nextFrame);
                };
                if (deadline) {
                    wake->condition.wait_until(lock, stop, *deadline, pending);
                } else {
                    wake->condition.wait(lock, stop, [&] {
                        return wake->pending || wake->framePending;
                    });
                }
                wake->pending = false;
                if (wake->framePending && Clock::now() >= nextFrame) {
                    consumeFrame = true;
                    wake->framePending = false;
                    nextFrame = Clock::now() + std::chrono::milliseconds(125);
                }
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
                if (update(active, consumeFrame)) {
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

std::string CaptureSource::backend() const {
    std::lock_guard lock(m_impl->mutex);
    return m_impl->backend;
}

CaptureCapabilities CaptureSource::capabilities() const {
    std::lock_guard lock(m_impl->mutex);
    return m_impl->capabilities;
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
