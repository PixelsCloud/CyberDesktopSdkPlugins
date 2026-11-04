// SPDX-License-Identifier: GPL-3.0-only
#include "libraries/dwmglass/src/DwmSurfaceRegistry.h"

#include <KNSoft/NDK/NDK.h>
#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <exception>
#include <initializer_list>
#include <map>
#include <mutex>
#include <stdexcept>
#include <utility>

#include "LiquidGlass/CompositionMaterial.h"
#include "libraries/dwmglass/src/DwmDiagnosticPacket.h"
#include "libraries/dwmglass/src/DwmDiagnostics.h"
#include "libraries/dwmglass/src/DwmLifecycleLock.h"
#include "libraries/liquidglass/src/NativeSurfaceProtocol_win.h"
#include "third_party/OpenGlass/OpenGlass/Architecture/Legacy/uDwmProjection.hpp"
#include "third_party/OpenGlass/OpenGlass/Shared.hpp"

namespace cyberglass::dwm {
namespace {
using liquidglass::native::SurfaceDescription;
using liquidglass::native::SurfaceRecord;
using namespace OpenGlass;
constexpr size_t kSurfaceLimit = 128;
std::weak_ptr<SurfaceRegistry> hookRegistry;

struct MappingCloser {
    void operator()(void* handle) const {
        CloseHandle(handle);
    }
};

struct ViewCloser {
    void operator()(SurfaceRecord* record) const {
        UnmapViewOfFile(record);
    }
};

struct NativeVisual {
    std::weak_ptr<Surface> owner;
    winrt::com_ptr<uDWM::CSolidColorLegacyMilBrushProxy> brush;
    winrt::com_ptr<uDWM::CRenderDataVisual> canvas;
    winrt::com_ptr<uDWM::CRgnGeometryProxy> geometry;
    SIZE size{};
};

std::map<uintptr_t, NativeVisual> brushes;

void removeVisual(uDWM::CTopLevelWindow* window) {
    const auto found = brushes.find(uintptr_t(window));
    if (found != brushes.end()) {
        if (const auto nativeBlur = window->GetClientBlurVisual()) {
            nativeBlur->SetExcludeSubtree(false);
        }
        if (found->second.canvas) {
            LOG_IF_FAILED(window->GetVisualCollection()->Remove(found->second.canvas.get()));
        }
        brushes.erase(found);
    }
}

HRESULT updateClientBlur(uDWM::CTopLevelWindow* window);
void destroyWindow(uDWM::CTopLevelWindow* window);
Projection::Detour<uDWM::Symbol_CTopLevelWindow_UpdateClientBlur, &updateClientBlur> updateOriginal;
Projection::Detour<uDWM::Symbol_CTopLevelWindow__CTopLevelWindow, &destroyWindow> destroyOriginal;

HRESULT applyClientBlur(uDWM::CTopLevelWindow* window) {
    const HRESULT result = S_OK;
    const auto registry = hookRegistry.lock();
    const auto data = window->GetData();
    if (!registry || !data) {
        return result;
    }
    const auto surface = registry->track(uintptr_t(data->GetHwnd()));
    if (!surface) {
        removeVisual(window);
        return result;
    }
    if (const auto nativeBlur = window->GetClientBlurVisual()) {
        // DwmEnableBlurBehindWindow is the registration/invalidation signal.
        // Its own native fill must not cover or contaminate our optical plane.
        nativeBlur->SetExcludeSubtree(true);
        winrt::check_hresult(nativeBlur->RenderRecursive());
    }
    const auto size = window->GetSize();
    if (size.cx <= 0 || size.cy <= 0 || size.cx > 16384 || size.cy > 16384) {
        surface->report(ERROR_NOT_READY, false);
        return result;
    }
    auto& value = brushes[uintptr_t(window)];
    value.owner = surface;
    if (!value.brush) {
        winrt::check_hresult(uDWM::CDesktopManager::GetInstance()->GetCompositor()->CreateSolidColorLegacyMilBrushProxy(
            value.brush.put()));
    }
    if (!value.canvas) {
        winrt::check_hresult(uDWM::CRenderDataVisual::Create(value.canvas.put()));
        value.canvas->SetInsetFromParent({});
        winrt::check_hresult(window->GetVisualCollection()->InsertRelative(value.canvas.get(), nullptr, false, true));
    }
    if (!value.geometry || value.size.cx != size.cx || value.size.cy != size.cy) {
        wil::unique_hrgn region(CreateRectRgn(0, 0, size.cx, size.cy));
        winrt::check_bool(region != nullptr);
        value.geometry = nullptr;
        winrt::check_hresult(uDWM::ResourceHelper::CreateGeometryFromHRGN(region.get(), value.geometry.put()));
        value.canvas->SetSize(&size);
        value.size = size;
        winrt::com_ptr<uDWM::CDrawGeometryInstruction> instruction;
        winrt::check_hresult(
            uDWM::CDrawGeometryInstruction::Create(value.brush.get(), value.geometry.get(), instruction.put()));
        winrt::check_hresult(value.canvas->ClearInstructions());
        winrt::check_hresult(value.canvas->AddInstruction(instruction.get()));
    }
    const auto marker = encodeSurfaceMarker(surface->id(), surface->description().revision);
    auto color = window->GetCaptionColorizationParameters()->getArgbcolor();
    color.a = std::bit_cast<float>(marker);
    winrt::check_hresult(value.brush->Update(1.0, color));
    winrt::check_hresult(value.canvas->UpdateRenderData());
    value.canvas->SetExcludeSubtree(false);
    winrt::check_hresult(value.canvas->RenderRecursive());
    sample(Diagnostic::Marker);
    return result;
}

HRESULT updateClientBlur(uDWM::CTopLevelWindow* window) {
    sample(Diagnostic::ClientBlur);
    const auto nativeResult = updateOriginal(window);
    if (FAILED(nativeResult)) {
        return nativeResult;
    }
    try {
        applyClientBlur(window);
    } catch (const winrt::hresult_error& error) {
        sample(Diagnostic::Error, uint32_t(error.code()));
        try {
            removeVisual(window);
        } catch (...) {
        }
    } catch (...) {
        sample(Diagnostic::Error, uint32_t(E_FAIL));
        try {
            removeVisual(window);
        } catch (...) {
        }
    }
    // Custom material failures must not become compositor-wide failures.
    // Best-effort removal restores the native blur subtree after a partial update.
    // A successfully updated canvas remains registered in brushes.
    return nativeResult;
}

void destroyWindow(uDWM::CTopLevelWindow* window) {
    // The native destructor is the lifetime boundary for all retained proxies.
    if (const auto data = window->GetData()) {
        if (const auto registry = hookRegistry.lock()) {
            registry->retire(uintptr_t(data->GetHwnd()));
        }
    }
    removeVisual(window);
    destroyOriginal(window);
}
}  // namespace

struct Surface::Impl {
    std::unique_ptr<void, MappingCloser> mapping;
    std::unique_ptr<SurfaceRecord, ViewCloser> view;
    mutable std::mutex mutex;
    SurfaceDescription cached;
    uintptr_t window = 0;
    uint32_t process = 0, cookie = 0;
    uint16_t id = 0;
    uint64_t generation = 0;
    mutable std::atomic<uint64_t> reported{UINT64_MAX};
    mutable std::atomic<uint64_t> appliedRevision{0};
    bool valid = false;
};

Surface::Surface(uintptr_t window, uint32_t cookie, uint16_t id, uint64_t generation)
    : m_impl(std::make_unique<Impl>()) {
    auto& value = *m_impl;
    value.window = window;
    value.cookie = cookie;
    value.id = id;
    value.generation = generation;
    DWORD process = 0;
    winrt::check_bool(GetWindowThreadProcessId(reinterpret_cast<HWND>(window), &process) != 0);
    value.process = process;
    const auto name = liquidglass::native::surfaceMappingName(process, window, cookie);
    value.mapping.reset(OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name.c_str()));
    winrt::check_bool(value.mapping != nullptr);
    value.view.reset(static_cast<SurfaceRecord*>(
        MapViewOfFile(value.mapping.get(), FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(SurfaceRecord))));
    winrt::check_bool(value.view != nullptr);
    if (!reload()) {
        throw std::invalid_argument("Invalid native surface protocol");
    }
}

