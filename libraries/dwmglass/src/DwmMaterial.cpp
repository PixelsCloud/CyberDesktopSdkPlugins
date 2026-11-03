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
#include "LiquidGlass/Contour.h"
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

struct ClipCache {
    std::weak_ptr<Surface> owner;
    ComPtr<ID2D1Factory> factory;
    ComPtr<ID2D1PathGeometry> path;
    float width = 0, height = 0, corner = 0, shoulder = 0;
    bool attached = false;
};

std::map<uint16_t, ClipCache> clips;

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
    std::erase_if(clips, [](const auto& entry) {
        return entry.second.owner.expired();
    });
    if (registry->empty()) {
        materials.clear();
        clips.clear();
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
    return E_FAIL;
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
    return E_FAIL;
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
} catch (const winrt::hresult_error& error) {
    return error.code();
} catch (...) {
    return E_FAIL;
}

CAeroParams aeroParameters(const liquidglass::native::SurfaceDescription& description, ID2D1DeviceContext& context) {
    CAeroParams result{};
    result.blurAmount = description.aero.blurSigma * description.dpiScale;
    result.optimization = D2D1_GAUSSIANBLUR_OPTIMIZATION_BALANCED;
    const auto argb = description.aero.color;
    const float red = float((argb >> 16u) & 255u) / 255;
    const float green = float((argb >> 8u) & 255u) / 255;
    const float blue = float(argb & 255u) / 255;
    const D2D1_COLOR_F srgb = D2D1::ColorF(red, green, blue, 1);
    winrt::com_ptr<ID2D1Bitmap1> target;
    winrt::check_hresult(Util::GetTargetBitmapFromD2DContext(&context, target));
    const auto format = target->GetPixelFormat().format;
    const auto color = format == DXGI_FORMAT_R16G16B16A16_FLOAT || format == DXGI_FORMAT_R32G32B32A32_FLOAT
                           ? Color::sRGBToscRGB(srgb, 0)
                           : srgb;
    result.color = {color.r * description.aero.colorBalance, color.g * description.aero.colorBalance,
                    color.b * description.aero.colorBalance, description.aero.colorBalance};
    // OpenGlass follows Windows 7: afterglow remains sRGB even on scRGB targets.
    result.afterglow = {srgb.r * description.aero.afterglowBalance, srgb.g * description.aero.afterglowBalance,
                        srgb.b * description.aero.afterglowBalance, 1};
    result.blurBalance = description.aero.blurBalance;
    result.fallback = color;
    return result;
}

ComPtr<ID2D1PathGeometry> clipGeometry(ID2D1DeviceContext& context, const std::shared_ptr<Surface>& surface,
                                       const liquidglass::native::SurfaceDescription& description,
                                       const D2D1_RECT_F& bounds) {
    ComPtr<ID2D1Factory> factory;
    context.GetFactory(&factory);
    auto& cached = clips[surface->id()];
    const float width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
    const float corner = description.corner * description.dpiScale, shoulder = 12 * description.dpiScale;
    const bool attached = description.attachedTop != 0;
    if (cached.owner.lock() == surface && cached.factory.Get() == factory.Get() && cached.width == width &&
        cached.height == height && cached.corner == corner && cached.shoulder == shoulder &&
        cached.attached == attached) {
        return cached.path;
    }
    ClipCache candidate;
    candidate.owner = surface;
    candidate.factory = factory;
    candidate.width = width;
    candidate.height = height;
    candidate.corner = corner;
    candidate.shoulder = shoulder;
    candidate.attached = attached;
    winrt::check_hresult(factory->CreatePathGeometry(&candidate.path));
    ComPtr<ID2D1GeometrySink> sink;
    winrt::check_hresult(candidate.path->Open(&sink));
    const auto contour = liquidglass::surfaceContour(width, height, corner, attached, shoulder);
    const auto point = [](double x, double y) {
        return D2D1::Point2F(float(x), float(y));
    };
    for (uint32_t index = 0; index < contour.count; ++index) {
        const auto& segment = contour.segments[index];
        const auto& p = segment.points;
        switch (segment.verb) {
            case liquidglass::ContourVerb::Move:
                sink->BeginFigure(point(p[0], p[1]), D2D1_FIGURE_BEGIN_FILLED);
                break;
            case liquidglass::ContourVerb::Line:
                sink->AddLine(point(p[0], p[1]));
                break;
            case liquidglass::ContourVerb::Cubic:
                sink->AddBezier({point(p[0], p[1]), point(p[2], p[3]), point(p[4], p[5])});
                break;
            case liquidglass::ContourVerb::Close:
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                break;
        }
    }
    winrt::check_hresult(sink->Close());
    cached = std::move(candidate);
    return cached.path;
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
    if (description.material == uint32_t(liquidglass::NativeMaterial::AeroGlass)) {
        if (materials.erase(surface->id())) {
            residentCount.store(materials.size(), std::memory_order_release);
        }
        const auto bounds = currentSurfaceBounds;
        const bool masked = description.corner > 0 || description.attachedTop;
        const auto clip = masked ? clipGeometry(*context, surface, description, bounds) : ComPtr<ID2D1PathGeometry>{};
        if (masked) {
            context->PushLayer(D2D1::LayerParameters1(drawingBounds, clip.Get(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                                      D2D1::Matrix3x2F::Translation(bounds.left, bounds.top), 1,
                                                      nullptr, D2D1_LAYER_OPTIONS1_INITIALIZE_FROM_BACKGROUND),
                               nullptr);
        }
        const auto restoreClip = wil::scope_exit([&] {
            if (masked) {
                context->PopLayer();
            }
        });
        const auto result = originalRender(realizer, context, geometry, drawingBounds, rectangles,
                                           aeroParameters(description, *context), Shared::GlassType::Aero);
        surface->report(uint32_t(result), SUCCEEDED(result), description.revision, description.material);
        return result;
    }
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
    return error.code();
} catch (const std::exception& error) {
    recordFailure(error.what());
    if (currentSurface) {
        currentSurface->report(ERROR_INVALID_DATA, false);
    }
    return E_FAIL;
} catch (...) {
    recordFailure("Unknown native material exception");
    if (currentSurface) {
        currentSurface->report(ERROR_INVALID_DATA, false);
    }
    return E_FAIL;
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
    clips.clear();
    currentSurface.reset();
    lastRetirement = 0;
    residentCount.store(0, std::memory_order_release);
}

uint64_t residentMaterialCount() {
    return residentCount.load(std::memory_order_acquire);
}

}  // namespace cyberglass::dwm
