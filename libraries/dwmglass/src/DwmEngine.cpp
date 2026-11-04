// SPDX-License-Identifier: GPL-3.0-only
#include <KNSoft/NDK/NDK.h>
#include <Windows.h>
#include <dwmapi.h>
#include <roapi.h>
#include <wil/stl.h>
#include <winrt/base.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <string>

#include "libraries/dwmglass/src/DwmDiagnosticPacket.h"
#include "libraries/dwmglass/src/DwmDiagnostics.h"
#include "libraries/dwmglass/src/DwmLifecycleLock.h"
#include "libraries/dwmglass/src/DwmSurfaceRegistry.h"
#include "libraries/liquidglass/src/NativeServiceProtocol_win.h"
#include "third_party/OpenGlass/Common/PeCodeViewIdentity.hpp"
#include "third_party/OpenGlass/OpenGlass/Architecture/Legacy/GlassIntegrity.hpp"
#include "third_party/OpenGlass/OpenGlass/Architecture/Legacy/dwmcoreProjection.hpp"
#include "third_party/OpenGlass/OpenGlass/Architecture/Legacy/uDwmProjection.hpp"
#include "third_party/OpenGlass/OpenGlass/GlassRenderer.hpp"
#include "third_party/OpenGlass/OpenGlass/Shared.hpp"
#include "third_party/OpenGlass/OpenGlass/SymbolCatalog.hpp"
#include "third_party/OpenGlass/OpenGlass/SymbolParser.hpp"

namespace cyberglass::dwm {
namespace {
using namespace OpenGlass;

struct EngineState {
    std::shared_ptr<SurfaceRegistry> registry;
    uint64_t generation = 0;
};

std::mutex lifecycle;
std::unique_ptr<EngineState> running;
bool projectionsReady = false;
std::atomic<uint64_t> diagnosticDeadline{0};
std::array<std::atomic<uint64_t>, size_t(Diagnostic::Count)> diagnosticCounters{};
std::mutex errorMutex;
std::array<char, 192> lastError{};

HRESULT destroyDevice(dwmcore::CD2DContext* context);
Projection::Detour<dwmcore::Symbol_CD2DContext_DestroyDeviceResources, &destroyDevice> destroyDeviceOriginal;

HRESULT destroyDevice(dwmcore::CD2DContext* context) {
    clearMaterials();
    GlassRenderer::DestroyDeviceResources(context);
    GlassIntegrity::DestroyDeviceResources(context);
    return destroyDeviceOriginal(context);
}

void validateModuleIdentity(HMODULE module) {
    const auto path = wil::GetModuleFileNameW<std::wstring>(module);
    PeCodeViewIdentity loaded, file;
    winrt::check_hresult(ReadLoadedPeCodeViewIdentity(module, loaded));
    winrt::check_hresult(ReadFilePeCodeViewIdentity(path.c_str(), file));
    if (loaded.machine != IMAGE_FILE_MACHINE_AMD64 || loaded.machine != file.machine ||
        loaded.timeDateStamp != file.timeDateStamp || loaded.sizeOfImage != file.sizeOfImage ||
        loaded.pdbAge != file.pdbAge || !InlineIsEqualGUID(loaded.pdbGuid, file.pdbGuid)) {
        throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH),
                                   L"Loaded DWM module differs from the symbol source");
    }
}

void resolveProjections(const wchar_t* symbolDirectory) {
    validateModuleIdentity(uDWM::g_moduleHandle);
    validateModuleIdentity(dwmcore::g_moduleHandle);
    winrt::check_bool(uDWM::g_registry.Freeze({uDWM::g_versionInfo.build, uDWM::g_versionInfo.revision}));
    winrt::check_bool(dwmcore::g_registry.Freeze({dwmcore::g_versionInfo.build, dwmcore::g_versionInfo.revision}));
    const bool userSymbols =
        Projection::CollectBuiltInSymbols(uDWM::g_moduleHandle, Projection::ModuleId::uDWM, uDWM::g_registry);
    const bool coreSymbols =
        Projection::CollectBuiltInSymbols(dwmcore::g_moduleHandle, Projection::ModuleId::DwmCore, dwmcore::g_registry);
    if (!userSymbols || !coreSymbols) {
        if (!symbolDirectory || !*symbolDirectory) {
            throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND), L"Exact DWM symbols are required");
        }
        CSymbolParser parser(symbolDirectory);
        if (!userSymbols) {
            winrt::check_hresult(parser.ParsePdb(uDWM::g_moduleHandle, uDWM::g_registry));
        }
        if (!coreSymbols) {
            winrt::check_hresult(parser.ParsePdb(dwmcore::g_moduleHandle, dwmcore::g_registry));
        }
    }
    if (!Projection::CommitModules(uDWM::g_registry, dwmcore::g_registry)) {
        throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH), L"DWM projection validation failed");
    }
}
}  // namespace

void sample(Diagnostic counter, uint64_t value) {
    const auto deadline = diagnosticDeadline.load(std::memory_order_relaxed);
    if (deadline && GetTickCount64() < deadline) {
        if (counter == Diagnostic::Error || counter == Diagnostic::RawAlpha) {
            diagnosticCounters[size_t(counter)].store(value, std::memory_order_relaxed);
        } else {
            diagnosticCounters[size_t(counter)].fetch_add(1, std::memory_order_relaxed);
        }
    }
}