Surface::~Surface() {
    report(ERROR_NOT_READY, false);
}

bool Surface::reload() {
    auto& value = *m_impl;
    SurfaceDescription snapshot;
    if (!liquidglass::native::readSurface(*value.view, snapshot)) {
        std::lock_guard lock(value.mutex);
        return value.valid;
    }
    if (!liquidglass::native::validSurface(snapshot, value.process, value.window, value.cookie)) {
        report(ERROR_INVALID_DATA, false);
        return false;
    }
    std::lock_guard lock(value.mutex);
    value.cached = snapshot;
    value.valid = true;
    return true;
}

uint16_t Surface::id() const {
    return m_impl->id;
}

uint32_t Surface::cookie() const {
    return m_impl->cookie;
}

SurfaceDescription Surface::description() const {
    std::lock_guard lock(m_impl->mutex);
    return m_impl->cached;
}

void Surface::report(uint32_t error, bool active, uint64_t revision, uint32_t material) const {
    auto& value = *m_impl;
    if (!revision || material == UINT32_MAX) {
        const auto snapshot = description();
        revision = snapshot.revision;
        material = snapshot.material;
    }
    if (active && value.view && value.appliedRevision.exchange(revision, std::memory_order_relaxed) != revision) {
        std::atomic_ref<uint64_t>(value.view->nativeRevision).store(revision, std::memory_order_release);
    }
    const uint64_t status = (uint64_t(error) << 2u) | (uint64_t(material) << 1u) | (active ? 1u : 0u);
    if (value.reported.load(std::memory_order_acquire) == status) {
        return;
    }
    if (value.view && value.reported.exchange(status, std::memory_order_acq_rel) != status) {
        std::atomic_ref<uint32_t>(value.view->nativeProcess).store(GetCurrentProcessId(), std::memory_order_release);
        std::atomic_ref<uint64_t>(value.view->nativeGeneration).store(value.generation, std::memory_order_release);
        std::atomic_ref<uint32_t>(value.view->nativeMaterial).store(material, std::memory_order_release);
        std::atomic_ref<uint32_t>(value.view->error).store(error, std::memory_order_release);
        std::atomic_ref<uint32_t>(value.view->active).store(active ? 1u : 0u, std::memory_order_release);
        const auto window = reinterpret_cast<HWND>(value.window);
        if (reinterpret_cast<uintptr_t>(GetPropW(window, liquidglass::native::kSurfaceProperty)) == value.cookie) {
            PostMessageW(window, liquidglass::native::surfaceNotificationMessage(),
                         uintptr_t(liquidglass::native::SurfaceNotification::Frame), value.cookie);
        }
    }
}

