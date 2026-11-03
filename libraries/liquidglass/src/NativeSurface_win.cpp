#include <Windows.h>
#include <dwmapi.h>
#include <sddl.h>
#include <winrt/base.h>

#include <atomic>
#include <cmath>
#include <memory>
#include <stdexcept>

#include "LiquidGlass/NativeService.h"
#include "LiquidGlass/NativeSurface.h"
#include "libraries/liquidglass/src/NativeSurfaceProtocol_win.h"

namespace liquidglass {
namespace {
std::atomic_uint32_t nextCookie{1};

struct MappingHandle {
    void operator()(void* handle) const {
        CloseHandle(handle);
    }
};

struct MappingView {
    void operator()(native::SurfaceRecord* record) const {
        UnmapViewOfFile(record);
    }
};

struct LocalMemory {
    void operator()(void* memory) const {
        LocalFree(memory);
    }
};

struct RegionCloser {
    void operator()(void* region) const {
        DeleteObject(region);
    }
};

void applyRegistration(HWND window, uint64_t revision) {
    // Changing this native attribute invalidates client-blur instructions without
    // toggling visibility/blur. Registered backdrop popups never maximize.
    RECT bounds{};
    winrt::check_bool(GetClientRect(window, &bounds));
    const std::unique_ptr<void, RegionCloser> region(
        CreateRectRgn(bounds.left, bounds.top, bounds.right, bounds.bottom));
    winrt::check_bool(region != nullptr);
    const DWM_BLURBEHIND blur{DWM_BB_ENABLE | DWM_BB_BLURREGION | DWM_BB_TRANSITIONONMAXIMIZED, TRUE,
                              static_cast<HRGN>(region.get()), revision & 1u ? TRUE : FALSE};
    winrt::check_hresult(DwmEnableBlurBehindWindow(window, &blur));
}
}  // namespace

struct NativeSurface::Impl {
    std::unique_ptr<void, MappingHandle> mapping;
    std::unique_ptr<native::SurfaceRecord, MappingView> view;
    native::SurfaceDescription description;
};

NativeSurface::NativeSurface() : m_impl(std::make_unique<Impl>()) {}

NativeSurface::~NativeSurface() {
    detach();
}

void NativeSurface::attach(uintptr_t window) {
    DWORD process = 0;
    const auto thread = GetWindowThreadProcessId(reinterpret_cast<HWND>(window), &process);
    if (!thread || process != GetCurrentProcessId() || thread != GetCurrentThreadId() ||
        !(GetWindowLongPtrW(reinterpret_cast<HWND>(window), GWL_STYLE) & WS_POPUP)) {
        throw std::invalid_argument("Native composition can register only the caller's own window on its UI thread");
    }
    detach();
    auto candidate = std::make_unique<Impl>();
    auto& description = candidate->description;
    description.window = window;
    description.process = process;
    description.cookie = nextCookie.fetch_add(1, std::memory_order_relaxed);
    if (!description.cookie) {
        description.cookie = nextCookie.fetch_add(1, std::memory_order_relaxed);
    }
    description.revision = 1;
    description.optics = native::encodeParameters(Parameters{});
    const auto name = native::surfaceMappingName(process, window, description.cookie);
    PSECURITY_DESCRIPTOR rawSecurity = nullptr;
    winrt::check_bool(ConvertStringSecurityDescriptorToSecurityDescriptorW(
        L"D:P(A;;GA;;;SY)(A;;GA;;;OW)(A;;GRGW;;;S-1-5-90-0)", SDDL_REVISION_1, &rawSecurity, nullptr));
    const std::unique_ptr<void, LocalMemory> security(rawSecurity);
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), security.get(), FALSE};
    candidate->mapping.reset(CreateFileMappingW(INVALID_HANDLE_VALUE, &attributes, PAGE_READWRITE, 0,
                                                sizeof(native::SurfaceRecord), name.c_str()));
    winrt::check_bool(candidate->mapping != nullptr);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        throw std::runtime_error("Native composition surface mapping already exists");
    }
    candidate->view.reset(static_cast<native::SurfaceRecord*>(
        MapViewOfFile(candidate->mapping.get(), FILE_MAP_ALL_ACCESS, 0, 0, sizeof(native::SurfaceRecord))));
    winrt::check_bool(candidate->view != nullptr);
    std::construct_at(candidate->view.get());
    native::writeSurface(*candidate->view, description);
    const auto handle = reinterpret_cast<HWND>(window);
    if (GetPropW(handle, native::kSurfaceProperty)) {
        throw std::runtime_error("Native composition surface already has an owner");
    }
    winrt::check_bool(
        SetPropW(handle, native::kSurfaceProperty, reinterpret_cast<HANDLE>(uintptr_t(description.cookie))));
    m_impl = std::move(candidate);
    try {
        applyRegistration(handle, description.revision);
    } catch (...) {
        detach();
        throw;
    }
}

