#include <sdk/Language.h>
#include <sdk/WindowsFonts.h>

#include <algorithm>
#include <cmath>
#include <optional>

#include "plugins/air-quality-suite/MechanicalView.h"
#include "plugins/pixel-desk/PixelAir.h"
#include "plugins/pixel-desk/PixelDigits.h"
#include "plugins/pixel-desk/PixelImages.h"
#include "plugins/pixel-desk/PixelLayout.h"
#include "plugins/pixel-desk/PixelParts.h"

namespace pixeldesk {
using cybercanvas::Canvas;
using cybercanvas::Rect;
using Microsoft::WRL::ComPtr;

namespace {
uint32_t levelInk(int level, bool dark) {
    constexpr uint32_t bright[]{0x95e6b4ff, 0xe5d46aff, 0xe9ad5aff, 0xd87458ff, 0xb487bdff, 0xb86c80ff};
    constexpr uint32_t deep[]{0x287c60ff, 0x796c31ff, 0x895c35ff, 0x853f49ff, 0x654d84ff, 0x743b58ff};
    return dark ? bright[std::clamp(level, 0, 5)] : deep[std::clamp(level, 0, 5)];
}

std::wstring quality(int level, bool english) {
    auto text = cdsdk::displayText(airplugin::levelName(level, english));
    if (english) {
        return text;
    }
    if (level == 0) {
        return cdsdk::displayText(L"优");
    }
    if (text.size() > 3) {
        text.insert((text.size() + 1) / 2, 1, L'\n');
    }
    return text;
}

struct FaceLayout {
    Rect title, city, aqi, badge, bar, leaf;
    std::array<Rect, 2> pollutantNames{}, pollutantValues{};
    Rect canisterLeaf{};
};

FaceLayout faceLayout(const AirSpec& spec, bool english = false) {
    FaceLayout layout;
    if (spec.columns == 2) {
        layout = {{}, {34, 37, 120, 32}, {126, 86, 146, 93}, {286, 101, 65, 78}, {45, 201, 305, 19}, {45, 95, 68, 68}};
    } else if (spec.rows == 1) {
        layout = {{88, 44, 76, 36},
                  {88, 153, 90, 32},
                  {181, 60, 172, 112},
                  {365, 89, 110, 83},
                  {88, 187, 366, 20},
                  {90, 92, 66, 58},
                  {{{520, 48, 205, 43}, {520, 134, 205, 43}}},
                  {{{733, 45, 100, 58}, {733, 131, 100, 58}}}};
    } else {
        layout = {{160, 150, 80, 34},
                  {157, 284, 105, 34},
                  {253, 168, 164, 113},
                  {427, 210, 64, 81},
                  {163, 331, 316, 20},
                  {160, 201, 72, 73},
                  {{{510, 168, 170, 47}, {510, 257, 170, 47}}},
                  {{{692, 163, 100, 64}, {692, 252, 100, 64}}},
                  {25, 251, 48, 44}};
    }
    if (english) {
        if (spec.columns == 2) {
            layout.city = {34, 37, 320, 32};
            layout.aqi = {138, 85, 172, 80};
            layout.badge = {35, 170, 320, 27};
        } else if (spec.rows == 1) {
            layout.city = {177, 42, 298, 29};
            layout.aqi = {181, 70, 172, 78};
            layout.badge = {85, 153, 390, 28};
        } else {
            layout.city = {250, 149, 233, 32};
            layout.aqi = {253, 193, 164, 91};
            layout.badge = {149, 294, 338, 28};
        }
    }
    return layout;
}

airplugin::Snapshot visibleSnapshot(const airplugin::Snapshot& source) {
    airplugin::Snapshot result;
    result.valid = source.valid;
    result.cached = source.cached;
    result.error = source.error;
    result.city = source.city;
    result.aqi = source.aqi;
    result.pollutants = source.pollutants;
    return result;
}
}  // namespace

struct AirRenderer::Impl {
    struct Key {
        double width, height, dpi;
        airplugin::Snapshot snapshot;
        std::string font, language, palette;
        bool loading;
        const cybercanvas::ImageData* body;
        bool operator==(const Key&) const = default;
    };

    const AirSpec& spec;
    cdsdk::Canvas canvas;
    cdsdk::Images images;
    cdsdk::WindowsFonts fonts{canvas.nativeTextFactory()};
    std::filesystem::path path;
    std::shared_ptr<const cybercanvas::ImageData> body;
    std::optional<Key> key;
    ComPtr<ID2D1RenderTarget> owner;
    cybercanvas::SurfaceImage face;
    uint64_t builds = 0;

