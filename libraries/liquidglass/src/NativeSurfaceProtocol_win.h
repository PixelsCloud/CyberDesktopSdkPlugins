#pragma once

#include <Windows.h>

#include <cstdint>
#include <string>

#include "LiquidGlass/Effect.h"
#include "LiquidGlass/NativeSurface.h"

namespace liquidglass::native {

inline constexpr wchar_t kSurfaceProperty[] = L"CyberGlass.Native.Surface.v1";
inline constexpr uint32_t kProtocolMagic = 0x43474e31;
inline constexpr uint32_t kProtocolVersion = 1;
using SurfaceNotification = NativeSurfaceEvent;

struct SurfaceOptics {
    float displacementScale = 0, blurAmount = 0, saturation = 0, aberrationIntensity = 0, elasticity = 0;
    uint32_t mode = 0, overLight = 0;
    float mouseOffsetX = 0, mouseOffsetY = 0, hoverGlowOpacity = 0, pressGlowOpacity = 0, interactionGlowOpacity = 0;
    float scaleX = 0, scaleY = 0, offsetX = 0, offsetY = 0, layoutWidth = 0, layoutHeight = 0;
};

struct SurfaceDescription {
    uint32_t magic = kProtocolMagic, version = kProtocolVersion, size = sizeof(SurfaceDescription);
    uint32_t process = 0, cookie = 0, attachedTop = 0;
    uint64_t window = 0, revision = 0;
    float dpiScale = 1, corner = 24;
    SurfaceOptics optics;
    uint32_t material = 0;
    AeroParameters aero;
};

struct alignas(64) SurfaceRecord {
    alignas(4) uint32_t sequence = 0;
    uint32_t reservedSequence = 0;
    SurfaceDescription description;
    uint32_t reservedDescription[10]{};
    // Keep compositor acknowledgements off the producer's parameter cache lines.
    alignas(4) uint32_t active = 0, error = ERROR_NOT_READY, nativeProcess = 0, nativeMaterial = 0;
    uint64_t nativeGeneration = 0;
    uint64_t nativeRevision = 0;
    uint32_t reservedFeedback[8]{};
};

std::wstring surfaceMappingName(uint32_t process, uintptr_t window, uint32_t cookie);
uint32_t surfaceNotificationMessage();
SurfaceOptics encodeParameters(const Parameters& parameters);
Parameters decodeParameters(const SurfaceOptics& optics);
bool readSurface(const SurfaceRecord& record, SurfaceDescription& description);
void writeSurface(SurfaceRecord& record, const SurfaceDescription& description);
bool validSurface(const SurfaceDescription& description, uint32_t process, uintptr_t window, uint32_t cookie);

}  // namespace liquidglass::native
