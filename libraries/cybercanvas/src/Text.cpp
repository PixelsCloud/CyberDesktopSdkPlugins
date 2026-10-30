#include "CyberCanvas/Text.h"

#include <dwrite_2.h>
#include <wrl/implements.h>

#include <algorithm>
#include <thread>

#include "CyberCanvas/Canvas.h"

namespace cybercanvas {
namespace {
using Microsoft::WRL::ClassicCom;
using Microsoft::WRL::Make;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;

class Ink final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IUnknown> {
public:
    TextInk value;

    explicit Ink(TextInk value) : value(std::move(value)) {}
};

class InlineImage final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IDWriteInlineObject> {
public:
    explicit InlineImage(const TextSpan& span)
        : image_(span.icon), size_(span.iconSize), baseline_(span.iconBaseline) {}

    HRESULT STDMETHODCALLTYPE Draw(void* context, IDWriteTextRenderer*, FLOAT x, FLOAT y, BOOL sideways, BOOL,
                                   IUnknown*) override {
        if (!context || sideways) {
            return E_INVALIDARG;
        }
        try {
            static_cast<Canvas*>(context)->image(*image_, {x, y, size_.width, size_.height});
            return S_OK;
        } catch (...) {
            return E_FAIL;
        }
    }

    HRESULT STDMETHODCALLTYPE GetMetrics(DWRITE_INLINE_OBJECT_METRICS* metrics) override {
        *metrics = {float(size_.width), float(size_.height), float(baseline_), FALSE};
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetOverhangMetrics(DWRITE_OVERHANG_METRICS* metrics) override {
        *metrics = {};
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetBreakConditions(DWRITE_BREAK_CONDITION* before,
                                                 DWRITE_BREAK_CONDITION* after) override {
        *before = *after = DWRITE_BREAK_CONDITION_NEUTRAL;
        return S_OK;
    }

private:
    std::shared_ptr<const ImageData> image_;
    Size size_;
    double baseline_;
};

class Renderer final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IDWriteTextRenderer> {
public:
    Renderer(Canvas& canvas, double lastBaseline, double visibleBottom)
        : canvas_(canvas), lastBaseline_(lastBaseline), visibleBottom_(visibleBottom) {
        canvas.nativeTextFactory()->QueryInterface(IID_PPV_ARGS(colorFactory_.GetAddressOf()));
    }

    HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void*, BOOL* disabled) override {
        *disabled = FALSE;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetCurrentTransform(void*, DWRITE_MATRIX* matrix) override {
        D2D1_MATRIX_3X2_F transform;
        canvas_.target->GetTransform(&transform);
        *matrix = {transform._11, transform._12, transform._21, transform._22, transform._31, transform._32};
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void*, FLOAT* pixels) override {
        float y;
        canvas_.target->GetDpi(pixels, &y);
        *pixels /= 96;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DrawGlyphRun(void*, FLOAT x, FLOAT y, DWRITE_MEASURING_MODE mode,
                                           const DWRITE_GLYPH_RUN* run, const DWRITE_GLYPH_RUN_DESCRIPTION* description,
                                           IUnknown* effect) override {
        if (y > lastBaseline_ + 1 / canvas_.devicePixelRatio()) {
            return S_OK;
        }
        try {
            const auto ink = effect ? static_cast<Ink*>(effect)->value : TextInk{};
            ComPtr<ID2D1SolidColorBrush> solid;
            require(canvas_.target->CreateSolidColorBrush(color(ink.color), solid.GetAddressOf()));
            ComPtr<IDWriteColorGlyphRunEnumerator> layers;
            if (colorFactory_ && SUCCEEDED(colorFactory_->TranslateColorGlyphRun(x, y, run, description, mode, nullptr,
                                                                                 0, layers.GetAddressOf()))) {
                BOOL more = FALSE;
                while (SUCCEEDED(layers->MoveNext(&more)) && more) {
                    const DWRITE_COLOR_GLYPH_RUN* layer = nullptr;
                    require(layers->GetCurrentRun(&layer));
                    const auto c = layer->runColor;
                    solid->SetColor(layer->paletteIndex == 0xffff
                                        ? color(ink.color)
                                        : D2D1::ColorF(c.r, c.g, c.b, c.a * color(ink.color).a));
                    canvas_.target->DrawGlyphRun({layer->baselineOriginX, layer->baselineOriginY}, &layer->glyphRun,
                                                 solid.Get(), mode);
                }
                return S_OK;
            }
            if (!ink.bottomColor && ink.outlineWidth == 0) {
                canvas_.target->DrawGlyphRun({x, y}, run, solid.Get(), mode);
                return S_OK;
            }
            ComPtr<ID2D1PathGeometry> geometry;
            ComPtr<ID2D1GeometrySink> sink;
            require(canvas_.factory->CreatePathGeometry(geometry.GetAddressOf()));
            require(geometry->Open(sink.GetAddressOf()));
            const auto outline = run->fontFace->GetGlyphRunOutline(
                run->fontEmSize, run->glyphIndices, run->glyphAdvances, run->glyphOffsets, run->glyphCount,
                run->isSideways, (run->bidiLevel & 1) != 0, sink.Get());
            require(sink->Close());
            if (FAILED(outline)) {
                canvas_.target->DrawGlyphRun({x, y}, run, solid.Get(), mode);
                return S_OK;
            }
            canvas_.saved([&] {
                canvas_.translate(x, y);
                if (ink.outlineWidth > 0) {
                    solid->SetColor(color(ink.outlineColor));
                    canvas_.target->DrawGeometry(geometry.Get(), solid.Get(), float(ink.outlineWidth * 2));
                    solid->SetColor(color(ink.color));
                }
                if (ink.bottomColor) {
                    D2D1_RECT_F bounds;
                    require(geometry->GetBounds(nullptr, &bounds));
                    const D2D1_GRADIENT_STOP stops[] = {{0, color(ink.color)}, {1, color(*ink.bottomColor)}};
                    auto gradient =
                        canvas_.linearGradient({0, bounds.top}, {0, std::max(bounds.top + 1, bounds.bottom)}, stops);
                    canvas_.target->FillGeometry(geometry.Get(), gradient.Get());
                } else {
                    canvas_.target->FillGeometry(geometry.Get(), solid.Get());
                }
            });
            return S_OK;
        } catch (...) {
            return E_FAIL;
        }
    }

    HRESULT STDMETHODCALLTYPE DrawUnderline(void*, FLOAT x, FLOAT y, const DWRITE_UNDERLINE* line,
                                            IUnknown* effect) override {
        if (y > lastBaseline_ + 1 / canvas_.devicePixelRatio()) {
            return S_OK;
        }
        return decoration(x, y + line->offset, line->width, line->thickness, effect);
    }

    HRESULT STDMETHODCALLTYPE DrawStrikethrough(void*, FLOAT x, FLOAT y, const DWRITE_STRIKETHROUGH* line,
                                                IUnknown* effect) override {
        if (y > lastBaseline_ + 1 / canvas_.devicePixelRatio()) {
            return S_OK;
        }
        return decoration(x, y + line->offset, line->width, line->thickness, effect);
    }

    HRESULT STDMETHODCALLTYPE DrawInlineObject(void* context, FLOAT x, FLOAT y, IDWriteInlineObject* object,
                                               BOOL sideways, BOOL rtl, IUnknown* effect) override {
        if (y >= visibleBottom_) {
            return S_OK;
        }
        return object->Draw(context, this, x, y, sideways, rtl, effect);
    }

private:
    HRESULT decoration(float x, float y, float width, float height, IUnknown* effect) {
        ComPtr<ID2D1SolidColorBrush> brush;
        const auto ink = effect ? static_cast<Ink*>(effect)->value : TextInk{};
        const auto status = canvas_.target->CreateSolidColorBrush(color(ink.color), brush.GetAddressOf());
        if (SUCCEEDED(status)) {
            canvas_.target->FillRectangle({x, y, x + width, y + height}, brush.Get());
        }
        return status;
    }

    Canvas& canvas_;
    double lastBaseline_, visibleBottom_;
    ComPtr<IDWriteFactory2> colorFactory_;
};

void extent(double value, double minimum, double maximum) {
    if (!std::isfinite(value) || value < minimum || value > maximum) {
        throw std::invalid_argument("Invalid styled text extent");
    }
}
}  // namespace

struct StyledText::Impl {
    struct Link {
        DWRITE_TEXT_RANGE range;
        std::string id;
    };

    std::thread::id owner = std::this_thread::get_id();
    std::wstring value;
    StyledTextOptions options;
    ComPtr<IDWriteTextLayout> layout;
    std::vector<Link> links;
    double visibleHeight = 0, lastBaseline = 0;
    uint32_t visibleLines = 0;
    mutable std::shared_ptr<const ImageData> cached;
    mutable double cachedDpi = 0, cachedPadding = 0;

    void check() const {
        if (owner != std::this_thread::get_id()) {
            throw std::logic_error("StyledText belongs to another thread");
        }
    }
};

StyledText::StyledText(Canvas& canvas, std::span<const TextSpan> spans, StyledTextOptions options)
    : impl_(std::make_unique<Impl>()) {
    extent(options.available.width, 1, 4096);
    extent(options.available.height, 1, 4096);
    if (spans.size() > 1024 || options.maximumLines > 4096) {
        throw std::length_error("Styled text span/line limit exceeded");
    }
    auto& p = *impl_;
    p.options = options;
    for (const auto& span : spans) {
        if (p.value.size() + span.text.size() + (span.icon ? 1 : 0) > 65536) {
            throw std::length_error("Styled text length limit exceeded");
        }
        p.value += span.icon ? L"\xfffc" : span.text;
    }
    // Canvas supplies identical paragraph defaults, automatic bidi and custom fonts.
    auto paragraph = options.paragraph;
    paragraph.vertical = VerticalAlign::Top;
    const auto templateLayout = canvas.nativeTextLayout(p.value, paragraph, options.available);
    require(canvas.nativeTextFactory()->CreateTextLayout(p.value.data(), UINT32(p.value.size()), templateLayout.Get(),
                                                         float(options.available.width),
                                                         float(options.available.height), p.layout.GetAddressOf()));
    uint32_t position = 0;
    for (const auto& span : spans) {
        extent(span.style.size, 1, 4096);
        extent(span.ink.outlineWidth, 0, 32);
        const DWRITE_TEXT_RANGE range{position, uint32_t(span.icon ? 1 : span.text.size())};
        position += range.length;
        if (!range.length) {
            continue;
        }
        require(p.layout->SetFontFamilyName(span.style.family.c_str(), range));
        require(p.layout->SetFontSize(float(span.style.size), range));
        require(
            p.layout->SetFontWeight(DWRITE_FONT_WEIGHT(span.style.bold ? FontWeight::Bold : span.style.weight), range));
        require(p.layout->SetLocaleName(span.style.locale.c_str(), range));
        extent(span.style.characterSpacing, -4096, 4096);
        if (span.style.characterSpacing != 0) {
            ComPtr<IDWriteTextLayout1> spacing;
            require(p.layout.As(&spacing));
            require(spacing->SetCharacterSpacing(float(span.style.characterSpacing), float(span.style.characterSpacing),
                                                 0, range));
        }
        if (span.style.tabularFigures) {
            ComPtr<IDWriteTypography> typography;
            require(canvas.nativeTextFactory()->CreateTypography(typography.GetAddressOf()));
            require(typography->AddFontFeature({DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES, 1}));
            require(p.layout->SetTypography(typography.Get(), range));
        }
        require(p.layout->SetUnderline(span.underline, range));
        const auto ink = Make<Ink>(span.ink);
        if (!ink) {
            throw std::bad_alloc();
        }
        require(p.layout->SetDrawingEffect(ink.Get(), range));
        if (span.icon) {
            extent(span.iconSize.width, 1, 4096);
            extent(span.iconSize.height, 1, 4096);
            extent(span.iconBaseline, 0, span.iconSize.height);
            const auto icon = Make<InlineImage>(span);
            if (!icon) {
                throw std::bad_alloc();
            }
            require(p.layout->SetInlineObject(icon.Get(), range));
        }
        if (!span.link.empty()) {
            p.links.push_back({range, span.link});
        }
    }
    UINT32 count = 0;
    p.layout->GetLineMetrics(nullptr, 0, &count);
    std::vector<DWRITE_LINE_METRICS> lines(count);
    require(p.layout->GetLineMetrics(lines.data(), count, &count));
    for (const auto& line : lines) {
        if ((options.maximumLines && p.visibleLines >= options.maximumLines) ||
            (p.visibleLines && p.visibleHeight + line.height > options.available.height)) {
            break;
        }
        p.lastBaseline = p.visibleHeight + line.baseline;
        p.visibleHeight += line.height;
        ++p.visibleLines;
    }
    p.visibleHeight = std::min(p.visibleHeight, options.available.height);
    require(p.layout->SetMaxHeight(float(std::max(1., p.visibleHeight))));
}

StyledText::~StyledText() = default;

TextMetrics StyledText::metrics() const {
    impl_->check();
    DWRITE_TEXT_METRICS metrics;
    require(impl_->layout->GetMetrics(&metrics));
    return {metrics.width, impl_->visibleHeight, metrics.widthIncludingTrailingWhitespace, impl_->visibleLines};
}

std::wstring StyledText::text() const {
    impl_->check();
    return impl_->value;
}

std::optional<TextHit> StyledText::hit(Point local) const {
    impl_->check();
    if (!std::isfinite(local.x) || !std::isfinite(local.y) || local.x < 0 || local.y < 0 ||
        local.x >= impl_->options.available.width || local.y >= impl_->visibleHeight) {
        return std::nullopt;
    }
    BOOL trailing = FALSE, inside = FALSE;
    DWRITE_HIT_TEST_METRICS metrics;
    require(impl_->layout->HitTestPoint(float(local.x), float(local.y), &trailing, &inside, &metrics));
    if (!inside || metrics.isTrimmed) {
        return std::nullopt;
    }
    TextHit result{metrics.textPosition, trailing != FALSE};
    for (const auto& link : impl_->links) {
        if (result.position >= link.range.startPosition &&
            result.position < link.range.startPosition + link.range.length) {
            result.link = link.id;
            break;
        }
    }
    return result;
}

std::vector<Rect> StyledText::linkBounds(const std::string& id) const {
    impl_->check();
    std::vector<Rect> result;
    for (const auto& link : impl_->links) {
        if (link.id != id) {
            continue;
        }
        UINT32 count = 0;
        impl_->layout->HitTestTextRange(link.range.startPosition, link.range.length, 0, 0, nullptr, 0, &count);
        std::vector<DWRITE_HIT_TEST_METRICS> bounds(count);
        require(impl_->layout->HitTestTextRange(link.range.startPosition, link.range.length, 0, 0, bounds.data(), count,
                                                &count));
        for (const auto& b : bounds) {
            if (!b.isTrimmed && b.top < impl_->visibleHeight) {
                result.push_back({b.left, b.top, b.width, std::min(double(b.height), impl_->visibleHeight - b.top)});
            }
        }
    }
    return result;
}

void StyledText::draw(Canvas& canvas, Point origin) const {
    impl_->check();
    if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !canvas.target) {
        throw std::invalid_argument("Invalid styled text destination");
    }
    const auto renderer = Make<Renderer>(canvas, origin.y + impl_->lastBaseline, origin.y + impl_->visibleHeight);
    if (!renderer) {
        throw std::bad_alloc();
    }
    canvas.clipped({origin.x - 32, origin.y - 32, impl_->options.available.width + 64, impl_->visibleHeight + 64}, [&] {
        require(impl_->layout->Draw(&canvas, renderer.Get(), float(origin.x), float(origin.y)));
    });
}

std::shared_ptr<const ImageData> StyledText::snapshot(double dpi, double padding) const {
    impl_->check();
    extent(dpi, .5, 4);
    extent(padding, 0, 64);
    if (impl_->cached && impl_->cachedDpi == dpi && impl_->cachedPadding == padding) {
        return impl_->cached;
    }
    const auto width = uint32_t(std::ceil((impl_->options.available.width + padding * 2) * dpi));
    const auto height = uint32_t(std::ceil((std::max(1., impl_->visibleHeight) + padding * 2) * dpi));
    if (width > 4096 || height > 4096 || uint64_t(width) * height * 4 > 16 * 1024 * 1024) {
        throw std::length_error("Styled text snapshot budget exceeded");
    }
    std::vector<uint8_t> pixels(size_t(width) * height * 4);
    Canvas canvas;
    canvas.render({width, height, width * 4, pixels.size(), pixels.data(), dpi}, {}, [&](Canvas& target, Rect) {
        draw(target, {padding, padding});
    });
    for (size_t i = 0; i < pixels.size(); i += 4) {
        std::swap(pixels[i], pixels[i + 2]);
    }
    auto result = ImageData::fromPremultipliedBgra(width, height, std::move(pixels));
    impl_->cached = result;
    impl_->cachedDpi = dpi;
    impl_->cachedPadding = padding;
    return result;
}
}  // namespace cybercanvas
