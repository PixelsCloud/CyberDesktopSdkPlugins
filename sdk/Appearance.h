#pragma once

#include <cstdint>

struct ID2D1RenderTarget;

namespace cybercanvas {
class Canvas;
struct Rect;
}  // namespace cybercanvas

namespace cdsdk {
class Settings;

enum class WidgetColorway { Sky, Teal, Iris, Coral, Honey, Rose, Fern, Plum };

struct WidgetAppearance {
    bool background = true;
    uint32_t surface = 0xEAF4FFFF;
    uint32_t foreground = 0x205B88FF;
    uint32_t secondary = 0x627F99FF;
    uint32_t accent = 0xE69A45FF;
    uint32_t track = 0xDBE9F5FF;
    double radius = 12;
    bool gradient = false;
    uint32_t gradientEnd = 0xEAF4FFFF;
    double gradientAngle = 90;
};

WidgetAppearance widgetPalette(WidgetColorway colorway);
// These are rendering defaults, not a settings-page schema. Expose only fields
// the plugin uses, with role-specific labels; see docs/APPEARANCE_SETTINGS.md.
void modernDefaults(Settings& settings, WidgetColorway colorway = WidgetColorway::Sky);
Settings modernSettings(Settings settings, bool enabled = true, WidgetColorway colorway = WidgetColorway::Sky);
void normalizeModernColors(Settings& settings);
bool modernAppearance(const Settings& settings);
WidgetAppearance widgetAppearance(const Settings& settings);
uint32_t mixColor(uint32_t first, uint32_t second, double fraction);
void widgetBackground(cybercanvas::Canvas& canvas, const WidgetAppearance& appearance, cybercanvas::Rect area,
                      ID2D1RenderTarget* destination = nullptr);
}  // namespace cdsdk
