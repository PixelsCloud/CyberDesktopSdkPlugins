#include "libraries/liquidglass/src/WindowStack_win.h"

#include <Windows.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <winrt/base.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <exception>
#include <future>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <utility>

#include "libraries/liquidglass/src/CaptureSession_win.h"
#include "libraries/liquidglass/src/FrameConsumer_win.h"

namespace liquidglass::detail {
namespace {
using Microsoft::WRL::ComPtr;

struct GdiRegion final {
    explicit GdiRegion(HRGN value) : handle(value) {
        winrt::check_bool(handle != nullptr);
    }

    ~GdiRegion() {
        DeleteObject(handle);
    }

    HRGN handle;
};

bool intersects(Region first, Region second) {
    return first.x < second.x + second.width && first.y < second.y + second.height &&
           first.x + first.width > second.x && first.y + first.height > second.y;
}

Region windowBounds(HWND window) {
    RECT bounds{};
    if (FAILED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds, sizeof(bounds)))) {
        GetWindowRect(window, &bounds);
    }
    return {bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top};
}

struct Events {
    std::mutex mutex;
    std::vector<Region> regions;
    std::vector<uintptr_t> excluded;
    std::vector<uintptr_t> sources;
    std::function<void()> wake;
    std::atomic_bool changed{true};
};

thread_local std::weak_ptr<Events> eventState;

void CALLBACK windowEvent(HWINEVENTHOOK, DWORD event, HWND window, LONG object, LONG child, DWORD, DWORD) noexcept try {
    if (!window || (object != OBJID_WINDOW && event != EVENT_SYSTEM_FOREGROUND) || child != CHILDID_SELF) {
        return;
    }
    if (event != EVENT_SYSTEM_FOREGROUND && event != EVENT_OBJECT_CREATE && event != EVENT_OBJECT_DESTROY &&
        event != EVENT_OBJECT_SHOW && event != EVENT_OBJECT_HIDE && event != EVENT_OBJECT_REORDER &&
        event != EVENT_OBJECT_LOCATIONCHANGE && event != EVENT_OBJECT_STATECHANGE) {
        return;
    }
    if (GetWindowLongPtrW(window, GWL_STYLE) & WS_CHILD) {
        return;
    }
    if (const auto state = eventState.lock()) {
        bool relevant = false;
        {
            std::lock_guard lock(state->mutex);
            if (std::find(state->excluded.begin(), state->excluded.end(), uintptr_t(window)) != state->excluded.end()) {
                return;
            }
            const auto bounds = windowBounds(window);
            const bool selected =
                std::find(state->sources.begin(), state->sources.end(), uintptr_t(window)) != state->sources.end();
            relevant = selected || std::any_of(state->regions.begin(), state->regions.end(), [bounds](Region roi) {
                           return intersects(bounds, roi);
                       });
        }
        if (relevant && !state->changed.exchange(true) && state->wake) {
            state->wake();
        }
    }
} catch (...) {
    // No C++ exception may unwind through the WinEvent C callback boundary.
}

class WindowEvents final {
public:
    explicit WindowEvents(std::shared_ptr<Events> state) {
        std::promise<DWORD> initialized;
        auto ready = initialized.get_future();
        m_thread = std::jthread([state = std::move(state), initialized = std::move(initialized)]() mutable {
            const auto previousDpi = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            eventState = state;
            MSG message{};
            PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
            const auto foreground = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
                                                    windowEvent, 0, 0, WINEVENT_OUTOFCONTEXT);
            const auto objects = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_LOCATIONCHANGE, nullptr, windowEvent,
                                                 0, 0, WINEVENT_OUTOFCONTEXT);
            initialized.set_value(foreground && objects ? GetCurrentThreadId() : 0);
            if (foreground && objects) {
                while (GetMessageW(&message, nullptr, 0, 0) > 0) {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
            }
            if (foreground) {
                UnhookWinEvent(foreground);
            }
            if (objects) {
                UnhookWinEvent(objects);
            }
            eventState.reset();
            if (previousDpi) {
                SetThreadDpiAwarenessContext(previousDpi);
            }
        });
        m_id = ready.get();
        if (!m_id) {
            throw std::runtime_error("Window-stack event subscription unavailable");
        }
    }

    ~WindowEvents() {
        if (m_id) {
            PostThreadMessageW(m_id, WM_QUIT, 0, 0);
        }
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

private:
    std::jthread m_thread;
    DWORD m_id = 0;
};

struct Candidate {
    uintptr_t window = 0;
    Region bounds;
    float opacity = 1;
    bool operator==(const Candidate&) const = default;
};

struct Enumeration {
    const std::vector<uintptr_t>& excluded;
    GdiRegion remaining{CreateRectRgn(0, 0, 0, 0)};
    std::vector<Candidate> windows;
    bool exhausted = false;
    std::exception_ptr failure;
};

