// SPDX-License-Identifier: GPL-3.0-only
#include <KNSoft/NDK/NDK.h>
#include <Windows.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <winrt/base.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <span>
#include <utility>

#include "LiquidGlass/CompositionMaterial.h"
#include "libraries/dwmglass/src/DwmDiagnostics.h"
#include "libraries/dwmglass/src/DwmSurfaceRegistry.h"
#include "libraries/liquidglass/src/NativeSurfaceProtocol_win.h"
#include "third_party/OpenGlass/OpenGlass/Architecture/Legacy/DetourChains.hpp"
#include "third_party/OpenGlass/OpenGlass/Architecture/Legacy/GlassInfra.hpp"
#include "third_party/OpenGlass/OpenGlass/Architecture/Legacy/dwmcoreProjection.hpp"
#include "third_party/OpenGlass/OpenGlass/GlassKernel.hpp"
#include "third_party/OpenGlass/OpenGlass/GlassRealizer.hpp"
#include "third_party/OpenGlass/OpenGlass/GlassRenderer.hpp"
#include "third_party/OpenGlass/OpenGlass/Util.hpp"

namespace OpenGlass::GlassRenderer {
HRESULT MyCDrawingContext_DrawGeometry(dwmcore::IDrawingContext*, dwmcore::CLegacyMilBrush*, dwmcore::CGeometry*);
extern decltype(&MyCDrawingContext_DrawGeometry) g_CDrawingContext_DrawGeometry_Org;
extern DetourChains::RenderDataTryDrawCommandAsDrawListWin10RendererNode
    g_CRenderData_TryDrawCommandAsDrawList_Win10_Org;
extern DetourChains::RenderDataTryDrawCommandAsDrawListWin11RendererNode
    g_CRenderData_TryDrawCommandAsDrawList_Win11_Org;
}  // namespace OpenGlass::GlassRenderer

namespace OpenGlass::GlassIntegrity {
HRESULT MyCDrawingContext_DrawVisualTree_Win10_1809(dwmcore::CDrawingContext*, const dwmcore::CVisualTree*,
                                                    const D2D1_RECT_F&, dwmcore::COverlayContext*, int, bool, bool,
                                                    bool, bool, bool, bool, bool);
HRESULT MyCDrawingContext_DrawVisualTree_Win10_1903(dwmcore::CDrawingContext*, const dwmcore::CVisualTree*,
                                                    const D2D1_RECT_F&, dwmcore::COverlayContext*, int, bool, bool,
                                                    bool, const D2D1_RECT_F*, bool, bool, bool);
HRESULT MyCDrawingContext_DrawVisualTree_Win10(dwmcore::CDrawingContext*, const dwmcore::CVisualTree*,
                                               const D2D1_RECT_F&, const dwmcore::COcclusionContext*, int, bool);
HRESULT MyCDrawingContext_DrawVisualTree_Win11(dwmcore::CDrawingContext*, const dwmcore::CVisualTree*,
                                               const D2D1_RECT_F&, const dwmcore::COcclusionContext*, int, bool,
                                               dwmcore::CVisual*);
}  // namespace OpenGlass::GlassIntegrity

