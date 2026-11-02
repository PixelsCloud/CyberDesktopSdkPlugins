#include <Windows.h>
#include <d2d1_1.h>
#include <d2d1effects.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <winrt/base.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "LiquidGlass/ReactEffect.h"
#include "LiquidGlass/Shadow.h"
#include "libraries/liquidglass/src/react/Displacement.h"

namespace liquidglass::react {
namespace {
using Microsoft::WRL::ComPtr;
#include "ReactDisplacement.h"
#include "ReactMaterialShader.h"

struct alignas(16) Constants {
    std::array<float, 4> input, panel, optics, pose, pointer, glow, frame, mask, slice, texel;
};

struct Image {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> view;
    ComPtr<ID3D11UnorderedAccessView> unordered;
    ComPtr<ID2D1Bitmap1> bitmap;
    int width = 0, height = 0;

    uint64_t bytes() const {
        return uint64_t(width) * height * 4;
    }
};

ComPtr<ID2D1Bitmap1> bitmap(ID2D1DeviceContext& d2d, ID3D11Texture2D& texture, DXGI_FORMAT format,
                            D2D1_BITMAP_OPTIONS options, D2D1_ALPHA_MODE alpha = D2D1_ALPHA_MODE_PREMULTIPLIED) {
    ComPtr<IDXGISurface> surface;
    winrt::check_hresult(texture.QueryInterface(IID_PPV_ARGS(&surface)));
    ComPtr<ID2D1Bitmap1> result;
    const auto properties = D2D1::BitmapProperties1(options, D2D1::PixelFormat(format, alpha));
    winrt::check_hresult(d2d.CreateBitmapFromDxgiSurface(surface.Get(), &properties, &result));
    return result;
}

bool ensure(Image& image, ID3D11Device& device, ID2D1DeviceContext& d2d, int width, int height, bool unordered,
            bool capacity = false) {
    if (capacity) {
        width = (width + 63) / 64 * 64;
        height = (height + 63) / 64 * 64;
    }
    if (image.width == width && image.height == height) {
        return false;
    }
    image = {};
    D3D11_TEXTURE2D_DESC description{};
    description.Width = UINT(width);
    description.Height = UINT(height);
    description.MipLevels = description.ArraySize = description.SampleDesc.Count = 1;
    description.Format = unordered ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;
    description.BindFlags =
        D3D11_BIND_SHADER_RESOURCE | (unordered ? D3D11_BIND_UNORDERED_ACCESS : D3D11_BIND_RENDER_TARGET);
    winrt::check_hresult(device.CreateTexture2D(&description, nullptr, &image.texture));
    winrt::check_hresult(device.CreateShaderResourceView(image.texture.Get(), nullptr, &image.view));
    if (unordered) {
        winrt::check_hresult(device.CreateUnorderedAccessView(image.texture.Get(), nullptr, &image.unordered));
    }
    image.bitmap =
        bitmap(d2d, *image.texture.Get(), description.Format,
               unordered ? D2D1_BITMAP_OPTIONS_NONE : D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW);
    image.width = width;
    image.height = height;
    return true;
}

struct Blur {
    ComPtr<ID2D1Effect> gaussian, horizontal, vertical;
    ComPtr<ID2D1Bitmap1> input;
    Image output;
    uint64_t revision = ~uint64_t(0), generation = 0;
    float sigmaX = -1, sigmaY = -1;