void NativeSurface::update(Parameters parameters, float dpiScale, float corner, bool attachedTop,
                           NativeMaterial material, AeroParameters aero) {
    auto& value = *m_impl;
    if (!value.view || !std::isfinite(dpiScale) || dpiScale < .25f || dpiScale > 8.f || !std::isfinite(corner) ||
        corner < 0 || corner > 16384) {
        throw std::invalid_argument("Native composition surface or optical geometry is invalid");
    }
    parameters.normalize();
    auto next = value.description;
    next.optics = native::encodeParameters(parameters);
    next.dpiScale = dpiScale;
    next.corner = corner;
    next.attachedTop = attachedTop ? 1u : 0u;
    next.material = uint32_t(material);
    next.aero = aero;
    if (!native::validSurface(next, next.process, uintptr_t(next.window), next.cookie)) {
        throw std::invalid_argument("Native composition material parameters are invalid");
    }
    if (parameters == native::decodeParameters(value.description.optics) &&
        next.dpiScale == value.description.dpiScale && next.corner == value.description.corner &&
        next.attachedTop == value.description.attachedTop && next.material == value.description.material &&
        next.aero == value.description.aero) {
        return;
    }
    ++next.revision;
    if (!next.revision) {
        ++next.revision;
    }
    native::writeSurface(*value.view, next);
    applyRegistration(reinterpret_cast<HWND>(next.window), next.revision);
    value.description = next;
}

void NativeSurface::detach() {
    auto& value = *m_impl;
    const auto window = reinterpret_cast<HWND>(value.description.window);
    if (value.view && IsWindow(window) &&
        reinterpret_cast<uintptr_t>(GetPropW(window, native::kSurfaceProperty)) == value.description.cookie) {
        RemovePropW(window, native::kSurfaceProperty);
        const DWM_BLURBEHIND blur{DWM_BB_ENABLE, FALSE, nullptr, FALSE};
        DwmEnableBlurBehindWindow(window, &blur);
    }
    value.view.reset();
    value.mapping.reset();
    value.description = {};
}

void NativeSurface::refresh() {
    auto& value = *m_impl;
    if (!value.view ||
        GetWindowThreadProcessId(reinterpret_cast<HWND>(value.description.window), nullptr) != GetCurrentThreadId()) {
        throw std::invalid_argument("Native surface refresh must run on its window thread");
    }
    auto next = value.description;
    if (!++next.revision) {
        ++next.revision;
    }
    native::writeSurface(*value.view, next);
    applyRegistration(reinterpret_cast<HWND>(next.window), next.revision);
    value.description = next;
}

bool NativeSurface::active() const {
    return m_impl->view && std::atomic_ref<const uint32_t>(m_impl->view->active).load(std::memory_order_acquire) == 1;
}

uint32_t NativeSurface::error() const {
    return m_impl->view ? std::atomic_ref<const uint32_t>(m_impl->view->error).load(std::memory_order_acquire)
                        : ERROR_NOT_READY;
}

uint32_t NativeSurface::cookie() const {
    return m_impl->description.cookie;
}

uint64_t NativeSurface::revision() const {
    return m_impl->description.revision;
}

uint64_t NativeSurface::appliedRevision() const {
    return m_impl->view ? std::atomic_ref<const uint64_t>(m_impl->view->nativeRevision).load(std::memory_order_acquire)
                        : 0;
}

NativeMaterial NativeSurface::appliedMaterial() const {
    return m_impl->view
               ? NativeMaterial(
                     std::atomic_ref<const uint32_t>(m_impl->view->nativeMaterial).load(std::memory_order_acquire))
               : NativeMaterial::LiquidGlass;
}

NativeBackendIdentity NativeSurface::identity() const {
    if (!m_impl->view) {
        return {};
    }
    return {std::atomic_ref<const uint32_t>(m_impl->view->nativeProcess).load(std::memory_order_acquire),
            std::atomic_ref<const uint64_t>(m_impl->view->nativeGeneration).load(std::memory_order_acquire)};
}

uint32_t NativeSurface::notificationMessage() {
    return native::surfaceNotificationMessage();
}

}  // namespace liquidglass