BOOL CALLBACK enumerateWindow(HWND window, LPARAM parameter) noexcept try {
    auto& state = *reinterpret_cast<Enumeration*>(parameter);
    if (!IsWindowVisible(window) || IsIconic(window) ||
        std::find(state.excluded.begin(), state.excluded.end(), uintptr_t(window)) != state.excluded.end()) {
        return TRUE;
    }
    DWORD cloaked = 0;
    DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    if (cloaked) {
        return TRUE;
    }
    const auto bounds = windowBounds(window);
    if (bounds.width < 1 || bounds.height < 1) {
        return TRUE;
    }
    RECT rect{bounds.x, bounds.y, bounds.x + bounds.width, bounds.y + bounds.height};
    if (!RectInRegion(state.remaining.handle, &rect)) {
        return TRUE;
    }
    COLORREF key = 0;
    BYTE alpha = 255;
    DWORD flags = 0;
    const bool layered = (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_LAYERED) != 0;
    const bool attributes = layered && GetLayeredWindowAttributes(window, &key, &alpha, &flags);
    if (attributes && (flags & LWA_ALPHA) && !alpha) {
        return TRUE;
    }
    state.windows.push_back({uintptr_t(window), bounds, attributes && (flags & LWA_ALPHA) ? alpha / 255.f : 1.f});
    // Only remove the guaranteed opaque client interior. Keep underlying
    // windows for non-client corners, shadows and per-pixel layered content.
    if (!layered) {
        RECT client{};
        if (GetClientRect(window, &client)) {
            POINT origin{};
            ClientToScreen(window, &origin);
            GdiRegion opaque(CreateRectRgn(origin.x, origin.y, origin.x + client.right, origin.y + client.bottom));
            if (CombineRgn(state.remaining.handle, state.remaining.handle, opaque.handle, RGN_DIFF) == NULLREGION) {
                state.exhausted = true;
                return FALSE;
            }
        }
    }
    return state.windows.size() <= 12;
} catch (...) {
    reinterpret_cast<Enumeration*>(parameter)->failure = std::current_exception();
    return FALSE;
}
}  // namespace

struct WindowStack::Impl {
    ComPtr<IUnknown> native;
    Region bounds;
    std::shared_ptr<Events> events = std::make_shared<Events>();
    std::unique_ptr<WindowEvents> hooks;
    std::vector<Region> regions;
    std::vector<uintptr_t> excluded;
    std::vector<Candidate> order;

    struct Entry {
        Region bounds;
        std::unique_ptr<CaptureSession> session;
        std::unique_ptr<FrameConsumer> published;
    };

    std::map<uintptr_t, Entry> sessions;
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> view;
    ComPtr<ID2D1Bitmap1> bitmap;
    uint64_t bytes = 0;
    uint64_t budget = 256ull * 1024 * 1024;
    bool dirty = true, ready = false, windowBlocked = false;
    std::string error;

    Impl(void* device, Region area, std::function<void()> wake) : bounds(area) {
        native = static_cast<IUnknown*>(device);
        events->wake = std::move(wake);
        hooks = std::make_unique<WindowEvents>(events);
    }