    void render(ID3D11Device& device, ID2D1DeviceContext& d2d, ID2D1Bitmap1& source, float x, float y,
                uint64_t content) {
        const auto size = source.GetPixelSize();
        const bool resized = ensure(output, device, d2d, int(size.width), int(size.height), false);
        if (!resized && input.Get() == &source && revision == content && sigmaX == x && sigmaY == y) {
            return;
        }
        input = &source;
        ID2D1Effect* final = nullptr;
        if (std::abs(x - y) < .0001f) {
            if (!gaussian) {
                winrt::check_hresult(d2d.CreateEffect(CLSID_D2D1GaussianBlur, &gaussian));
                winrt::check_hresult(gaussian->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD));
            }
            gaussian->SetInput(0, input.Get());
            winrt::check_hresult(gaussian->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, x));
            final = gaussian.Get();
        } else {
            if (!horizontal) {
                winrt::check_hresult(d2d.CreateEffect(CLSID_D2D1DirectionalBlur, &horizontal));
                winrt::check_hresult(d2d.CreateEffect(CLSID_D2D1DirectionalBlur, &vertical));
                winrt::check_hresult(
                    horizontal->SetValue(D2D1_DIRECTIONALBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD));
                winrt::check_hresult(vertical->SetValue(D2D1_DIRECTIONALBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD));
                winrt::check_hresult(vertical->SetValue(D2D1_DIRECTIONALBLUR_PROP_ANGLE, 90.f));
            }
            horizontal->SetInput(0, input.Get());
            vertical->SetInputEffect(0, horizontal.Get());
            winrt::check_hresult(horizontal->SetValue(D2D1_DIRECTIONALBLUR_PROP_STANDARD_DEVIATION, x));
            winrt::check_hresult(vertical->SetValue(D2D1_DIRECTIONALBLUR_PROP_STANDARD_DEVIATION, y));
            final = vertical.Get();
        }
        const auto area = D2D1::RectF(0, 0, float(size.width), float(size.height));
        winrt::check_hresult(d2d.InvalidateEffectInputRectangle(final, 0, &area));
        d2d.SetTarget(output.bitmap.Get());
        d2d.SetTransform(D2D1::Matrix3x2F::Identity());
        d2d.BeginDraw();
        d2d.Clear();
        d2d.DrawImage(final, D2D1::Point2F(), area, D2D1_INTERPOLATION_MODE_LINEAR, D2D1_COMPOSITE_MODE_SOURCE_COPY);
        const auto result = d2d.EndDraw();
        d2d.SetTarget(nullptr);
        winrt::check_hresult(result);
        revision = content;
        sigmaX = x;
        sigmaY = y;
        ++generation;
    }
};

ComPtr<ID3D11ShaderResourceView> displacementView(ID3D11Device& device, const uint8_t* data, int width, int height) {
    D3D11_TEXTURE2D_DESC description{};
    description.Width = UINT(width);
    description.Height = UINT(height);
    description.MipLevels = description.ArraySize = description.SampleDesc.Count = 1;
    description.Format = DXGI_FORMAT_R8G8_UNORM;
    description.Usage = D3D11_USAGE_IMMUTABLE;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const D3D11_SUBRESOURCE_DATA initial{data, UINT(width * 2), 0};
    ComPtr<ID3D11Texture2D> texture;
    winrt::check_hresult(device.CreateTexture2D(&description, &initial, &texture));
    ComPtr<ID3D11ShaderResourceView> view;
    winrt::check_hresult(device.CreateShaderResourceView(texture.Get(), nullptr, &view));
    return view;
}

