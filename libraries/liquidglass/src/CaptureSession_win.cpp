#include "libraries/liquidglass/src/CaptureSession_win.h"

#include <Windows.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.UI.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace liquidglass::detail {
namespace {
namespace capture = winrt::Windows::Graphics::Capture;
using Microsoft::WRL::ComPtr;

struct Frame final {
    explicit Frame(capture::Direct3D11CaptureFrame value) : frame(std::move(value)) {}

    Frame(Frame&& other) noexcept : frame(std::exchange(other.frame, nullptr)) {}

    ~Frame() {
        try {
            if (frame) {
                frame.Close();
            }
        } catch (...) {
        }
    }

    capture::Direct3D11CaptureFrame frame{nullptr};
};

struct Mailbox {
    std::mutex mutex;
    std::optional<Frame> pending;
    std::string error;
    std::function<void()> wake;
    std::atomic_bool active{true};
};

template <typename Action>
bool optionalProperty(Action&& action) {
    try {
        action();
        return true;
    } catch (const winrt::hresult_error&) {
        return false;
    }
}
}  // namespace

struct CaptureSession::Impl {
    std::shared_ptr<Mailbox> mailbox = std::make_shared<Mailbox>();
    capture::GraphicsCaptureItem item{nullptr};
    capture::Direct3D11CaptureFramePool pool{nullptr};
    capture::GraphicsCaptureSession session{nullptr};
    capture::IDisplayGraphicsCaptureSession display{nullptr};
    capture::Direct3D11CaptureFramePool::FrameArrived_revoker arrived;
    capture::GraphicsCaptureItem::Closed_revoker closed;
    SessionCapabilities capabilities;
    Region extent;
    uint64_t iteration = 0;
    bool started = false, monitor = false;
    std::vector<uintptr_t> excluded;
    std::vector<Region> changes;
    std::optional<Frame> frame;

    struct Cached {
        ComPtr<IUnknown> identity;
        ComPtr<ID2D1Bitmap1> bitmap;
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11ShaderResourceView> view;
    };

    std::array<Cached, 2> cache;
    size_t cacheNext = 0, current = 0;