namespace cyberglass::dwm {
namespace {
using namespace OpenGlass;
using liquidglass::CompositionRegion;
using Microsoft::WRL::ComPtr;
std::weak_ptr<SurfaceRegistry> materialRegistry;
thread_local std::shared_ptr<Surface> currentSurface;
thread_local D2D1_RECT_F currentSurfaceBounds{};
using DrawFunction = decltype(&GlassRenderer::MyCDrawingContext_DrawGeometry);
using RenderFunction = HRESULT (*)(CGlassRealizer*, ID2D1DeviceContext*, ID2D1Geometry*, const D2D1_RECT_F&,
                                   const std::span<const D2D1_RECT_F>&, const CAeroParams&, Shared::GlassType);
DrawFunction originalDraw = &GlassRenderer::MyCDrawingContext_DrawGeometry;
auto originalListWin10 = &GlassRenderer::MyCRenderData_TryDrawCommandAsDrawList_Win10;
auto originalListWin11 = &GlassRenderer::MyCRenderData_TryDrawCommandAsDrawList_Win11;
RenderFunction originalRender = std::bit_cast<RenderFunction>(&CGlassRealizer::Render);
auto originalExpansion = &GlassKernel::GetBlurExpansion;

struct MaterialCache {
    std::weak_ptr<Surface> owner;
    ComPtr<ID3D11Device> device;
    ComPtr<ID2D1Device> drawingDevice;
    std::unique_ptr<liquidglass::CompositionMaterial> material;
    uint64_t revision = 0;
};

// Accessed only while the native compositor is drawing. No timer or polling.
std::map<uint16_t, MaterialCache> materials;
uint64_t lastRetirement = 0;
std::atomic<uint64_t> residentCount{0};

void pruneMaterials() {
    if (currentSurface) {
        return;
    }
    const auto registry = materialRegistry.lock();
    if (!registry || registry->retirementRevision() == lastRetirement) {
        return;
    }
    lastRetirement = registry->retirementRevision();
    std::erase_if(materials, [](const auto& entry) {
        return entry.second.owner.expired();
    });
    if (registry->empty()) {
        materials.clear();
        GlassRenderer::Cleanup();
    }
    residentCount.store(materials.size(), std::memory_order_release);
}

template <auto Callback>
struct FrameBoundary;

template <typename... Args, HRESULT (*Callback)(Args...)>
struct FrameBoundary<Callback> {
    inline static decltype(Callback) original = Callback;

    static HRESULT invoke(Args... args) {
        pruneMaterials();
        return original(args...);
    }

