#pragma once

#include <cstdint>
#include <memory>

#include "LiquidGlass/Effect.h"

namespace liquidglass {

struct NativeBackendIdentity;

enum class NativeMaterial : uint32_t { LiquidGlass, AeroGlass };
enum class NativeSurfaceEvent : uintptr_t { Frame = 1, BackendReady = 2, BackendUnavailable = 3 };

struct AeroParameters {
    float blurSigma = 12, colorBalance = .08f, afterglowBalance = .08f, blurBalance = .92f;
    uint32_t color = 0xffa9b1bb;
    bool operator==(const AeroParameters&) const = default;
};

// Own-window registration only. Background contributors are managed by DWM.
// Calls must remain on the owning window's thread; destroy before its HWND.
class NativeSurface final {
public:
    NativeSurface();
    ~NativeSurface();
    void attach(uintptr_t window);
    void update(Parameters parameters, float dpiScale, float corner, bool attachedTop,
                NativeMaterial material = NativeMaterial::LiquidGlass, AeroParameters aero = {});
    void detach();
    void refresh();
    bool active() const;
    uint32_t error() const;
    uint32_t cookie() const;
    uint64_t revision() const;
    uint64_t appliedRevision() const;
    NativeMaterial appliedMaterial() const;
    NativeBackendIdentity identity() const;
    static uint32_t notificationMessage();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace liquidglass