struct SurfaceRegistry::Impl {
    mutable std::mutex mutex;
    std::map<uintptr_t, std::shared_ptr<Surface>> windows;
    std::array<std::shared_ptr<Surface>, kSurfaceLimit + 1> slots;
    std::atomic<float> expansion{0};
    std::atomic<uint64_t> retired{0};
    uint64_t generation = 0;

    void updateExpansion() {
        float maximum = 0;
        for (const auto& [window, surface] : windows) {
            const auto description = surface->description();
            const auto p = liquidglass::native::decodeParameters(description.optics);
            const float amount = description.material == uint32_t(liquidglass::NativeMaterial::AeroGlass)
                                     ? description.aero.blurSigma * 3
                                     : liquidglass::opticalExpansion(p, description.dpiScale) / description.dpiScale;
            maximum = std::max(maximum, std::max(1.f, std::ceil(amount * description.dpiScale)));
        }
        expansion.store(maximum, std::memory_order_release);
    }
};

SurfaceRegistry::SurfaceRegistry(uint64_t generation) : m_impl(std::make_unique<Impl>()) {
    m_impl->generation = generation;
}

SurfaceRegistry::~SurfaceRegistry() = default;

std::shared_ptr<Surface> SurfaceRegistry::track(uintptr_t window) {
    auto& value = *m_impl;
    const auto cookie = uint32_t(
        reinterpret_cast<uintptr_t>(GetPropW(reinterpret_cast<HWND>(window), liquidglass::native::kSurfaceProperty)));
    if (cookie) {
        sample(Diagnostic::Property);
    }
    std::lock_guard lock(value.mutex);
    auto existing = value.windows.find(window);
    if (!cookie && existing == value.windows.end()) {
        return {};
    }
    if (existing != value.windows.end() && existing->second->cookie() == cookie && cookie) {
        if (existing->second->reload()) {
            value.updateExpansion();
            return existing->second;
        }
    }
    if (existing != value.windows.end()) {
        value.slots[existing->second->id()].reset();
        value.windows.erase(existing);
        value.retired.fetch_add(1, std::memory_order_release);
    }
    if (!cookie) {
        value.updateExpansion();
        return {};
    }
    for (uint16_t slot = 1; slot <= kSurfaceLimit; ++slot) {
        if (!value.slots[slot]) {
            auto surface = std::make_shared<Surface>(window, cookie, slot, value.generation);
            value.slots[slot] = surface;
            value.windows.emplace(window, surface);
            value.updateExpansion();
            return surface;
        }
    }
    throw std::runtime_error("Native glass surface budget exhausted");
}

