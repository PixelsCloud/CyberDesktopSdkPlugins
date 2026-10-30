#include <sdk/Icons.h>
#include <sdk/Language.h>
#include <sdk/WindowsFonts.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <optional>
#include <sstream>

#include "plugins/pixel-desk/PixelDigits.h"
#include "plugins/pixel-desk/PixelImages.h"
#include "plugins/pixel-desk/PixelLayout.h"
#include "plugins/pixel-desk/PixelPanels.h"
#include "plugins/pixel-desk/PixelWeather.h"
#include "plugins/pixel-desk/WeatherArt.h"

namespace pixeldesk {
using cybercanvas::Canvas;
using cybercanvas::Rect;
using Microsoft::WRL::ComPtr;

namespace {
void caseSkin(Canvas& canvas, const cybercanvas::ImageData& body, const WeatherSpec& spec) {
    // Extend/collapse only the flat panels. The mint corner parts and bevels
    // retain the compact case's uniform scale in every future footprint.
    const double partScale = 188 / spec.visible.height;
    const double width = spec.visible.width * partScale, height = spec.visible.height * partScale;
    const std::array<double, 4> fromX{0, width * .2, width * .8, width}, fromY{0, height * .3, height * .7, height};
    const std::array<double, 4> toX{0, fromX[1], fromX[2] + spec.world.width - width, spec.world.width},
        toY{0, fromY[1], fromY[2] + spec.world.height - height, spec.world.height};
    for (unsigned y = 0; y < 3; ++y) {
        for (unsigned x = 0; x < 3; ++x) {
            const double sx = (toX[x + 1] - toX[x]) / (fromX[x + 1] - fromX[x]),
                         sy = (toY[y + 1] - toY[y]) / (fromY[y + 1] - fromY[y]);
            canvas.clipped({toX[x], toY[y], toX[x + 1] - toX[x], toY[y + 1] - toY[y]}, [&] {
                canvas.image(body,
                             {toX[x] - (spec.visible.x * partScale + fromX[x]) * sx,
                              toY[y] - (spec.visible.y * partScale + fromY[y]) * sy, spec.source.width * partScale * sx,
                              spec.source.height * partScale * sy},
                             {.fit = cybercanvas::ImageFit::Stretch,
                              .clip = false,
                              .sampling = cybercanvas::ImageSampling::Nearest});
            });
        }
    }
}

std::wstring wrappedCondition(int code, bool english) {
    auto text = cdsdk::displayText(weatherplugin::condition(code, english));
    if (!english && text.size() > 2) {
        text.insert((text.size() + 1) / 2, 1, L'\n');
    } else if (english && text.size() > 7) {
        auto split = text.find_last_of(L' ', text.size() / 2);
        if (split == std::wstring::npos) {
            split = text.find(L' ');
        }
        if (split != std::wstring::npos) {
            text[split] = L'\n';
        }
    }
    return text;
}

std::wstring weatherRange(const weatherplugin::Day* day, bool imperial) {
    if (!day) {
        return L"-- / --";
    }
    return std::to_wstring(std::llround(weatherplugin::displayTemperature(day->low, imperial))) + L"° / " +
           std::to_wstring(std::llround(weatherplugin::displayTemperature(day->high, imperial))) + L"°";
}
}  // namespace

struct WeatherRenderer::Impl {
    struct Key {
        double width, height, dpi;
        weatherplugin::Snapshot snapshot;
        std::string language, font, units, palette;
        bool loading;
        const cybercanvas::ImageData* body;
        const cybercanvas::ImageData* scene;
        bool operator==(const Key&) const = default;
    };

    const WeatherSpec& spec;
    cdsdk::Canvas canvas;
    cdsdk::Images images;
    cdsdk::Icons icons;
    cdsdk::WindowsFonts fonts{canvas.nativeTextFactory()};
    std::filesystem::path path;
    std::shared_ptr<const cybercanvas::ImageData> body, scene;
    std::optional<Key> key;
    ComPtr<ID2D1RenderTarget> owner;
    cybercanvas::SurfaceImage face;
    uint64_t builds = 0;