void recordFailure(const char* text) {
    std::unique_lock lock(errorMutex, std::try_to_lock);
    if (!lock) {
        return;
    }
    const auto size = std::min(std::strlen(text), lastError.size() - 1);
    std::memcpy(lastError.data(), text, size);
    lastError[size] = 0;
}

DWORD inspect(DiagnosticPacket* packet) {
    std::unique_lock lifecycleLock(lifecycle, std::try_to_lock);
    if (!lifecycleLock) {
        return ERROR_BUSY;
    }
    if (!packet || packet->magic != 0x43474431 || packet->version != 1 || packet->size != sizeof(DiagnosticPacket)) {
        return ERROR_INVALID_PARAMETER;
    }
    diagnosticDeadline.store(packet->enabled ? GetTickCount64() + 30000 : 0, std::memory_order_relaxed);
    auto& values = packet->values;
    values[0] = packet->enabled;
    for (size_t index = 0; index < diagnosticCounters.size(); ++index) {
        values[index + 1] = diagnosticCounters[index].load(std::memory_order_relaxed);
    }
    if (running) {
        inspectSurface(std::span<uint64_t>(values.data() + 11, 6));
    }
    std::lock_guard errorLock(errorMutex);
    packet->error = lastError;
    packet->residentLiquidMaterials = residentMaterialCount();
    if (running) {
        packet->surfaceCount = describeSurfaces(packet->surfaces);
    }
    return ERROR_SUCCESS;
}

DWORD start(liquidglass::native::BootRequest* request) try {
    std::unique_lock lifecycleLock(lifecycle, std::try_to_lock);
    if (!lifecycleLock) {
        return ERROR_BUSY;
    }
    if (!request || request->magic != liquidglass::native::kBootMagic || request->version != 1 ||
        !request->generation || request->reserved || request->pathCharacters > 32767) {
        return ERROR_INVALID_DATA;
    }
    if (running) {
        request->generation = running->generation;
        return ERROR_ALREADY_INITIALIZED;
    }
    const auto path = reinterpret_cast<const wchar_t*>(request + 1);
    if (path[request->pathCharacters] != L'\0') {
        return ERROR_INVALID_DATA;
    }
    const std::wstring symbols(path, request->pathCharacters);
    if (!GetModuleHandleW(L"dwm.exe") || !uDWM::g_moduleHandle || !dwmcore::g_moduleHandle) {
        return ERROR_ACCESS_DENIED;
    }
    if (!projectionsReady) {
        resolveProjections(symbols.c_str());
        initializeInfrastructure();
        projectionsReady = true;
    }
    auto candidate = std::make_unique<EngineState>();
    candidate->generation = request->generation;
    candidate->registry = std::make_shared<SurfaceRegistry>(request->generation);
    // Upstream's rendering/occlusion recognition is retained; only explicitly
    // registered client geometry receives its marker. Native settings and other
    // applications' non-client frames are left under Windows' own processing.
    Shared::g_type = Shared::GlassType::Blur;
    Shared::g_transparencyEnabled = true;
    Shared::g_disableOnBattery = false;
    Shared::g_useD3DRendering = false;
    // Keep callback code valid until DWM exits. A service restart is not a
    // safe DLL-unload boundary for in-flight upstream hooks.
    HMODULE pinned = nullptr;
    winrt::check_bool(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                                         reinterpret_cast<LPCWSTR>(&start), &pinned));
    {
        const CompositorAccess compositor(uDWM::CDesktopManager::s_csDwmInstance);
        if (!compositor) {
            return ERROR_BUSY;
        }
        HookHelper::GetHookRundown().Open();
        HookHelper::HookTransaction transaction(HookHelper::HookMode::Install);
        GlassIntegrity::Startup();
        GlassRenderer::Startup();
        installMaterialHooks(candidate->registry);
        installSurfaceHooks(candidate->registry);
        HookHelper::ApplyInlineHooks(std::initializer_list<HookHelper::DetourInfo>{{&destroyDeviceOriginal}});
        transaction.Commit();
    }
    running = std::move(candidate);
    return ERROR_SUCCESS;
} catch (const winrt::hresult_error& error) {
    return uint32_t(error.code());
} catch (...) {
    return ERROR_INVALID_DATA;
}

DWORD stop() {
    std::unique_lock lifecycleLock(lifecycle, std::try_to_lock);
    if (!lifecycleLock || running) {
        // Upstream WaitForDrain fail-fasts on timeout. Do not call it inside
        // DWM, unpatch active callbacks or unload their code. Effects remain
        // available; the module's lifetime ends with the compositor process.
        return ERROR_BUSY;
    }
    return ERROR_SUCCESS;
}

}  // namespace cyberglass::dwm

extern "C" __declspec(dllexport) DWORD WINAPI CyberGlassDwmStart(void* request) {
    return cyberglass::dwm::start(static_cast<liquidglass::native::BootRequest*>(request));
}

extern "C" __declspec(dllexport) DWORD WINAPI CyberGlassDwmStop(void*) {
    return cyberglass::dwm::stop();
}

extern "C" __declspec(dllexport) DWORD WINAPI CyberGlassDwmInspect(void* request) {
    return cyberglass::dwm::inspect(static_cast<cyberglass::dwm::DiagnosticPacket*>(request));
}

BOOL WINAPI DllMain(HMODULE, DWORD, void*) {
    return TRUE;
}