    static auto binding() {
        return HookHelper::HookRundownThunk<&invoke>::Bind(&original);
    }
};

using Boundary1809 = FrameBoundary<&GlassIntegrity::MyCDrawingContext_DrawVisualTree_Win10_1809>;
using Boundary1903 = FrameBoundary<&GlassIntegrity::MyCDrawingContext_DrawVisualTree_Win10_1903>;
using Boundary10 = FrameBoundary<&GlassIntegrity::MyCDrawingContext_DrawVisualTree_Win10>;
using Boundary11 = FrameBoundary<&GlassIntegrity::MyCDrawingContext_DrawVisualTree_Win11>;

float expansion() {
    const auto registry = materialRegistry.lock();
    return registry ? registry->samplingExpansion() : 0;
}

std::shared_ptr<Surface> findSurface(dwmcore::CLegacyMilBrush* brush) {
    std::shared_ptr<Surface> surface;
    if (brush && HookHelper::get_vftable_from(brush) == dwmcore::CSolidColorLegacyMilBrush::vftable) {
        sample(Diagnostic::Solid);
        const auto color = static_cast<dwmcore::CSolidColorLegacyMilBrush*>(brush)->GetRealizedColor();
        const auto marker = std::bit_cast<uint32_t>(color.a);
        if (color.a > 0 && color.a < 1) {
            sample(Diagnostic::RawAlpha, marker);
        }
        if (const auto id = decodeSurfaceMarker(marker)) {
            sample(Diagnostic::Brush);
            if (const auto registry = materialRegistry.lock()) {
                surface = registry->find(id);
            }
        }
    }
    return surface;
}

bool ownsCommand(dwmcore::CRenderData* data, UINT type, DWM::span<const BYTE>* resources) {
    if (!GlassInfra::IsDrawGeometryCommand(type, resources)) {
        return false;
    }
    const auto command = reinterpret_cast<const dwmcore::CDrawGeometryCommand*>(resources->data);
    sample(Diagnostic::Command);
    const auto brushes = data->GetResources();
    if (!brushes || command->brushIndex >= brushes->count) {
        return false;
    }
    const auto brush = reinterpret_cast<dwmcore::CLegacyMilBrush*>(brushes->data[command->brushIndex]);
    return findSurface(brush) != nullptr;
}

HRESULT drawListWin10(dwmcore::CRenderData* data, dwmcore::CDrawingContext* context, dwmcore::CDrawListCache* cache,
                      dwmcore::CDrawListEntryBuilder* builder, bool unknown, UINT type,
                      DWM::span<const BYTE>* resources, bool* succeeded) try {
    if (!ownsCommand(data, type, resources)) {
        return GlassRenderer::g_CRenderData_TryDrawCommandAsDrawList_Win10_Org(data, context, cache, builder, unknown,
                                                                               type, resources, succeeded);
    }
    return originalListWin10(data, context, cache, builder, unknown, type, resources, succeeded);
} catch (...) {
    return GlassRenderer::g_CRenderData_TryDrawCommandAsDrawList_Win10_Org(data, context, cache, builder, unknown, type,
                                                                           resources, succeeded);
}

HRESULT drawListWin11(dwmcore::CRenderData* data, dwmcore::CDrawingContext* context, dwmcore::CDrawListCache* cache,
                      dwmcore::CDrawListEntryBuilder* builder, UINT type, DWM::span<const BYTE>* resources,
                      bool* succeeded) try {
    if (!ownsCommand(data, type, resources)) {
        return GlassRenderer::g_CRenderData_TryDrawCommandAsDrawList_Win11_Org(data, context, cache, builder, type,
                                                                               resources, succeeded);
    }
    return originalListWin11(data, context, cache, builder, type, resources, succeeded);
} catch (...) {
    return GlassRenderer::g_CRenderData_TryDrawCommandAsDrawList_Win11_Org(data, context, cache, builder, type,
                                                                           resources, succeeded);
}

HRESULT drawGeometry(dwmcore::IDrawingContext* drawing, dwmcore::CLegacyMilBrush* brush,
                     dwmcore::CGeometry* geometry) try {
    auto surface = findSurface(brush);
    if (!surface) {
        // Preserve native processing and cost for every unregistered surface.
        return GlassRenderer::g_CDrawingContext_DrawGeometry_Org(drawing, brush, geometry);
    }
    // The renderer later supplies the unoccluded, dirty shape. Its bounds can
    // be only a narrow strip when a layered Qt foreground covers this window.
    // Optical layout and corner radii must use the original complete geometry.
    dwmcore::CShapePtr shape;
    winrt::check_hresult(geometry->GetShapeData(nullptr, &shape));
    if (!shape || shape->IsEmpty()) {
        return S_OK;
    }
    D2D1_RECT_F bounds{};
    winrt::check_hresult(shape->GetTightBounds(&bounds, drawing->GetDrawingContext()->GetWorldTransform()));
    const auto previousBounds = std::exchange(currentSurfaceBounds, bounds);
    const auto previous = std::exchange(currentSurface, std::move(surface));
    const auto restore = wil::scope_exit([&] {
        currentSurface = previous;
        currentSurfaceBounds = previousBounds;
    });
    return originalDraw(drawing, brush, geometry);
} catch (...) {
    return GlassRenderer::g_CDrawingContext_DrawGeometry_Org(drawing, brush, geometry);
}

HRESULT render(CGlassRealizer* realizer, ID2D1DeviceContext* context, ID2D1Geometry* geometry,
               const D2D1_RECT_F& drawingBounds, const std::span<const D2D1_RECT_F>& rectangles,
               const CAeroParams& parameters, Shared::GlassType type) try {
    const auto surface = currentSurface;
    if (!surface) {
        return originalRender(realizer, context, geometry, drawingBounds, rectangles, parameters, type);
    }
    const auto description = surface->description();
    sample(Diagnostic::Render);
    winrt::com_ptr<ID2D1Bitmap1> target;
    winrt::com_ptr<ID3D11Texture2D> texture;
    winrt::check_hresult(Util::GetTargetBitmapFromD2DContext(context, target));
    winrt::check_hresult(Util::GetTextureFromD2DBitmap(target.get(), texture));
    ComPtr<ID3D11Device> device;
    texture->GetDevice(&device);
    ComPtr<ID2D1Device> d2d;
    context->GetDevice(&d2d);
    auto& cached = materials[surface->id()];
    if (cached.owner.lock() != surface || cached.device.Get() != device.Get() ||
        cached.drawingDevice.Get() != d2d.Get()) {
        MaterialCache candidate;
        candidate.owner = surface;
        candidate.device = device;
        candidate.drawingDevice = d2d;
        candidate.material = std::make_unique<liquidglass::CompositionMaterial>();
        candidate.material->initialize(device.Get(), d2d.Get());
        cached = std::move(candidate);
        residentCount.store(materials.size(), std::memory_order_release);
    }
    const auto bounds = currentSurfaceBounds;
    D3D11_TEXTURE2D_DESC source{};
    texture->GetDesc(&source);
    const auto left = std::floor(double(bounds.left)), top = std::floor(double(bounds.top));
    const auto right = std::ceil(double(bounds.right)), bottom = std::ceil(double(bounds.bottom));
    if (!std::isfinite(left) || !std::isfinite(top) || !std::isfinite(right) || !std::isfinite(bottom) ||
        left < std::numeric_limits<int>::min() || top < std::numeric_limits<int>::min() ||
        right > std::numeric_limits<int>::max() || bottom > std::numeric_limits<int>::max() || right - left > 16384 ||
        bottom - top > 16384) {
        throw std::invalid_argument("Native material geometry exceeds GPU dimensions");
    }
    if (right <= 0 || bottom <= 0 || left >= source.Width || top >= source.Height) {
        return S_OK;
    }
    const CompositionRegion region{int(left), int(top), int(right - left), int(bottom - top)};
    if (region.width < 1 || region.height < 1) {
        return S_OK;
    }
    liquidglass::EffectGeometry optics{region.width,
                                       region.height,
                                       0,
                                       0,
                                       description.dpiScale,
                                       description.corner * description.dpiScale,
                                       description.attachedTop != 0};
    const auto p = liquidglass::native::decodeParameters(description.optics);
    const float halo = liquidglass::opticalExpansion(p, description.dpiScale);
    std::array<CompositionRegion, 16> updates;
    size_t count = 0;
    const auto addUpdate = [&](D2D1_RECT_F rectangle) {
        rectangle.left = std::max(rectangle.left, drawingBounds.left);
        rectangle.top = std::max(rectangle.top, drawingBounds.top);
        rectangle.right = std::min(rectangle.right, drawingBounds.right);
        rectangle.bottom = std::min(rectangle.bottom, drawingBounds.bottom);
        if (rectangle.left < rectangle.right && rectangle.top < rectangle.bottom) {
            const int x = int(std::floor(rectangle.left - halo));
            const int y = int(std::floor(rectangle.top - halo));
            updates[count++] = {x, y, int(std::ceil(rectangle.right + halo)) - x,
                                int(std::ceil(rectangle.bottom + halo)) - y};
        }
    };
    if (rectangles.size() > updates.size()) {
        addUpdate(drawingBounds);
    } else {
        for (const auto& rectangle : rectangles) {
            addUpdate(rectangle);
        }
    }
    if (!count) {
        return S_OK;
    }
    // Submit DWM's pending D2D commands before sampling its target. This is
    // a local D2D flush, never the cross-process/presentation wait DwmFlush.
    winrt::check_hresult(context->Flush());
    cached.material->render(texture.get(), region, optics, p, ++cached.revision,
                            std::span<const CompositionRegion>(updates.data(), count));
    const auto bitmap = static_cast<ID2D1Bitmap1*>(cached.material->bitmap());
    D2D1_MATRIX_3X2_F transform{};
    context->GetTransform(&transform);
    context->SetTransform(D2D1::Matrix3x2F::Identity());
    const auto restore = wil::scope_exit([&] {
        context->SetTransform(transform);
    });
    for (auto rectangle : rectangles) {
        rectangle.left = std::max(rectangle.left, drawingBounds.left);
        rectangle.top = std::max(rectangle.top, drawingBounds.top);
        rectangle.right = std::min(rectangle.right, drawingBounds.right);
        rectangle.bottom = std::min(rectangle.bottom, drawingBounds.bottom);
        if (rectangle.left >= rectangle.right || rectangle.top >= rectangle.bottom) {
            continue;
        }
        const auto local = D2D1::RectF(float(rectangle.left - left), float(rectangle.top - top),
                                       float(rectangle.right - left), float(rectangle.bottom - top));
        context->DrawImage(bitmap, D2D1::Point2F(rectangle.left, rectangle.top), local,
                           D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR, D2D1_COMPOSITE_MODE_SOURCE_OVER);
    }
    surface->report(ERROR_SUCCESS, true, description.revision, description.material);
    return S_OK;
} catch (const winrt::hresult_error& error) {
    if (currentSurface) {
        currentSurface->report(uint32_t(error.code()), false);
    }
    return originalRender(realizer, context, geometry, drawingBounds, rectangles, parameters, type);
} catch (const std::exception& error) {
    recordFailure(error.what());
    if (currentSurface) {
        currentSurface->report(ERROR_INVALID_DATA, false);
    }
    return originalRender(realizer, context, geometry, drawingBounds, rectangles, parameters, type);
} catch (...) {
    recordFailure("Unknown native material exception");
    if (currentSurface) {
        currentSurface->report(ERROR_INVALID_DATA, false);
    }
    return originalRender(realizer, context, geometry, drawingBounds, rectangles, parameters, type);
}

auto drawDetour() {
    return HookHelper::HookRundownThunk<&drawGeometry>::Bind(&originalDraw);
}

auto renderDetour() {
    return HookHelper::HookRundownThunk<&render>::Bind(&originalRender);
}

auto expansionDetour() {
    return HookHelper::HookRundownThunk<&expansion>::Bind(&originalExpansion);
}
}  // namespace

void installMaterialHooks(const std::shared_ptr<SurfaceRegistry>& registry) {
    materialRegistry = registry;
    HookHelper::ApplyInlineHooks(std::initializer_list<HookHelper::DetourInfo>{
        {&Boundary1809::original, Boundary1809::binding()},
        {&Boundary1903::original, Boundary1903::binding()},
        {&Boundary10::original, Boundary10::binding()},
        {&Boundary11::original, Boundary11::binding()},
        {&originalListWin10, HookHelper::HookRundownThunk<&drawListWin10>::Bind(&originalListWin10)},
        {&originalListWin11, HookHelper::HookRundownThunk<&drawListWin11>::Bind(&originalListWin11)},
        {&originalDraw, drawDetour()},
        {&originalRender, renderDetour()},
        {&originalExpansion, expansionDetour()}});
}

void removeMaterialHooks() {
    HookHelper::ApplyInlineHooks(std::initializer_list<HookHelper::DetourInfo>{
        {&Boundary11::original, Boundary11::binding()},
        {&Boundary10::original, Boundary10::binding()},
        {&Boundary1903::original, Boundary1903::binding()},
        {&Boundary1809::original, Boundary1809::binding()},
        {&originalListWin11, HookHelper::HookRundownThunk<&drawListWin11>::Bind(&originalListWin11)},
        {&originalListWin10, HookHelper::HookRundownThunk<&drawListWin10>::Bind(&originalListWin10)},
        {&originalExpansion, expansionDetour()},
        {&originalRender, renderDetour()},
        {&originalDraw, drawDetour()}});
}

void clearMaterials() {
    materials.clear();
    currentSurface.reset();
    lastRetirement = 0;
    residentCount.store(0, std::memory_order_release);
}

uint64_t residentMaterialCount() {
    return residentCount.load(std::memory_order_acquire);
}

}  // namespace cyberglass::dwm