    Impl(void* device, uintptr_t target, bool isMonitor, std::function<void()> wake, bool capabilityProbe,
         uint64_t maximumPoolBytes)
        : monitor(isMonitor) {
        mailbox->wake = std::move(wake);
        winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice native{nullptr};
        winrt::copy_from_abi(native, device);
        const auto factory = winrt::get_activation_factory<capture::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        auto result =
            monitor ? factory->CreateForMonitor(reinterpret_cast<HMONITOR>(target),
                                                winrt::guid_of<capture::GraphicsCaptureItem>(), winrt::put_abi(item))
                    : factory->CreateForWindow(reinterpret_cast<HWND>(target),
                                               winrt::guid_of<capture::GraphicsCaptureItem>(), winrt::put_abi(item));
        // A newly shown ordinary window may not have reached WGC's registry
        // yet. Retry only that bounded initialization boundary;
        // never turn capability failure into a permanent polling loop.
        const auto window = reinterpret_cast<HWND>(target);
        if (!monitor && !(GetWindowLongPtrW(window, GWL_EXSTYLE) & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE))) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
            while (result == E_INVALIDARG && IsWindowVisible(window) && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
                result = factory->CreateForWindow(window, winrt::guid_of<capture::GraphicsCaptureItem>(),
                                                  winrt::put_abi(item));
            }
        }
        if (FAILED(result)) {
            RECT area{};
            DWORD cloaked = 0;
            wchar_t name[256]{};
            GetWindowRect(window, &area);
            GetClassNameW(window, name, 256);
            DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
            const auto message = std::wstring(L"WGC rejected ") + name + L" at " + std::to_wstring(area.left) + L"," +
                                 std::to_wstring(area.top) + L" " + std::to_wstring(area.right - area.left) + L"x" +
                                 std::to_wstring(area.bottom - area.top) + L" visible=" +
                                 std::to_wstring(IsWindowVisible(window)) + L" cloaked=" + std::to_wstring(cloaked);
            throw winrt::hresult_error(result, message);
        }
        const auto size = item.Size();
        if (size.Width < 1 || size.Height < 1 ||
            (!capabilityProbe && uint64_t(size.Width) * size.Height * 8 > maximumPoolBytes)) {
            throw std::runtime_error("Capture session exceeds the 256 MiB shared frame-pool budget");
        }
        extent = {0, 0, size.Width, size.Height};
        auto poolSize = size;
        if (capabilityProbe) {
            poolSize = {1, 1};
        }
        pool = capture::Direct3D11CaptureFramePool::CreateFreeThreaded(
            native, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, poolSize);
        session = pool.CreateCaptureSession(item);
        if (monitor) {
            display = session.try_as<capture::IDisplayGraphicsCaptureSession>();
        }
        capabilities.exclusion = bool(display);
        if (const auto cursor = session.try_as<capture::IGraphicsCaptureSession2>()) {
            capabilities.cursor = optionalProperty([&] {
                cursor.IsCursorCaptureEnabled(false);
            });
        }
        if (const auto border = session.try_as<capture::IGraphicsCaptureSession3>()) {
            capabilities.border = optionalProperty([&] {
                border.IsBorderRequired(false);
            });
        }
        if (const auto interval = session.try_as<capture::IGraphicsCaptureSession5>()) {
            capabilities.interval = optionalProperty([&] {
                interval.MinUpdateInterval(std::chrono::milliseconds(125));
            });
        }
        if (const auto dirty = session.try_as<capture::IGraphicsCaptureSession4>()) {
            capabilities.dirty = optionalProperty([&] {
                dirty.DirtyRegionMode(capture::GraphicsCaptureDirtyRegionMode::ReportOnly);
            });
        }
        const std::weak_ptr<Mailbox> weak = mailbox;
        arrived = pool.FrameArrived(winrt::auto_revoke, [weak](const auto& sender, const auto&) {
            const auto state = weak.lock();
            if (!state || !state->active.load()) {
                return;
            }
            try {
                auto next = sender.TryGetNextFrame();
                if (!next) {
                    return;
                }
                std::lock_guard lock(state->mutex);
                state->pending.emplace(std::move(next));
            } catch (const winrt::hresult_error& error) {
                std::lock_guard lock(state->mutex);
                state->error = winrt::to_string(error.message());
            }
            if (state->wake) {
                state->wake();
            }
        });
        closed = item.Closed(winrt::auto_revoke, [weak](const auto&, const auto&) {
            if (const auto state = weak.lock(); state && state->active.load()) {
                {
                    std::lock_guard lock(state->mutex);
                    state->error = "Captured window or display became unavailable";
                }
                if (state->wake) {
                    state->wake();
                }
            }
        });
    }

    ~Impl() {
        mailbox->active.store(false);
        arrived.revoke();
        closed.revoke();
        try {
            if (session) {
                session.Close();
            }
            if (pool) {
                pool.Close();
            }
        } catch (...) {
        }
    }
};

CaptureSession::CaptureSession(void* native, uintptr_t target, bool monitor, std::function<void()> wake,
                               bool capabilityProbe, uint64_t maximumPoolBytes)
    : m_impl(std::make_unique<Impl>(native, target, monitor, std::move(wake), capabilityProbe, maximumPoolBytes)) {}

CaptureSession::~CaptureSession() = default;

CaptureSizeChanged::CaptureSizeChanged()
    : std::runtime_error("Capture size changed; recreate only this source session") {}

void CaptureSession::start(const std::vector<uintptr_t>& exclusions) {
    auto& state = *m_impl;
    if (state.started && state.excluded == exclusions) {
        return;
    }
    if (state.display) {
        std::vector<winrt::Windows::UI::WindowId> windows;
        for (const auto window : exclusions) {
            if (IsWindow(reinterpret_cast<HWND>(window))) {
                windows.push_back({uint64_t(window)});
            }
        }
        state.iteration = state.display.SetWindowExclusionList(windows);
    } else if (state.monitor) {
        throw std::runtime_error("Display session has no per-session window exclusion; use window-stack capture");
    }
    state.excluded = exclusions;
    if (!state.started) {
        state.session.StartCapture();
        state.started = true;
    }
}