    void refresh() {
        Enumeration enumeration{excluded};
        for (const auto roi : regions) {
            GdiRegion region(CreateRectRgn(roi.x, roi.y, roi.x + roi.width, roi.y + roi.height));
            CombineRgn(enumeration.remaining.handle, enumeration.remaining.handle, region.handle, RGN_OR);
        }
        EnumWindows(enumerateWindow, reinterpret_cast<LPARAM>(&enumeration));
        if (enumeration.failure) {
            std::rethrow_exception(enumeration.failure);
        }
        if (!enumeration.exhausted) {
            throw std::runtime_error("Window stack cannot cover the requested background within its 12-window limit");
        }
        dirty |= order != enumeration.windows;
        order = std::move(enumeration.windows);
        {
            std::lock_guard lock(events->mutex);
            events->sources.clear();
            for (const auto& candidate : order) {
                events->sources.push_back(candidate.window);
            }
        }
        std::erase_if(sessions, [this](const auto& entry) {
            return std::none_of(order.begin(), order.end(), [&](const auto& candidate) {
                return candidate.window == entry.first;
            });
        });
        uint64_t planned = uint64_t(bounds.width) * bounds.height * 4;
        for (const auto& candidate : order) {
            auto& entry = sessions[candidate.window];
            if (!entry.published && !entry.session) {
                entry.published = FrameConsumer::find(candidate.window, bounds, events->wake);
            }
            const auto publishedArea = entry.published ? entry.published->bounds() : Region{};
            planned += entry.published ? uint64_t(publishedArea.width) * publishedArea.height * 8
                                       : uint64_t(candidate.bounds.width) * candidate.bounds.height * 8;
        }
        if (planned > budget) {
            throw std::runtime_error("Window-stack full frame pools exceed the 256 MiB shared GPU budget");
        }
        for (const auto& candidate : order) {
            auto& entry = sessions.at(candidate.window);
            if (entry.session &&
                (entry.bounds.width != candidate.bounds.width || entry.bounds.height != candidate.bounds.height)) {
                entry.session.reset();
            }
        }
        uint64_t reserved = uint64_t(bounds.width) * bounds.height * 4;
        for (const auto& [window, entry] : sessions) {
            if (entry.published) {
                const auto area = entry.published->bounds();
                reserved += uint64_t(area.width) * area.height * 8;
            } else if (entry.session) {
                reserved += entry.session->bytes();
            }
        }
        for (const auto& candidate : order) {
            auto& entry = sessions.at(candidate.window);
            if (entry.published) {
                entry.published->configure(regions);
                entry.bounds = entry.published->bounds();
            } else {
                if (!entry.session || entry.bounds.width != candidate.bounds.width ||
                    entry.bounds.height != candidate.bounds.height) {
                    entry.session.reset();
                    entry.session =
                        std::make_unique<CaptureSession>(native.Get(), candidate.window, false, events->wake, false,
                                                         reserved < budget ? budget - reserved : 0);
                    reserved += entry.session->bytes();
                    entry.session->start();
                }
                entry.bounds = candidate.bounds;
            }
        }
        error.clear();
        windowBlocked = false;
    }
};

WindowStack::WindowStack(void* native, Region bounds, std::function<void()> wake)
    : m_impl(std::make_unique<Impl>(native, bounds, std::move(wake))) {}

WindowStack::~WindowStack() = default;

void WindowStack::configure(const std::vector<Region>& requested, const std::vector<uintptr_t>& excluded) {
    auto& state = *m_impl;
    std::vector<Region> regions;
    GdiRegion coverage(CreateRectRgn(0, 0, 0, 0));
    for (const auto area : requested) {
        const int left = std::max(state.bounds.x, area.x);
        const int top = std::max(state.bounds.y, area.y);
        const int right = std::min(state.bounds.x + state.bounds.width, area.x + area.width);
        const int bottom = std::min(state.bounds.y + state.bounds.height, area.y + area.height);
        GdiRegion part(CreateRectRgn(left, top, right, bottom));
        CombineRgn(coverage.handle, coverage.handle, part.handle, RGN_OR);
    }
    const auto required = GetRegionData(coverage.handle, 0, nullptr);
    if (required < sizeof(RGNDATAHEADER) || required > 64 * 1024) {
        throw std::runtime_error("Window-stack region union exceeds its bounded layout budget");
    }
    std::vector<unsigned char> data(required);
    auto* description = reinterpret_cast<RGNDATA*>(data.data());
    winrt::check_bool(GetRegionData(coverage.handle, required, description) == required);
    const auto* rectangles = reinterpret_cast<const RECT*>(description->Buffer);
    for (DWORD index = 0; index < description->rdh.nCount; ++index) {
        const auto& area = rectangles[index];
        regions.push_back({area.left, area.top, area.right - area.left, area.bottom - area.top});
    }
    if (state.regions != regions || state.excluded != excluded) {
        state.regions = std::move(regions);
        state.excluded = excluded;
        state.events->changed.store(true);
        state.dirty = true;
        std::lock_guard lock(state.events->mutex);
        state.events->regions = state.regions;
        state.events->excluded = excluded;
    }
}