    Impl(const CdCreateContext& context, const WeatherSpec& value) : spec(value) {
        images.bind(context);
        canvas.bindGpu(context);
        icons.bind(context);
        path = std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(context.plugin_directory))) /
               "assets" / "body.png";
    }

    void label(Canvas& layer, std::wstring text, Rect area, double size, uint32_t color, bool ellipsis = false) {
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
            layer.text(text, style, area, color);
        } else {
            const auto fit = layer.fitTextInk(text, style, area);
            layer.brush->SetColor(cybercanvas::color(color));
            layer.brush->SetOpacity(1);
            layer.target->DrawTextLayout(D2D1::Point2F(float(fit.origin.x), float(fit.origin.y)), fit.layout.Get(),
                                         layer.brush.Get());
        }
    }

    void landscape(Canvas& layer, const weatherplugin::Snapshot& value) {
        constexpr Rect area{32, 32, 400, 224};
        const auto kind = weatherplugin::iconName(value.code, value.day);
        const bool wet = kind == "rain" || kind == "drizzle" || kind == "sleet" ||
                         kind.find("thunderstorms") != std::string::npos,
                   snow = kind == "snow" || kind == "snowflake";
        const uint32_t sky = !value.day                                   ? 0x10253aff
                             : wet || kind == "overcast" || kind == "fog" ? 0x526f83ff
                                                                          : 0x417cafff;
        layer.clipped(area, [&] {
            layer.box(area.x, area.y, area.width, area.height, 0, sky);
            if (scene) {
                layer.image(*scene, area,
                            {.fit = cybercanvas::ImageFit::Cover,
                             .clip = false,
                             .sampling = cybercanvas::ImageSampling::Nearest});
            }
            if (!value.day || wet || snow) {
                layer.box(area.x, area.y, area.width, area.height, 0, !value.day ? 0x06152ca0 : 0x203b5360);
            }
            weatherArt(layer, {133, 41, 174, 174}, value.code, value.day, value.day ? sky : 0x0a1b31ff);
            if (wet) {
                for (unsigned i = 0; i < 20; ++i) {
                    layer.box(44 + (i * 71) % 370, 61 + (i * 47) % 172, 2, 11, 0, 0xb5d7e080);
                }
            } else if (snow) {
                for (unsigned i = 0; i < 20; ++i) {
                    layer.box(44 + (i * 71) % 370, 61 + (i * 47) % 172, 3, 3, 0, 0xf5e7cdbb);
                }
            }
        });
        layer.strokeBox(area, 0, 0x7b9aa4cc, {.width = 2});
    }

    void large(Canvas& layer, const weatherplugin::Snapshot& value, uint32_t ink, bool english, bool imperial,
               bool light) {
        landscape(layer, value);
        const bool marked = key->loading || value.cached;
        label(layer, weatherplugin::wide(value.city), {451, 36, marked ? 232. : 311., 31}, 30, ink, true);
        if (marked) {
            label(layer, key->loading ? (english ? L"Wait" : L"更新") : (english ? L"Cache" : L"缓存"),
                  {700, 40, 62, 24}, 21, light ? 0x267461ff : kMint);
        }
        const auto number =
            std::to_string(std::llround(weatherplugin::displayTemperature(value.temperature, imperial)));
        pixelDigits(layer, number, {453, 83, 164, 90}, ink);
        label(layer, imperial ? L"°F" : L"°C", {618, 79, 32, 29}, 28, ink);
        label(layer, wrappedCondition(value.code, english), {663, 106, 98, 58}, english ? 30 : 43, ink);
        layer.line(452, 184, 765, 184, light ? 0x9eb2aaff : 0x66828f80, 2);
        layer.line(611, 195, 611, 262, light ? 0x9eb2aaff : 0x66828f80, 2);
        label(layer, std::to_wstring(std::llround(value.humidity)) + L"%", {489, 199, 109, 33}, 34, ink);
        std::wostringstream speed;
        speed.imbue(std::locale::classic());
        speed << std::fixed << std::setprecision(1) << value.wind * (imperial ? 3600 / 1609.344 : 1)
              << (imperial ? L" mph" : L" m/s");
        label(layer, speed.str(), {653, 199, 111, 33}, 31, ink);
        label(layer, english ? L"Humidity" : cdsdk::displayText(L"湿度"), {452, 238, 148, 23}, 22, ink);
        label(layer, english ? L"Wind" : cdsdk::displayText(L"风速"), {618, 238, 148, 23}, 22, ink);
        layer.line(32, 274, 768, 274, light ? 0x9eb2aaff : 0x66828f80, 2);
        constexpr const wchar_t* zh[]{L"今天", L"明天", L"后天"};
        constexpr const wchar_t* en[]{L"Today", L"Tomorrow", L"Day after"};
        for (unsigned i = 0; i < 3; ++i) {
            const double left = 32 + i * 245;
            if (i) {
                layer.line(left - 6, 284, left - 6, 373, light ? 0x9eb2aaff : 0x66828f80, 2);
            }
            label(layer, english ? en[i] : cdsdk::displayText(zh[i]), {left, 286, 236, 25}, 25, ink);
            const auto* day = i < value.days.size() ? &value.days[i] : nullptr;
            if (day) {
                weatherArt(layer, {left + 31, 319, 55, 55}, day->code, true, light ? kCream : kInk);
            }
            label(layer, weatherRange(day, imperial), {left + 99, 331, 127, 26}, 25, ink);
        }
    }

    void content(Canvas& layer, const weatherplugin::Snapshot& value) {
        if (spec.normalized) {
            normalizedContent(layer, value);
            return;
        }
        const bool english = key->language == "en", imperial = key->units == "imperial",
                   light = key->palette == "light";
        const uint32_t ink = light ? kInk : kCream, muted = light ? 0x486875ff : 0xbed0d5ff;
        if (body) {
            caseSkin(layer, *body, spec);
        } else {
            stepped(layer, {0, 0, spec.world.width, spec.world.height}, 18, kMint);
            stepped(layer, {15, 15, spec.world.width - 30, spec.world.height - 30}, 12, kInk);
        }
        if (light) {
            stepped(layer, {25, 25, spec.world.width - 50, spec.world.height - 50}, 10, kCream);
        }
        if (!value.valid) {
            const auto title = value.city.empty() ? (english ? L"Weather" : L"天气") : weatherplugin::wide(value.city);
            const auto message = key->loading ? (english ? L"Updating weather" : L"正在更新天气")
                                 : value.error == weatherplugin::Error::LocationUnavailable
                                     ? (english ? L"Choose a location" : L"请选择位置")
                                     : (english ? L"Weather unavailable" : L"暂时无法获取天气");
            label(layer, title, {40, spec.rows > 1 ? 136. : 32., spec.world.width - 80, 28}, 26, ink, true);
            label(layer, message, {40, spec.rows > 1 ? 180. : 75., spec.world.width - 80, 36}, 29, ink);
            label(layer, english ? L"Double-click to retry" : L"双击可重新获取",
                  {45, spec.rows > 1 ? 234. : 128., spec.world.width - 90, 25}, 22, muted);
            return;
        }
        if (spec.rows > 1) {
            large(layer, value, ink, english, imperial, light);
            return;
        }
        if (spec.columns > 2) {
            const bool marked = key->loading || value.cached;
            label(layer, weatherplugin::wide(value.city), {136, 34, marked ? 98. : 126., 29}, 26, ink, true);
            if (marked) {
                label(layer, key->loading ? (english ? L"Wait" : L"更新") : (english ? L"Cache" : L"缓存"),
                      {242, 38, 36, 20}, 16, light ? 0x267461ff : kMint);
            }
            weatherArt(layer, {30, 44, 105, 105}, value.code, value.day, light ? kCream : kInk);
            const auto number =
                std::to_string(std::llround(weatherplugin::displayTemperature(value.temperature, imperial)));
            pixelDigits(layer, number, {142, 70, 96, 58}, ink);
            label(layer, imperial ? L"°F" : L"°C", {239, 64, 24, 23}, 21, ink);
            label(layer, cdsdk::displayText(weatherplugin::condition(value.code, english)), {137, 136, 126, 24}, 23,
                  ink);
            constexpr const wchar_t* zh[]{L"今天", L"明天", L"后天"};
            constexpr const wchar_t* en[]{L"Today", L"Tomorrow", L"Day after"};
            for (unsigned i = 0; i < 3; ++i) {
                const double left = 290 + i * 162;
                layer.line(left - 10, 30, left - 10, 159, light ? 0x9eb2aaff : 0x66828f80, 2);
                label(layer, english ? en[i] : cdsdk::displayText(zh[i]), {left, 32, 148, 26}, 26, ink);
                const auto* day = i < value.days.size() ? &value.days[i] : nullptr;
                if (day) {
                    weatherArt(layer, {left + 43, 65, 62, 62}, day->code, true, light ? kCream : kInk);
                }
                label(layer, weatherRange(day, imperial), {left - 1, 137, 150, 24}, 24, ink);
            }
            return;
        }
        const bool marked = key->loading || value.cached;
        label(layer, weatherplugin::wide(value.city), {172, 34, marked ? 144. : 194., 31}, 26, ink, true);
        if (marked) {
            label(layer, key->loading ? (english ? L"Wait" : L"更新") : (english ? L"Cache" : L"缓存"),
                  {302, 40, 55, 22}, 18, light ? 0x267461ff : kMint);
        }
        weatherArt(layer, {31, 30, 132, 130}, value.code, value.day, light ? kCream : kInk);
        const auto number =
            std::to_string(std::llround(weatherplugin::displayTemperature(value.temperature, imperial)));
        pixelDigits(layer, number, {172, 70, 116, 63}, ink);
        label(layer, imperial ? L"°F" : L"°C", {286, 61, 26, 23}, 22, ink);
        label(layer, wrappedCondition(value.code, english), {305, 84, 65, 46}, english ? 25 : 32, ink);
        label(layer, weatherRange(value.days.empty() ? nullptr : &value.days.front(), imperial), {176, 141, 191, 23},
              24, ink);
    }

    void normalizedContent(Canvas& layer, const weatherplugin::Snapshot& value) {
        if (spec.rows == 1) {
            normalizedStrip(layer, value);
            return;
        }
        const bool english = key->language == "en", imperial = key->units == "imperial";
        if (body) {
            layer.image(*body, {-spec.visible.x, -spec.visible.y, spec.source.width, spec.source.height},
                        {.fit = cybercanvas::ImageFit::Stretch,
                         .clip = false,
                         .sampling = cybercanvas::ImageSampling::Nearest});
        }
        const bool light = key->palette == "light";
        const uint32_t ink = light ? kInk : kCream;
        if (light) {
            layer.box(173, 341, 763, 260, 0, kCream);
            layer.box(948, 341, 425, 123, 0, kCream);
            layer.box(948, 483, 425, 118, 0, kCream);
            layer.box(173, 622, 1200, 130, 0, kCream);
        }
        const auto city = value.city.empty() ? (english ? L"Weather" : L"天气") : weatherplugin::wide(value.city);
        label(layer, city, {458, 345, 455, 66}, 59, ink, true);
        if (!value.valid) {
            const auto message = key->loading ? (english ? L"Updating weather" : L"正在更新天气")
                                 : value.error == weatherplugin::Error::LocationUnavailable
                                     ? (english ? L"Choose a location" : L"请选择位置")
                                     : (english ? L"Weather unavailable" : L"暂时无法获取天气");
            label(layer, message, {215, 455, 666, 92}, 53, ink);
            label(layer, english ? L"Double-click to retry" : L"双击可重新获取", {215, 645, 1100, 60}, 38, ink);
            return;
        }
        weatherArt(layer, {171, 371, 330, 239}, value.code, value.day, light ? kCream : kInk);
        const auto temperature =
            std::to_string(std::llround(weatherplugin::displayTemperature(value.temperature, imperial)));
        pixelDigits(layer, temperature, {504, 434, 264, 151}, ink);
        label(layer, imperial ? L"°F" : L"°C", {782, 450, 106, 103}, 86, ink);
        if (key->loading || value.cached) {
            label(layer, key->loading ? (english ? L"Updating" : L"更新中") : (english ? L"Cached" : L"缓存"),
                  {695, 583, 206, 27}, 24, light ? 0x267461ff : kMint);
        }
        label(layer, english ? L"Humidity" : L"湿度", {1110, 344, 251, 40}, 37, ink);
        label(layer, std::to_wstring(std::llround(value.humidity)) + L"%", {1110, 389, 252, 73}, 76, ink);
        label(layer, english ? L"Wind" : L"风速", {1110, 487, 250, 38}, 36, ink);
        std::wostringstream speed;
        speed.imbue(std::locale::classic());
        speed << std::fixed << std::setprecision(1) << value.wind * (imperial ? 3600 / 1609.344 : 1);
        label(layer, speed.str(), {1110, 533, 171, 66}, 72, ink);
        label(layer, imperial ? L"mph" : L"m/s", {1280, 560, 86, 37}, 39, ink);
        constexpr const wchar_t* zh[]{L"今天", L"明天", L"后天"};
        constexpr const wchar_t* en[]{L"Today", L"Tomorrow", L"Day after"};
        for (unsigned i = 0; i < 3; ++i) {
            const double left = 173 + i * 403;
            const auto* day = i < value.days.size() ? &value.days[i] : nullptr;
            label(layer, english ? en[i] : zh[i], {left + 114, 632, 270, 39}, 37, ink);
            if (day) {
                weatherArt(layer, {left + 10, 664, 124, 92}, day->code, true, light ? kCream : kInk);
            }
            label(layer, weatherRange(day, imperial), {left + 135, 686, 250, 55}, 48, ink);
        }
    }

    void normalizedStrip(Canvas& layer, const weatherplugin::Snapshot& value) {
        const bool english = key->language == "en", imperial = key->units == "imperial";
        const bool compact = spec.columns == 2, light = key->palette == "light";
        const uint32_t ink = light ? kInk : kCream;
        if (body) {
            if (!compact) {
                panelStrip(layer, *body, {-spec.visible.x, -spec.visible.y, spec.source.width, spec.source.height},
                           spec.source.height + 74, 90, 90, true);
            } else {
                layer.image(*body, {-spec.visible.x, -spec.visible.y, spec.source.width, spec.source.height},
                            {.fit = cybercanvas::ImageFit::Stretch,
                             .clip = false,
                             .sampling = cybercanvas::ImageSampling::Nearest});
            }
        }
        if (!compact) {
            layer.box(48, 34, 846, 202, 0, light ? kCream : kInk);
            for (double column : {346., 536., 726.}) {
                for (double row = 43; row < 229; row += 12) {
                    layer.box(column, row, 3, 6, 0, kMint);
                }
            }
        }
        if (light) {
            layer.box(compact ? 37 : 47, compact ? 39 : 30, compact ? 125 : 848, compact ? 193 : 139, 0, kCream);
        }
        const auto city = value.city.empty() ? (english ? L"Weather" : L"天气") : weatherplugin::wide(value.city);
        const bool marked = key->loading || value.cached;
        const Rect cityArea = compact ? Rect{37, 43, marked ? 86. : 125., 30} : Rect{170, 57, marked ? 113. : 157., 31};
        label(layer, city, cityArea, 27, ink, true);
        if (marked && value.valid) {
            label(layer, key->loading ? (english ? L"Wait" : L"更新") : (english ? L"Cache" : L"缓存"),
                  compact ? Rect{124, 47, 37, 21} : Rect{283, 61, 44, 23}, 17, light ? 0x267461ff : kMint);
        }
        if (!value.valid) {
            pixelDigits(layer, "--", compact ? Rect{44, 151, 102, 45} : Rect{169, 108, 123, 70}, ink);
            const auto message = key->loading ? (english ? L"Updating" : L"获取中")
                                 : value.error == weatherplugin::Error::LocationUnavailable
                                     ? (english ? L"Location" : L"未定位")
                                     : (english ? L"Unavailable" : L"不可用");
            label(layer, message, compact ? Rect{37, 197, 125, 27} : Rect{166, 193, 163, 28}, 24, ink);
            return;
        }
        weatherArt(layer, compact ? Rect{46, 73, 112, 80} : Rect{49, 79, 112, 117}, value.code, value.day,
                   light ? kCream : kInk);
        const auto temperature =
            std::to_string(std::llround(weatherplugin::displayTemperature(value.temperature, imperial)));
        pixelDigits(layer, temperature, compact ? Rect{42, 153, 91, 43} : Rect{170, 108, 122, 78}, ink);
        label(layer, imperial ? L"°F" : L"°C", compact ? Rect{132, 152, 29, 28} : Rect{294, 111, 34, 38}, 29, ink);
        if (compact) {
            label(layer, cdsdk::displayText(weatherplugin::condition(value.code, english)), {37, 199, 125, 25}, 25,
                  ink);
            return;
        }
        constexpr const wchar_t* zh[]{L"今天", L"明天", L"后天"};
        constexpr const wchar_t* en[]{L"Today", L"Tomorrow", L"Day after"};
        for (unsigned i = 0; i < 3; ++i) {
            const double left = 346 + i * 190;
            const auto* day = i < value.days.size() ? &value.days[i] : nullptr;
            label(layer, english ? en[i] : zh[i], {left + 10, 55, 169, 30}, 26, ink);
            if (day) {
                weatherArt(layer, {left + 55, 101, 80, 65}, day->code, true, light ? kCream : kInk);
            }
            if (day) {
                const auto low =
                    std::to_wstring(std::llround(weatherplugin::displayTemperature(day->low, imperial))) + L"°";
                const auto high =
                    std::to_wstring(std::llround(weatherplugin::displayTemperature(day->high, imperial))) + L"°";
                label(layer, low, {left + 9, 193, 75, 28}, 26, light ? 0x35718cff : 0x9bd5ecff);
                label(layer, L"/", {left + 84, 193, 21, 28}, 26, ink);
                label(layer, high, {left + 105, 193, 75, 28}, 26, light ? 0x8c5b24ff : 0xffc76eff);
            } else {
                label(layer, weatherRange(day, imperial), {left + 9, 193, 171, 28}, 26, ink);
            }
        }
    }

    void prepare(const cdsdk::Settings& settings, const weatherplugin::Snapshot& value, bool loading, double width,
                 double height, double dpi) {
        const auto bounds = fitBody(width, height, spec.world.width, spec.world.height);
        const double scale = bounds.width / spec.world.width;
        const auto edge =
            uint32_t(std::clamp(std::ceil(std::max(spec.source.width, spec.source.height) *
                                          (spec.normalized ? 1 : 188 / spec.visible.height) * scale * dpi / 64) *
                                    64,
                                64., 4096.));
        pixeldesk::requestPixelImage(images, "body", path, 1, edge, dpi);
        if (spec.rows > 1 && !spec.normalized) {
            const auto pixels = uint32_t(std::clamp(std::ceil(400 * scale * dpi / 64) * 64, 64., 4096.));
            pixeldesk::requestPixelImage(images, "city", path.parent_path() / "city.png", 1, pixels, dpi);
        }
        images.poll();
        if (const auto next = images.get("body").image) {
            body = next;
        }
        if (const auto next = images.get("city").image) {
            scene = next;
        }
        const Key next{width,
                       height,
                       dpi,
                       value,
                       settings.at("language").text,
                       settings.at("font_family").text,
                       settings.at("units").text,
                       settings.at("palette").text,
                       loading,
                       body.get(),
                       scene.get()};
        if (key == next && owner == canvas.target) {
            return;
        }
        key = next;
        fonts.select(canvas.nativeTextFactory(), key->font);
        canvas.fontCollection(fonts.collection());
        cybercanvas::OffscreenSurface surface;
        canvas.renderLayer(
            surface, {uint32_t(std::ceil(width * dpi)), uint32_t(std::ceil(height * dpi)), dpi}, [&](Canvas& layer) {
                const double x = (width - spec.world.width * scale) / 2, y = (height - spec.world.height * scale) / 2;
                layer.saved([&] {
                    layer.translate(x, y);
                    layer.scale(scale, scale);
                    content(layer, value);
                });
                // Paint functional Lucide icons in output logical coordinates so
                // their shared cache is keyed by the actual display size and DPI.
                if (spec.rows > 1 && value.valid) {
                    const uint32_t color = key->palette == "light" ? 0x267461ff : kMint;
                    const Rect humidity = spec.normalized ? Rect{980, 356, 101, 91} : Rect{454, 201, 28, 28};
                    const Rect wind = spec.normalized ? Rect{980, 504, 101, 91} : Rect{620, 201, 28, 28};
                    icons.paint(layer,
                                {x + humidity.x * scale, y + humidity.y * scale, humidity.width * scale,
                                 humidity.height * scale},
                                "droplet", color);
                    icons.paint(layer,
                                {x + wind.x * scale, y + wind.y * scale, wind.width * scale, wind.height * scale},
                                "wind", color);
                }
            });
        face = surface.snapshot();
        owner = canvas.target;
        ++builds;
    }
};

WeatherRenderer::WeatherRenderer(const CdCreateContext& context, const WeatherSpec& spec)
    : impl_(std::make_unique<Impl>(context, spec)) {}

WeatherRenderer::~WeatherRenderer() = default;

void WeatherRenderer::onChange(std::function<void()> callback) {
    impl_->images.onChange(std::move(callback));
}

void WeatherRenderer::pause(bool paused) {
    impl_->images.pause(paused);
}

bool WeatherRenderer::ready() const {
    return impl_->body && !impl_->images.pending() && impl_->images.get("body").image == impl_->body &&
           (impl_->spec.rows == 1 || impl_->spec.normalized ||
            (impl_->scene && impl_->images.get("city").image == impl_->scene));
}

bool WeatherRenderer::render(const cdsdk::Settings& settings, const weatherplugin::Snapshot& snapshot, bool loading,
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

bool WeatherRenderer::save(const std::filesystem::path& path, const CdFrame& frame) {
    return impl_->canvas.save(path.wstring(), frame);
}

uint64_t WeatherRenderer::cacheBuilds() const {
    return impl_->builds;
}
}  // namespace pixeldesk