bool CaptureSession::take(ID2D1DeviceContext& context, ID3D11Device& device) {
    auto& state = *m_impl;
    std::optional<Frame> next;
    {
        std::lock_guard lock(state.mailbox->mutex);
        if (!state.mailbox->error.empty()) {
            throw std::runtime_error(state.mailbox->error);
        }
        if (state.mailbox->pending) {
            next.emplace(std::move(*state.mailbox->pending));
            state.mailbox->pending.reset();
        }
    }
    if (!next) {
        return false;
    }
    if (state.display) {
        const auto version = next->frame.try_as<capture::IDirect3D11CaptureFrame3>();
        state.capabilities.iteration = bool(version);
        if (!version) {
            throw std::runtime_error("Display exclusion requires frame configuration revision support");
        }
        if (version.ConfigurationIteration() < state.iteration) {
            return false;
        }
    }
    const auto size = next->frame.ContentSize();
    if (size.Width != state.extent.width || size.Height != state.extent.height) {
        throw CaptureSizeChanged();
    }
    ComPtr<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess> access;
    winrt::check_hresult(
        static_cast<IUnknown*>(winrt::get_abi(next->frame.Surface()))->QueryInterface(IID_PPV_ARGS(&access)));
    ComPtr<IDXGISurface> surface;
    winrt::check_hresult(access->GetInterface(IID_PPV_ARGS(&surface)));
    ComPtr<IUnknown> identity;
    winrt::check_hresult(surface.As(&identity));
    size_t index = 0;
    while (index < state.cache.size() && state.cache[index].identity.Get() != identity.Get()) {
        ++index;
    }
    if (index == state.cache.size()) {
        index = state.cacheNext++ % state.cache.size();
        auto& cached = state.cache[index];
        cached = {};
        cached.identity = std::move(identity);
        const auto properties = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_NONE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                              state.monitor ? D2D1_ALPHA_MODE_IGNORE : D2D1_ALPHA_MODE_PREMULTIPLIED));
        winrt::check_hresult(context.CreateBitmapFromDxgiSurface(surface.Get(), &properties, &cached.bitmap));
        winrt::check_hresult(surface.As(&cached.texture));
        winrt::check_hresult(device.CreateShaderResourceView(cached.texture.Get(), nullptr, &cached.view));
    }
    state.current = index;
    state.changes.clear();
    if (state.capabilities.dirty) {
        if (const auto dirty = next->frame.try_as<capture::IDirect3D11CaptureFrame2>()) {
            const auto regions = dirty.DirtyRegions();
            if (regions.Size() <= 1024) {
                for (const auto& area : regions) {
                    state.changes.push_back({area.X, area.Y, area.Width, area.Height});
                }
            } else {
                state.changes.push_back(state.extent);
            }
        } else {
            state.capabilities.dirty = false;
        }
    }
    if (!state.capabilities.dirty) {
        state.changes.push_back(state.extent);
    }
    state.frame.reset();
    state.frame.emplace(std::move(*next));
    return true;
}

SessionCapabilities CaptureSession::capabilities() const {
    return m_impl->capabilities;
}

Region CaptureSession::size() const {
    return m_impl->extent;
}

uint64_t CaptureSession::bytes() const {
    return uint64_t(m_impl->extent.width) * m_impl->extent.height * 8;
}

const std::vector<Region>& CaptureSession::changes() const {
    return m_impl->changes;
}

ID2D1Bitmap1* CaptureSession::bitmap() const {
    return m_impl->frame ? m_impl->cache[m_impl->current].bitmap.Get() : nullptr;
}

ID3D11Texture2D* CaptureSession::texture() const {
    return m_impl->frame ? m_impl->cache[m_impl->current].texture.Get() : nullptr;
}

ID3D11ShaderResourceView* CaptureSession::view() const {
    return m_impl->frame ? m_impl->cache[m_impl->current].view.Get() : nullptr;
}
}  // namespace liquidglass::detail