std::vector<uint8_t> readMask(ID3D11Device& device, ID3D11DeviceContext& context, const Image& image) {
    D3D11_TEXTURE2D_DESC description{};
    image.texture->GetDesc(&description);
    description.BindFlags = 0;
    description.Usage = D3D11_USAGE_STAGING;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    winrt::check_hresult(device.CreateTexture2D(&description, nullptr, &staging));
    context.CopyResource(staging.Get(), image.texture.Get());
    context.Flush();
    std::vector<uint8_t> bytes(size_t(image.width) * image.height * 4);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
    while (true) {
        const auto result = context.Map(staging.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
        if (result != DXGI_ERROR_WAS_STILL_DRAWING) {
            winrt::check_hresult(result);
            break;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            throw std::runtime_error("Generated shadow mask exceeded its readback deadline");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    for (int y = 0; y < image.height; ++y) {
        std::memcpy(bytes.data() + size_t(y) * image.width * 4,
                    static_cast<const uint8_t*>(mapped.pData) + size_t(y) * mapped.RowPitch, size_t(image.width) * 4);
    }
    context.Unmap(staging.Get(), 0);
    return bytes;
}
}  // namespace

struct EffectTarget::Impl {
    ComPtr<IUnknown> sourceIdentity;
    ComPtr<ID3D11ShaderResourceView> sourceView, generatedMap;
    ComPtr<ID2D1Bitmap1> sourceBitmap;
    Image underlay, warped, output, mask, sourceImage;
    Blur background, svg, inset, edge, shadowBlur;
    std::shared_ptr<const ShadowImage> shadow;
    std::array<float, 16> underlayKey{};
    std::array<float, 24> warpKey{};
    std::array<float, 8> maskKey{};
    bool hasUnderlay = false, hasWarp = false, hasMask = false;
    uint64_t inputRevision = 0, underlayRevision = 0, warpedRevision = 0, assetReadback = 0;
    int mapWidth = 0, mapHeight = 0;
};

struct Effect::Impl {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID2D1DeviceContext> d2d;
    ComPtr<ID3D11ComputeShader> underlay, raster, warp, composite, mask;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11SamplerState> sampler;
    std::array<ComPtr<ID3D11ShaderResourceView>, 3> maps;

    ComPtr<ID3D11ComputeShader> compile(const char* entry) {
        ComPtr<ID3DBlob> shader, errors;
        const auto result =
            D3DCompile(kReactMaterialShader, sizeof(kReactMaterialShader) - 1, "LiquidGlass.React", nullptr, nullptr,
                       entry, "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &shader, &errors);
        if (FAILED(result)) {
            const std::string detail =
                errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize())
                       : "React material shader compilation failed";
            throw winrt::hresult_error(result, winrt::to_hstring(detail));
        }
        ComPtr<ID3D11ComputeShader> value;
        winrt::check_hresult(
            device->CreateComputeShader(shader->GetBufferPointer(), shader->GetBufferSize(), nullptr, &value));
        return value;
    }

    void dispatch(ID3D11ComputeShader& shader, Image& target, const Constants& values,
                  std::array<ID3D11ShaderResourceView*, 8> inputs, int width, int height) {
        context->UpdateSubresource(constants.Get(), 0, nullptr, &values, 0, 0);
        ID3D11Buffer* buffers[] = {constants.Get()};
        ID3D11SamplerState* samplers[] = {sampler.Get()};
        ID3D11UnorderedAccessView* outputs[] = {target.unordered.Get()};
        context->CSSetShader(&shader, nullptr, 0);
        context->CSSetConstantBuffers(0, 1, buffers);
        context->CSSetSamplers(0, 1, samplers);
        context->CSSetShaderResources(0, UINT(inputs.size()), inputs.data());
        context->CSSetUnorderedAccessViews(0, 1, outputs, nullptr);
        context->Dispatch((UINT(width) + 15) / 16, (UINT(height) + 15) / 16, 1);
        inputs.fill(nullptr);
        ID3D11UnorderedAccessView* empty[] = {nullptr};
        context->CSSetShaderResources(0, UINT(inputs.size()), inputs.data());
        context->CSSetUnorderedAccessViews(0, 1, empty, nullptr);
        context->CSSetShader(nullptr, nullptr, 0);
    }
};

EffectTarget::EffectTarget() : m_impl(std::make_unique<Impl>()) {}

EffectTarget::~EffectTarget() = default;

void* EffectTarget::bitmap() const {
    return m_impl->output.bitmap.Get();
}

void* EffectTarget::texture() const {
    return m_impl->output.texture.Get();
}

int EffectTarget::width() const {
    return m_impl->output.width;
}

int EffectTarget::height() const {
    return m_impl->output.height;
}

std::shared_ptr<const ShadowImage> EffectTarget::shadowImage() const {
    return m_impl->shadow;
}

uint64_t EffectTarget::assetReadbackBytes() const {
    return m_impl->assetReadback;
}

uint64_t EffectTarget::allocatedBytes() const {
    const auto& value = *m_impl;
    uint64_t bytes = 0;
    for (const auto* image :
         {&value.underlay, &value.warped, &value.output, &value.mask, &value.sourceImage, &value.background.output,
          &value.svg.output, &value.inset.output, &value.edge.output, &value.shadowBlur.output}) {
        bytes += image->bytes();
    }
    return bytes + uint64_t(value.mapWidth) * value.mapHeight * 2;
}

Effect::Effect() : m_impl(std::make_unique<Impl>()) {}

Effect::~Effect() = default;

void Effect::initialize(void* d3dDevice, void* d2dContext) {
    auto& self = *m_impl;
    self.device = static_cast<ID3D11Device*>(d3dDevice);
    self.d2d = static_cast<ID2D1DeviceContext*>(d2dContext);
    self.device->GetImmediateContext(&self.context);
    self.underlay = self.compile("Underlay");
    self.raster = self.compile("RasterSource");
    self.warp = self.compile("Warp");
    self.composite = self.compile("Composite");
    self.mask = self.compile("MaskImage");
    D3D11_BUFFER_DESC description{};
    description.ByteWidth = sizeof(Constants);
    description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    winrt::check_hresult(self.device->CreateBuffer(&description, nullptr, &self.constants));
    D3D11_SAMPLER_DESC sampling{};
    sampling.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampling.AddressU = sampling.AddressV = sampling.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampling.MaxLOD = D3D11_FLOAT32_MAX;
    winrt::check_hresult(self.device->CreateSamplerState(&sampling, &self.sampler));
    const uint8_t* assets[] = {kReactStandard, kReactPolar, kReactProminent};
    for (size_t index = 0; index < self.maps.size(); ++index) {
        self.maps[index] = displacementView(*self.device.Get(), assets[index], 256, 256);
    }
}

void Effect::render(EffectTarget& target, void* texture, EffectGeometry geometry, Parameters p, uint64_t revision) {
    auto& self = *m_impl;
    auto& out = *target.m_impl;
    p.normalize();
    out.assetReadback = 0;
    if (geometry.width < 1 || geometry.height < 1) {
        throw std::invalid_argument("React material needs positive output dimensions");
    }
    const float dpi = std::isfinite(geometry.dpiScale) ? std::clamp(geometry.dpiScale, .25f, 8.f) : 1;
    const float bodyWidth = (p.layoutWidth > 0 ? p.layoutWidth : geometry.width / dpi) * dpi;
    const float bodyHeight = (p.layoutHeight > 0 ? p.layoutHeight : geometry.height / dpi) * dpi;
    const float radius =
        std::isfinite(geometry.corner) ? std::clamp(geometry.corner, 0.f, std::min(bodyWidth, bodyHeight) / 2) : 0;
    const float shadowSigma = (p.overLight ? 35.f : 20.f) * dpi;
    const int pad = int(std::ceil(shadowSigma * 3 + (p.overLight ? 16 : 12) * dpi + 2));
    const int cap = int(std::ceil(radius + shadowSigma * 3));
    const int templateWidth = std::min(int(std::ceil(bodyWidth)), cap * 2 + 1);
    const int templateHeight = std::min(int(std::ceil(bodyHeight)), cap * 2 + 1);
    const float svgSigma = std::max(.1f, .5f - p.aberrationIntensity * .1f) * dpi;
    const int svgPadX = int(std::ceil(svgSigma * 3)) + 1;
    const int svgPadY = int(std::ceil(svgSigma * 3)) + 1;
    auto& source = *static_cast<ID3D11Texture2D*>(texture);
    D3D11_TEXTURE2D_DESC sourceDescription{};
    source.GetDesc(&sourceDescription);
    ComPtr<IUnknown> identity;
    winrt::check_hresult(source.QueryInterface(IID_PPV_ARGS(&identity)));
    const bool rebound = identity.Get() != out.sourceIdentity.Get();
    if (rebound) {
        out.sourceIdentity = std::move(identity);
        out.sourceView.Reset();
        winrt::check_hresult(self.device->CreateShaderResourceView(&source, nullptr, &out.sourceView));
        out.sourceBitmap =
            bitmap(*self.d2d.Get(), source, sourceDescription.Format, D2D1_BITMAP_OPTIONS_NONE, D2D1_ALPHA_MODE_IGNORE);
        out.hasUnderlay = out.hasWarp = false;
    }
    ensure(out.output, *self.device.Get(), *self.d2d.Get(), geometry.width, geometry.height, true, true);
    ensure(out.sourceImage, *self.device.Get(), *self.d2d.Get(), int(std::ceil(bodyWidth)), int(std::ceil(bodyHeight)),
           true);
    ensure(out.warped, *self.device.Get(), *self.d2d.Get(), int(std::ceil(bodyWidth)) + 2 * svgPadX,
           int(std::ceil(bodyHeight)) + 2 * svgPadY, true, true);
    Constants c{{float(sourceDescription.Width), float(sourceDescription.Height), geometry.sourceX, geometry.sourceY},
                {bodyWidth, bodyHeight, radius, dpi},
                {p.displacementScale * (p.overLight ? .5f : 1.f) * dpi, p.saturation / 100, p.aberrationIntensity,
                 float(p.mode)},
                {p.scaleX, p.scaleY, p.offsetX * dpi, p.offsetY * dpi},
                {p.mouseOffsetX, p.mouseOffsetY, p.hoverGlowOpacity, p.pressGlowOpacity},
                {p.interactionGlowOpacity, p.overLight ? 1.f : 0.f, 0, 0},
                {float(geometry.width), float(geometry.height), float(svgPadX), float(svgPadY)},
                {float(templateWidth), float(templateHeight), float(pad), geometry.attachedTop ? 1.f : 0.f},
                {float(std::min(cap, templateWidth / 2)), float(std::min(cap, templateHeight / 2)),
                 float(templateWidth + pad * 2), float(templateHeight + pad * 2)},
                {float(out.warped.width), float(out.warped.height), float(out.sourceImage.width),
                 float(out.sourceImage.height)}};
    const std::array<float, 8> maskKey{
        float(templateWidth), float(templateHeight), radius, dpi, shadowSigma, float(pad), c.mask[3], 0};
    if (!out.hasMask || out.maskKey != maskKey) {
        ensure(out.mask, *self.device.Get(), *self.d2d.Get(), templateWidth + pad * 2, templateHeight + pad * 2, true);
        self.dispatch(*self.mask.Get(), out.mask, c, {}, out.mask.width, out.mask.height);
        const uint64_t maskRevision = ++out.inputRevision;
        out.inset.render(*self.device.Get(), *self.d2d.Get(), *out.mask.bitmap.Get(), 1.5f * dpi, 1.5f * dpi,
                         maskRevision);
        out.edge.render(*self.device.Get(), *self.d2d.Get(), *out.mask.bitmap.Get(), 2 * dpi, 2 * dpi, maskRevision);
        out.shadowBlur.render(*self.device.Get(), *self.d2d.Get(), *out.mask.bitmap.Get(), shadowSigma, shadowSigma,
                              maskRevision);
        auto shadow = std::make_shared<ShadowImage>();
        shadow->width = out.shadowBlur.output.width;
        shadow->height = out.shadowBlur.output.height;
        shadow->bodyWidth = templateWidth;
        shadow->bodyHeight = templateHeight;
        shadow->padding = pad;
        shadow->capX = pad + std::min(cap, templateWidth / 2);
        shadow->capY = pad + std::min(cap, templateHeight / 2);
        shadow->dpi = dpi;
        shadow->offsetY = p.overLight ? 16.f : 12.f;
        shadow->opacity = p.overLight ? .75f : .25f;
        shadow->bgra = readMask(*self.device.Get(), *self.context.Get(), out.shadowBlur.output);
        out.assetReadback = shadow->bgra.size();
        out.shadow = std::move(shadow);
        out.maskKey = maskKey;
        out.hasMask = true;
    }
    ID3D11ShaderResourceView* rawView = out.sourceView.Get();
    ID2D1Bitmap1* blurInput = out.sourceBitmap.Get();
    uint64_t backgroundRevision = revision;
    const std::array<float, 16> underlayKey{c.panel[0], c.panel[1], c.panel[2], c.panel[3], c.pose[0], c.pose[1],
                                            c.pose[2],  c.pose[3],  c.input[2], c.input[3], c.mask[3], c.glow[1],
                                            0,          0,          0,          0};
    {
        const bool resized = ensure(out.underlay, *self.device.Get(), *self.d2d.Get(), int(sourceDescription.Width),
                                    int(sourceDescription.Height), true);
        if (resized || !out.hasUnderlay || out.underlayKey != underlayKey || out.underlayRevision != revision) {
            self.dispatch(*self.underlay.Get(), out.underlay, c,
                          {nullptr, nullptr, nullptr, out.mask.view.Get(), out.shadowBlur.output.view.Get(), rawView},
                          out.underlay.width, out.underlay.height);
            out.underlayKey = underlayKey;
            out.underlayRevision = revision;
            out.hasUnderlay = true;
            ++out.inputRevision;
        }
        rawView = out.underlay.view.Get();
        blurInput = out.underlay.bitmap.Get();
        backgroundRevision = out.inputRevision;
    }
    const float sigma = p.blurSigma() * dpi;
    out.background.render(*self.device.Get(), *self.d2d.Get(), *blurInput, sigma * p.scaleX, sigma * p.scaleY,
                          backgroundRevision);
    ID3D11ShaderResourceView* map = nullptr;
    if (p.mode == react::RefractionMode::Shader) {
        const int width = std::max(1, int(bodyWidth / dpi)), height = std::max(1, int(bodyHeight / dpi));
        if (width != out.mapWidth || height != out.mapHeight) {
            const auto pixels = react::shaderDisplacement(width, height);
            out.generatedMap = displacementView(*self.device.Get(), pixels.data(), width, height);
            out.mapWidth = width;
            out.mapHeight = height;
        }
        map = out.generatedMap.Get();
    } else {
        map = self.maps[size_t(p.mode)].Get();
    }
    const std::array<float, 24> warpKey{c.panel[0],
                                        c.panel[1],
                                        c.panel[2],
                                        c.panel[3],
                                        c.pose[0],
                                        c.pose[1],
                                        c.pose[2],
                                        c.pose[3],
                                        c.optics[0],
                                        c.optics[1],
                                        c.optics[2],
                                        c.optics[3],
                                        c.input[2],
                                        c.input[3],
                                        c.frame[0],
                                        c.frame[1],
                                        c.frame[2],
                                        c.frame[3],
                                        float(out.background.generation),
                                        float(out.warped.width),
                                        float(out.warped.height),
                                        0,
                                        0,
                                        0};
    if (!out.hasWarp || out.warpKey != warpKey) {
        self.dispatch(*self.raster.Get(), out.sourceImage, c, {out.background.output.view.Get()}, out.sourceImage.width,
                      out.sourceImage.height);
        self.dispatch(*self.warp.Get(), out.warped, c,
                      {nullptr, map, nullptr, nullptr, nullptr, nullptr, out.sourceImage.view.Get()}, out.warped.width,
                      out.warped.height);
        ++out.warpedRevision;
        out.warpKey = warpKey;
        out.hasWarp = true;
    }
    out.svg.render(*self.device.Get(), *self.d2d.Get(), *out.warped.bitmap.Get(), svgSigma, svgSigma,
                   out.warpedRevision);
    self.dispatch(*self.composite.Get(), out.output, c,
                  {out.background.output.view.Get(), map, out.svg.output.view.Get(), out.inset.output.view.Get(),
                   out.edge.output.view.Get(), rawView, out.sourceImage.view.Get(), out.sourceView.Get()},
                  geometry.width, geometry.height);
}
}  // namespace liquidglass::react