bool WindowStack::take(ID2D1DeviceContext& context, ID3D11Device& device, bool consumeFrame, uint64_t maximumBytes) {
    auto& state = *m_impl;
    state.budget = maximumBytes;
    for (const auto& [window, entry] : state.sessions) {
        const auto handle = reinterpret_cast<HWND>(window);
        if (!IsWindow(handle) || !IsWindowVisible(handle) || IsIconic(handle)) {
            state.events->changed.store(true);
        }
    }
    if (state.events->changed.exchange(false)) {
        try {
            state.refresh();
        } catch (const winrt::hresult_error& failure) {
            if (failure.code() != E_INVALIDARG && failure.code() != E_ACCESSDENIED) {
                throw;
            }
            state.sessions.clear();
            state.bitmap.Reset();
            state.view.Reset();
            state.texture.Reset();
            state.ready = false;
            state.bytes = 0;
            state.error = "This background window is not capturable on this Windows version: " +
                          winrt::to_string(failure.message());
            state.windowBlocked = true;
            return false;
        }
    }
    if (state.windowBlocked) {
        return false;
    }
    state.error.clear();
    state.bytes = state.texture ? uint64_t(state.bounds.width) * state.bounds.height * 4 : 0;
    bool fresh = std::exchange(state.dirty, false);
    bool complete = !state.sessions.empty();
    for (auto& [window, entry] : state.sessions) {
        if (entry.published) {
            fresh |= entry.published->take(device, context);
            complete &= entry.published->bitmap() != nullptr;
            state.bytes += entry.published->bytes();
            const auto reason = entry.published->error();
            if (!reason.empty()) {
                state.error = reason;
            }
        } else {
            if (consumeFrame) {
                try {
                    fresh |= entry.session->take(context, device);
                } catch (const CaptureSizeChanged&) {
                    entry.session.reset();
                    state.events->changed.store(true);
                    state.events->wake();
                    state.ready = false;
                    state.dirty = true;
                    return false;
                }
            }
            complete &= entry.session->bitmap() != nullptr;
            state.bytes += entry.session->bytes();
        }
    }
    if (state.bytes > state.budget) {
        throw std::runtime_error("Actual source textures exceed the shared 256 MiB GPU budget");
    }
    if (!complete) {
        state.dirty = true;
        state.ready = false;
        if (!state.error.empty() && state.texture) {
            state.bytes -= uint64_t(state.bounds.width) * state.bounds.height * 4;
            state.bitmap.Reset();
            state.view.Reset();
            state.texture.Reset();
        }
        return false;
    }
    if (!fresh) {
        return false;
    }
    if (!state.texture) {
        state.bytes += uint64_t(state.bounds.width) * state.bounds.height * 4;
        D3D11_TEXTURE2D_DESC description{};
        description.Width = UINT(state.bounds.width);
        description.Height = UINT(state.bounds.height);
        description.MipLevels = description.ArraySize = description.SampleDesc.Count = 1;
        description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        winrt::check_hresult(device.CreateTexture2D(&description, nullptr, &state.texture));
        winrt::check_hresult(device.CreateShaderResourceView(state.texture.Get(), nullptr, &state.view));
        ComPtr<IDXGISurface> surface;
        winrt::check_hresult(state.texture.As(&surface));
        const auto properties = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        winrt::check_hresult(context.CreateBitmapFromDxgiSurface(surface.Get(), &properties, &state.bitmap));
    }
    ComPtr<ID2D1Image> previous;
    context.GetTarget(&previous);
    context.SetTarget(state.bitmap.Get());
    context.SetDpi(96, 96);
    context.SetTransform(D2D1::Matrix3x2F::Identity());
    context.BeginDraw();
    for (const auto roi : state.regions) {
        const auto clip =
            D2D1::RectF(float(roi.x - state.bounds.x), float(roi.y - state.bounds.y),
                        float(roi.x + roi.width - state.bounds.x), float(roi.y + roi.height - state.bounds.y));
        context.PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);
        context.Clear(D2D1::ColorF(0, 0, 0, 1));
        for (auto it = state.order.rbegin(); it != state.order.rend(); ++it) {
            if (!intersects(it->bounds, roi)) {
                continue;
            }
            const auto& entry = state.sessions.at(it->window);
            const auto area = entry.published ? entry.published->bounds() : entry.bounds;
            auto* bitmap = entry.published ? entry.published->bitmap() : entry.session->bitmap();
            const auto size = bitmap->GetPixelSize();
            const auto target =
                D2D1::RectF(float(area.x - state.bounds.x), float(area.y - state.bounds.y),
                            float(area.x - state.bounds.x + size.width), float(area.y - state.bounds.y + size.height));
            context.DrawBitmap(bitmap, target, it->opacity, D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
        }
        context.PopAxisAlignedClip();
    }
    const auto result = context.EndDraw();
    context.SetTarget(previous.Get());
    winrt::check_hresult(result);
    state.ready = true;
    return true;
}

std::string WindowStack::error() const {
    return m_impl->error;
}

const char* WindowStack::backend() const {
    return usesPublishedFrames() ? "windows-window-stack-wgc-shared-gpu" : "windows-window-stack-wgc";
}

bool WindowStack::usesPublishedFrames() const {
    return std::any_of(m_impl->sessions.begin(), m_impl->sessions.end(), [](const auto& entry) {
        return bool(entry.second.published);
    });
}

uint64_t WindowStack::bytes() const {
    return m_impl->bytes;
}

ID2D1Bitmap1* WindowStack::bitmap() const {
    return m_impl->ready ? m_impl->bitmap.Get() : nullptr;
}

ID3D11Texture2D* WindowStack::texture() const {
    return m_impl->ready ? m_impl->texture.Get() : nullptr;
}

ID3D11ShaderResourceView* WindowStack::view() const {
    return m_impl->ready ? m_impl->view.Get() : nullptr;
}
}  // namespace liquidglass::detail