std::shared_ptr<Surface> SurfaceRegistry::find(uint16_t id) const {
    std::lock_guard lock(m_impl->mutex);
    return id > 0 && id <= kSurfaceLimit ? m_impl->slots[id] : nullptr;
}

void SurfaceRegistry::retire(uintptr_t window) {
    std::lock_guard lock(m_impl->mutex);
    const auto found = m_impl->windows.find(window);
    if (found != m_impl->windows.end()) {
        found->second->report(ERROR_NOT_READY, false);
        m_impl->slots[found->second->id()].reset();
        m_impl->windows.erase(found);
        m_impl->retired.fetch_add(1, std::memory_order_release);
        m_impl->updateExpansion();
    }
}

void SurfaceRegistry::clear() {
    std::lock_guard lock(m_impl->mutex);
    m_impl->windows.clear();
    m_impl->slots.fill(nullptr);
    m_impl->expansion.store(0, std::memory_order_release);
    m_impl->retired.fetch_add(1, std::memory_order_release);
}

float SurfaceRegistry::samplingExpansion() const {
    return m_impl->expansion.load(std::memory_order_acquire);
}

uint64_t SurfaceRegistry::retirementRevision() const {
    return m_impl->retired.load(std::memory_order_acquire);
}

bool SurfaceRegistry::empty() const {
    std::lock_guard lock(m_impl->mutex);
    return m_impl->windows.empty();
}

void installSurfaceHooks(const std::shared_ptr<SurfaceRegistry>& registry) {
    hookRegistry = registry;
    HookHelper::ApplyInlineHooks(std::initializer_list<HookHelper::DetourInfo>{{&updateOriginal}, {&destroyOriginal}});
}

void removeSurfaceHooks() {
    while (!brushes.empty()) {
        const auto window = reinterpret_cast<OpenGlass::uDWM::CTopLevelWindow*>(brushes.begin()->first);
        removeVisual(window);
        updateOriginal(window);
    }
    HookHelper::ApplyInlineHooks(std::initializer_list<HookHelper::DetourInfo>{{&destroyOriginal}, {&updateOriginal}});
    brushes.clear();
}

uint32_t encodeSurfaceMarker(uint16_t id, uint64_t revision) {
    // Only the marker nonce wraps; the shared description retains the full revision.
    return 0x3f000001u | (((uint32_t(id & 255u) << 3u) | ((uint32_t(revision) & 15u) << 11u) | 2u) << 8u);
}

uint16_t decodeSurfaceMarker(uint32_t marker) {
    return (marker & 0xff8000ffu) == 0x3f000001u ? uint16_t((marker >> 11u) & 255u) : 0;
}

void inspectSurface(std::span<uint64_t> values) {
    const CompositorAccess lock(OpenGlass::uDWM::CDesktopManager::s_csDwmInstance);
    if (!lock) {
        return;
    }
    values[0] = brushes.size();
    if (!brushes.empty()) {
        if (const auto visual = brushes.begin()->second.canvas.get()) {
            values[1] = uint32_t(visual->GetWidth());
            values[2] = uint32_t(visual->GetHeight());
            values[3] = visual->GetCount();
            values[4] = visual->GetVisualProxy() != nullptr;
            if (!visual->IsEmpty()) {
                const auto instruction = reinterpret_cast<OpenGlass::uDWM::CDrawGeometryInstruction*>(
                    visual->GetInstructions().views().front());
                values[5] = instruction->GetBrush() == brushes.begin()->second.brush.get();
            }
        }
    }
}

uint32_t describeSurfaces(std::span<DiagnosticSurface> surfaces) {
    const CompositorAccess lock(OpenGlass::uDWM::CDesktopManager::s_csDwmInstance);
    if (!lock) {
        return 0;
    }
    uint32_t count = 0;
    for (const auto& [window, value] : brushes) {
        if (const auto owner = value.owner.lock(); owner && value.canvas && count < surfaces.size()) {
            const auto description = owner->description();
            surfaces[count++] = {description.window, description.process, description.material,
                                 value.canvas->GetWidth(), value.canvas->GetHeight()};
        }
    }
    return count;
}

}  // namespace cyberglass::dwm
