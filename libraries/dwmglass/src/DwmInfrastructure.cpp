// SPDX-License-Identifier: GPL-3.0-only
#include <KNSoft/NDK/NDK.h>
#include <Windows.h>
#include <psapi.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>

#include "libraries/dwmglass/src/DwmSurfaceRegistry.h"
#include "libraries/liquidglass/src/NativeSurfaceProtocol_win.h"
#include "third_party/OpenGlass/OpenGlass/Architecture/Legacy/GlassInfra.hpp"
#include "third_party/OpenGlass/OpenGlass/Architecture/Legacy/GlassIntegrity.hpp"
#include "third_party/OpenGlass/OpenGlass/Architecture/Legacy/dwmcoreProjection.hpp"
#include "third_party/OpenGlass/OpenGlass/GlassRenderer.hpp"

namespace {
UINT drawGeometryCommand = 0;
}

// The native host supplies settings through its surface protocol. The optional
// registry settings provider used by upstream configuration code is not used.
HKEY OpenGlass::GlassEngine::GetDwmKey() {
    return nullptr;
}

HKEY OpenGlass::GlassEngine::GetMachineDwmKey() {
    return nullptr;
}

bool OpenGlass::GlassInfra::IsDrawGeometryCommand(UINT command, const DWM::span<const BYTE>* resources) noexcept {
    return command == drawGeometryCommand && resources && resources->length == sizeof(dwmcore::CDrawGeometryCommand);
}

void OpenGlass::GlassInfra::RedrawTopLevelWindow(uDWM::CTopLevelWindow* window, bool, bool notifySizeChange) {
    const auto data = window->GetData();
    if (!data || !GetPropW(data->GetHwnd(), liquidglass::native::kSurfaceProperty)) {
        return;
    }
    window->OnBlurBehindUpdated();
    window->SetDirtyFlags(0x1000000u);
    if (notifySizeChange) {
        LOG_IF_FAILED(data->OnWindowSizeUpdated());
    }
}

namespace cyberglass::dwm {

void initializeInfrastructure() {
    using namespace OpenGlass;
    switch (dwmcore::g_versionInfo.build) {
        case 17763:
            drawGeometryCommand = 460;
            return;
        case 18362:
            drawGeometryCommand = 535;
            return;
        case 19041:
            drawGeometryCommand = 461;
            return;
        case 22000:
            drawGeometryCommand = 456;
            return;
        case 22621:
            drawGeometryCommand = 444;
            return;
        default:
            break;
    }
    const auto code = static_cast<const uint8_t*>(dwmcore::Symbol_CRenderDataBuilder_DrawGeometry.get());
    MODULEINFO module{};
    winrt::check_bool(GetModuleInformation(GetCurrentProcess(), dwmcore::g_moduleHandle, &module, sizeof(module)));
    const uintptr_t first = reinterpret_cast<uintptr_t>(code);
    const uintptr_t moduleFirst = reinterpret_cast<uintptr_t>(module.lpBaseOfDll);
    if (first < moduleFirst || first - moduleFirst > module.SizeOfImage ||
        module.SizeOfImage - (first - moduleFirst) < 132) {
        throw std::runtime_error("DWM draw geometry symbol is outside its module");
    }
    constexpr std::array<uint8_t, 4> prefix{0xc7, 0x44, 0x0a, 0x04};
    size_t matches = 0;
    for (size_t offset = 0; offset <= 124; ++offset) {
        if (std::memcmp(code + offset, prefix.data(), prefix.size()) == 0) {
            std::memcpy(&drawGeometryCommand, code + offset + prefix.size(), sizeof(drawGeometryCommand));
            ++matches;
        }
    }
    if (matches != 1 || drawGeometryCommand == 0 || drawGeometryCommand > 4096) {
        throw std::runtime_error("DWM draw geometry command could not be validated");
    }
}

}  // namespace cyberglass::dwm