    Impl(const CdCreateContext& context, const AirSpec& value) : spec(value) {
        images.bind(context);
        canvas.bindGpu(context);
        path = std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(context.plugin_directory))) /
               "assets/body.png";
    }

    void label(Canvas& layer, std::wstring text, Rect area, double size, uint32_t ink, bool ellipsis = false) {
        if (text.empty() || area.width <= 0 || area.height <= 0) {
            return;
        }
        cybercanvas::TextStyle style;
        style.family = fonts.family();
        style.locale = cdsdk::drawingLocale();
        style.size = size;
        style.bold = true;
        style.tabularFigures = true;
        style.align = cybercanvas::TextAlign::Center;
        style.vertical = cybercanvas::VerticalAlign::Center;
        style.ellipsis = ellipsis;
        if (ellipsis) {
            layer.text(text, style, area, ink);
        } else {
            const auto fit = layer.fitTextInk(text, style, area);
            layer.brush->SetColor(cybercanvas::color(ink));
            layer.brush->SetOpacity(1);
            layer.target->DrawTextLayout(D2D1::Point2F(float(fit.origin.x), float(fit.origin.y)), fit.layout.Get(),
                                         layer.brush.Get());
        }
    }

    void content(Canvas& layer) {
        const auto& value = key->snapshot;
        const bool english = key->language == "en", dark = key->palette == "dark";
        const auto g = faceLayout(spec, english);
        const auto ink = dark ? kCream : kInk;
        if (body) {
            layer.image(*body, {-spec.visible.x, -spec.visible.y, spec.source.width, spec.source.height},
                        {.fit = cybercanvas::ImageFit::Stretch,
                         .clip = false,
                         .sampling = cybercanvas::ImageSampling::Nearest});
        }
        if (!dark) {
            const Rect glass = spec.columns == 2 ? Rect{34, 35, 326, 184}
                               : spec.rows == 1  ? Rect{65, 37, 778, 182}
                                                 : Rect{142, 144, 666, 213};
            stepped(layer, glass, 10, kCream);
        }
        label(layer, L"AQI", g.title, 30, ink);
        const auto city = value.city.empty() ? (spec.columns == 2 ? (english ? L"Air quality" : L"空气质量") : L"--")
                                             : airplugin::wide(value.city);
        auto cityArea = g.city;
        if (english && value.valid && (value.cached || key->loading)) {
            cityArea.width = spec.columns == 2 ? 234 : spec.rows == 1 ? 198 : 162;
        }
        label(layer, city, cityArea, spec.columns == 2 ? 27 : 30, ink, true);
        const int level = airplugin::level(value.aqi);
        const auto accent = levelInk(level, dark);
        pixelDigits(layer, value.valid ? std::to_string(std::llround(value.aqi)) : "--", g.aqi,
                    value.valid ? accent : ink, true);
        const auto status =
            value.valid    ? quality(level, english)
            : key->loading ? (english ? L"Loading" : L"获取中")
            : value.error == airplugin::Error::CredentialsMissing || value.error == airplugin::Error::Unauthorized
                ? (english ? L"Setup" : L"未配置")
            : value.error == airplugin::Error::LocationUnavailable ? (english ? L"Location" : L"未定位")
                                                                   : (english ? L"No data" : L"不可用");
        label(layer, status, g.badge, english ? 24 : spec.columns == 2 ? 43 : 49, ink);
        if (value.valid && (value.cached || key->loading)) {
            const Rect mark = spec.columns == 2 ? Rect{276, 41, 76, 25}
                              : spec.rows == 1  ? Rect{383, 47, 90, 25}
                                                : Rect{420, 157, 68, 28};
            label(layer, key->loading ? (english ? L"Wait" : L"更新") : (english ? L"Cache" : L"缓存"), mark, 18,
                  dark ? kAmber : 0x895c35ff);
        }
        stepped(layer, g.bar, 3, kInk);
        layer.strokeBox(g.bar, 2, dark ? 0x76969dff : 0x6e8176ff, {.width = 2});
        if (spec.columns == 2 && !english) {
            for (double y = 86; y < 182; y += 10) {
                layer.line(282, y, 282, y + 5, dark ? 0x9aadb4aa : 0x77887fff, 1.3);
            }
        }
        const double segment = (g.bar.width - 6) / 6;
        for (unsigned i = 0; i < 6; ++i) {
            layer.box(g.bar.x + 3 + i * segment, g.bar.y + 3, segment - 2, g.bar.height - 6, 0,
                      value.valid ? levelInk(int(i), true) : 0x405867ff);
        }
        if (value.valid) {
            const double fraction = std::clamp((airplugin::mechanicalAqiAngle(value.aqi) - 140) / 260, 0., 1.);
            const double x = g.bar.x + 4 + (g.bar.width - 8) * fraction;
            auto pointer = layer.path();
            pointer.moveTo({x - 7, g.bar.y - 10}).lineTo({x + 7, g.bar.y - 10}).lineTo({x, g.bar.y - 1}).close();
            layer.fill(pointer, ink);
        }
        if (spec.columns > 2) {
            const double divider = spec.rows == 1 ? 494 : 496;
            const double top = spec.rows == 1 ? 41 : 154, bottom = spec.rows == 1 ? 219 : 346;
            layer.line(divider, top, divider, bottom, dark ? 0x668d8fff : 0xa5b2a6ff, 2);
            for (unsigned i = 0; i < 2; ++i) {
                const auto name = std::wstring(i ? L"PM10" : L"PM2.5") + (english ? L" sub-index" : L" 分指数");
                label(layer, name, g.pollutantNames[i], 28, ink);
                const auto number =
                    value.valid && value.pollutants[i] >= 0 ? std::to_string(std::llround(value.pollutants[i])) : "--";
                pixelDigits(layer, number, g.pollutantValues[i], ink);
            }
            const double y = spec.rows == 1 ? 118 : 238;
            layer.line(divider + 16, y, spec.rows == 1 ? 836 : 795, y, dark ? 0x668d8f80 : 0xa5b2a6ff, 2);
        }
    }

    void prepare(const cdsdk::Settings& settings, const airplugin::Snapshot& value, bool loading, double width,
                 double height, double dpi) {
        const auto bounds = fitBody(width, height, spec.world.width, spec.world.height);
        const double scale = bounds.width / spec.world.width;
        const auto edge = uint32_t(
            std::clamp(std::ceil(std::max(spec.source.width, spec.source.height) * scale * dpi / 64) * 64, 64., 4096.));
        pixeldesk::requestPixelImage(images, "body", path, 1, edge, dpi);
        images.poll();
        body = images.get("body").image;
        const Key next{width,
                       height,
                       dpi,
                       visibleSnapshot(value),
                       settings.at("font_family").text,
                       settings.at("language").text,
                       settings.at("palette").text,
                       loading,
                       body.get()};
        if (key == next && owner == canvas.target) {
            return;
        }
        key = next;
        fonts.select(canvas.nativeTextFactory(), next.font);
        canvas.fontCollection(fonts.collection());
        cybercanvas::OffscreenSurface surface;
        canvas.renderLayer(surface, {uint32_t(std::ceil(width * dpi)), uint32_t(std::ceil(height * dpi)), dpi},
                           [&](Canvas& layer) {
                               layer.saved([&] {
                                   layer.translate(bounds.x, bounds.y);
                                   layer.scale(scale, scale);
                                   content(layer);
                               });
                               if (value.valid) {
                                   const auto color = levelInk(airplugin::level(value.aqi), next.palette == "dark");
                                   const auto icon = [&](Rect area) {
                                       pixelLeaf(layer,
                                                 {bounds.x + area.x * scale, bounds.y + area.y * scale,
                                                  area.width * scale, area.height * scale},
                                                 color);
                                   };
                                   const auto g = faceLayout(spec, next.language == "en");
                                   icon(g.leaf);
                                   if (spec.rows > 1) {
                                       icon(g.canisterLeaf);
                                   }
                               }
                           });
        face = surface.snapshot();
        owner = canvas.target;
        ++builds;
    }
};

