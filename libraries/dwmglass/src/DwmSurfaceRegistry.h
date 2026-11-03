// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <memory>
#include <span>

namespace liquidglass::native {
struct SurfaceDescription;
}

namespace cyberglass::dwm {
struct DiagnosticSurface;

class Surface final {
public:
    Surface(uintptr_t window, uint32_t cookie, uint16_t id, uint64_t generation);
    ~Surface();
    bool reload();
    uint16_t id() const;
    uint32_t cookie() const;
    liquidglass::native::SurfaceDescription description() const;
    void report(uint32_t error, bool active, uint64_t revision = 0, uint32_t material = UINT32_MAX) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

class SurfaceRegistry final {
public:
    explicit SurfaceRegistry(uint64_t generation);
    ~SurfaceRegistry();
    std::shared_ptr<Surface> track(uintptr_t window);
    std::shared_ptr<Surface> find(uint16_t id) const;
    void retire(uintptr_t window);
    void clear();
    float samplingExpansion() const;
    uint64_t retirementRevision() const;
    bool empty() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

void installSurfaceHooks(const std::shared_ptr<SurfaceRegistry>& registry);
void removeSurfaceHooks();
void installMaterialHooks(const std::shared_ptr<SurfaceRegistry>& registry);
void removeMaterialHooks();
void clearMaterials();
void initializeInfrastructure();
uint32_t encodeSurfaceMarker(uint16_t id, uint64_t revision);
uint16_t decodeSurfaceMarker(uint32_t marker);
void inspectSurface(std::span<uint64_t> values);
uint64_t residentMaterialCount();
uint32_t describeSurfaces(std::span<DiagnosticSurface> surfaces);

}  // namespace cyberglass::dwm
