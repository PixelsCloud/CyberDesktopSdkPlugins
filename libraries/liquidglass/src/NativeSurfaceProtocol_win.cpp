#include "libraries/liquidglass/src/NativeSurfaceProtocol_win.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <type_traits>

namespace liquidglass::native {

static_assert(std::is_trivially_copyable_v<SurfaceDescription>);
static_assert(sizeof(SurfaceDescription) == 144);
static_assert(offsetof(SurfaceRecord, active) == 192);
static_assert(sizeof(SurfaceRecord) == 256);
static_assert(std::atomic_ref<uint32_t>::is_always_lock_free);

std::wstring surfaceMappingName(uint32_t process, uintptr_t window, uint32_t cookie) {
    return L"Local\\CyberGlass.Native.Surface.v1." + std::to_wstring(process) + L"." + std::to_wstring(window) + L"." +
           std::to_wstring(cookie);
}

uint32_t surfaceNotificationMessage() {
    static const auto message = RegisterWindowMessageW(L"CyberGlass.Native.Surface.Status.v1");
    return message;
}

SurfaceOptics encodeParameters(const Parameters& p) {
    return {p.displacementScale,
            p.blurAmount,
            p.saturation,
            p.aberrationIntensity,
            p.elasticity,
            uint32_t(p.mode),
            p.overLight ? 1u : 0u,
            p.mouseOffsetX,
            p.mouseOffsetY,
            p.hoverGlowOpacity,
            p.pressGlowOpacity,
            p.interactionGlowOpacity,
            p.scaleX,
            p.scaleY,
            p.offsetX,
            p.offsetY,
            p.layoutWidth,
            p.layoutHeight};
}

Parameters decodeParameters(const SurfaceOptics& p) {
    Parameters parameters;
    parameters.displacementScale = p.displacementScale;
    parameters.blurAmount = p.blurAmount;
    parameters.saturation = p.saturation;
    parameters.aberrationIntensity = p.aberrationIntensity;
    parameters.elasticity = p.elasticity;
    parameters.mode = react::RefractionMode(p.mode);
    parameters.overLight = p.overLight == 1;
    parameters.mouseOffsetX = p.mouseOffsetX;
    parameters.mouseOffsetY = p.mouseOffsetY;
    parameters.hoverGlowOpacity = p.hoverGlowOpacity;
    parameters.pressGlowOpacity = p.pressGlowOpacity;
    parameters.interactionGlowOpacity = p.interactionGlowOpacity;
    parameters.scaleX = p.scaleX;
    parameters.scaleY = p.scaleY;
    parameters.offsetX = p.offsetX;
    parameters.offsetY = p.offsetY;
    parameters.layoutWidth = p.layoutWidth;
    parameters.layoutHeight = p.layoutHeight;
    return parameters;
}

bool readSurface(const SurfaceRecord& record, SurfaceDescription& description) {
    const std::atomic_ref<const uint32_t> sequence(record.sequence);
    for (int attempt = 0; attempt < 4; ++attempt) {
        const auto before = sequence.load(std::memory_order_acquire);
        if (before & 1u) {
            continue;
        }
        std::memcpy(&description, &record.description, sizeof(description));
        std::atomic_thread_fence(std::memory_order_acquire);
        if (sequence.load(std::memory_order_acquire) == before) {
            return true;
        }
    }
    return false;
}

void writeSurface(SurfaceRecord& record, const SurfaceDescription& description) {
    const std::atomic_ref<uint32_t> sequence(record.sequence);
    sequence.fetch_add(1, std::memory_order_acq_rel);
    std::memcpy(&record.description, &description, sizeof(description));
    sequence.fetch_add(1, std::memory_order_release);
}

bool validSurface(const SurfaceDescription& description, uint32_t process, uintptr_t window, uint32_t cookie) {
    if (description.magic != kProtocolMagic || description.version != kProtocolVersion ||
        description.size != sizeof(SurfaceDescription) || description.process != process ||
        description.window != window || description.cookie != cookie || !description.revision ||
        description.attachedTop > 1 || description.optics.overLight > 1 || description.optics.mode > 3 ||
        description.material != uint32_t(NativeMaterial::LiquidGlass) ||
        std::any_of(description.reservedMaterial.begin(), description.reservedMaterial.end(),
                    [](uint32_t value) {
                        return value != 0;
                    }) ||
        !std::isfinite(description.dpiScale) || description.dpiScale < .25f || description.dpiScale > 8.f ||
        !std::isfinite(description.corner) || description.corner < 0 || description.corner > 16384) {
        return false;
    }
    const auto parameters = decodeParameters(description.optics);
    auto normalized = parameters;
    normalized.normalize();
    return normalized == parameters;
}

}  // namespace liquidglass::native