AirRenderer::AirRenderer(const CdCreateContext& context, const AirSpec& spec)
    : impl_(std::make_unique<Impl>(context, spec)) {}

AirRenderer::~AirRenderer() = default;

void AirRenderer::onChange(std::function<void()> callback) {
    impl_->images.onChange(std::move(callback));
}

void AirRenderer::pause(bool paused) {
    impl_->images.pause(paused);
}

bool AirRenderer::ready() const {
    return impl_->body && !impl_->images.pending() && impl_->images.get("body").image == impl_->body;
}

bool AirRenderer::render(const cdsdk::Settings& settings, const airplugin::Snapshot& snapshot, bool loading,
                         const CdFrame& frame, CdRect safe) {
    if (!impl_->canvas.valid(frame)) {
        return false;
    }
    const cdsdk::LanguageScope language(settings.at("language").text);
    impl_->canvas.render(frame, safe, [&](auto&, CdRect area) {
        impl_->prepare(settings, snapshot, loading, area.width, area.height, frame.device_pixel_ratio);
        impl_->canvas.target->DrawBitmap(impl_->face.Get(), D2D1::RectF(0, 0, float(area.width), float(area.height)), 1,
                                         D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    });
    return true;
}

bool AirRenderer::save(const std::filesystem::path& path, const CdFrame& frame) {
    return impl_->canvas.save(path.wstring(), frame);
}

uint64_t AirRenderer::cacheBuilds() const {
    return impl_->builds;
}
}  // namespace pixeldesk
