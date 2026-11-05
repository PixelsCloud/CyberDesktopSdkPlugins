#include "sdk/Appearance.h"

#include <CyberCanvas/Canvas.h>
#include <CyberCanvas/Types.h>

#include <algorithm>
#include <cmath>
#include <numbers>

#include "sdk/Settings.h"

namespace cdsdk {
WidgetAppearance widgetPalette(WidgetColorway colorway) {
    WidgetAppearance result;
    switch (colorway) {
        case WidgetColorway::Sky:
            result = {true, 0xEAF4FFFF, 0x205B88FF, 0x627F99FF, 0xE69A45FF};
            break;
        case WidgetColorway::Teal:
            result = {true, 0xE7F6F1FF, 0x166E68FF, 0x5C8981FF, 0xE3915AFF};
            break;
        case WidgetColorway::Iris:
            result = {true, 0xF0EDFCFF, 0x66509EFF, 0x8A7CA7FF, 0xDF9978FF};
            break;
        case WidgetColorway::Coral:
            result = {true, 0xFFF0E9FF, 0xB45545FF, 0xAA7B70FF, 0x329E96FF};
            break;
        case WidgetColorway::Honey:
            result = {true, 0xFFF3D9FF, 0x98611FFF, 0xA08457FF, 0x329D95FF};
            break;
        case WidgetColorway::Rose:
            result = {true, 0xFCECF1FF, 0xA34F77FF, 0xA78093FF, 0xC99A3DFF};
            break;
        case WidgetColorway::Fern:
            result = {true, 0xEEF5E7FF, 0x477B4EFF, 0x7B9472FF, 0xDBA04AFF};
            break;
        case WidgetColorway::Plum:
            result = {true, 0xF4EAF7FF, 0x824D95FF, 0x9D80A8FF, 0xD99A54FF};
            break;
    }
    result.track = mixColor(result.surface, result.foreground, .08);
    return result;
}

void modernDefaults(Settings& settings, WidgetColorway colorway) {
    const auto colors = widgetPalette(colorway);
    settings.define({{"appearance_version", CD_SETTING_INTEGER, 1, 0, "", 1, 1},
                     {"appearance_colorway", CD_SETTING_INTEGER, int64_t(colorway), 0, "", 0, 7},
                     {"show_background", CD_SETTING_TOGGLE, 1},
                     {"background", CD_SETTING_COLOR, colors.surface},
                     {"background_fill", CD_SETTING_CHOICE, 0, 0, "solid", 0, 0, {"solid", "linear"}},
                     {"background_gradient_end", CD_SETTING_COLOR, colors.surface},
                     {"background_gradient_angle", CD_SETTING_INTEGER, 90, 0, "", 0, 360},
                     {"foreground", CD_SETTING_COLOR, colors.foreground},
                     {"secondary", CD_SETTING_COLOR, colors.secondary},
                     {"accent", CD_SETTING_COLOR, colors.accent},
                     {"background_corner_radius", CD_SETTING_INTEGER, 12, 0, "", 0, 32},
                     {"font_family", CD_SETTING_FONT, 0, 0, "Segoe UI"}});
}

Settings modernSettings(Settings settings, bool enabled, WidgetColorway colorway) {
    if (enabled) {
        modernDefaults(settings, colorway);
    }
    return settings;
}

void normalizeModernColors(Settings& settings) {
    CdSettingValue colorway{};
    if (!modernAppearance(settings) || settings.get("appearance_colorway", &colorway, sizeof(colorway)) != CD_OK) {
        return;
    }
    // Recognize the retired stock palette as a whole; retain individually chosen colors.
    if (settings.at("background").integer != 0xF8F9F3FF || settings.at("foreground").integer != 0x252B24FF ||
        settings.at("secondary").integer != 0x747B6CFF || settings.at("accent").integer != 0x8C9E76FF) {
        return;
    }
    const auto colors = widgetPalette(WidgetColorway(colorway.integer));
    settings.define({{"background", CD_SETTING_COLOR, colors.surface},
                     {"foreground", CD_SETTING_COLOR, colors.foreground},
                     {"secondary", CD_SETTING_COLOR, colors.secondary},
                     {"accent", CD_SETTING_COLOR, colors.accent}});
}

bool modernAppearance(const Settings& settings) {
    CdSettingValue value{};
    return settings.get("appearance_version", &value, sizeof(value)) == CD_OK;
}

WidgetAppearance widgetAppearance(const Settings& settings) {
    WidgetAppearance result;
    if (!modernAppearance(settings)) {
        return result;
    }
    result.background = settings.at("show_background").integer != 0;
    result.surface = uint32_t(settings.at("background").integer);
    result.gradient = settings.at("background_fill").text == "linear";
    result.gradientEnd = uint32_t(settings.at("background_gradient_end").integer);
    result.gradientAngle = double(settings.at("background_gradient_angle").integer);
    result.foreground = uint32_t(settings.at("foreground").integer);
    result.secondary = uint32_t(settings.at("secondary").integer);
    result.accent = uint32_t(settings.at("accent").integer);
    result.radius = double(settings.at("background_corner_radius").integer);
    result.track = mixColor(result.surface, result.foreground, .08);
    return result;
}

uint32_t mixColor(uint32_t first, uint32_t second, double fraction) {
    const double t = std::clamp(fraction, 0., 1.);
    uint32_t result = 0;
    for (int shift : {24, 16, 8, 0}) {
        const auto a = (first >> shift) & 255u, b = (second >> shift) & 255u;
        result |= uint32_t(std::lround(std::lerp(double(a), double(b), t))) << shift;
    }
    return result;
}

void widgetBackground(cybercanvas::Canvas& canvas, const WidgetAppearance& appearance, cybercanvas::Rect area,
                      ID2D1RenderTarget* destination) {
    if (!appearance.background || area.width <= 0 || area.height <= 0) {
        return;
    }
    const double radius = std::clamp(appearance.radius, 0., std::min(area.width, area.height) * .25);
    auto* target = destination ? destination : canvas.target.Get();
    const auto rectangle = D2D1::RoundedRect(
        D2D1::RectF(float(area.x), float(area.y), float(area.x + area.width), float(area.y + area.height)),
        float(radius), float(radius));
    if (!appearance.gradient || appearance.surface == appearance.gradientEnd) {
        canvas.ink(appearance.surface);
        target->FillRoundedRectangle(rectangle, canvas.brush.Get());
        return;
    }
    // Project the whole panel onto the direction so rectangular panels reach both endpoint colors.
    const double angle = appearance.gradientAngle * std::numbers::pi / 180;
    const double dx = std::cos(angle), dy = std::sin(angle);
    const double extent = (std::abs(dx) * area.width + std::abs(dy) * area.height) * .5;
    const double cx = area.x + area.width * .5, cy = area.y + area.height * .5;
    const D2D1_GRADIENT_STOP stops[]{{0, cybercanvas::color(appearance.surface)},
                                     {1, cybercanvas::color(appearance.gradientEnd)}};
    const auto gradient = canvas.linearGradient({cx - dx * extent, cy - dy * extent},
                                                {cx + dx * extent, cy + dy * extent}, stops, target);
    target->FillRoundedRectangle(rectangle, gradient.Get());
}
}  // namespace cdsdk
