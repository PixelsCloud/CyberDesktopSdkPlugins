#pragma once

#include <sdk/Canvas.h>
#include <sdk/Settings.h>
#include <sdk/Update.h>

#include <array>
#include <string_view>

#include "plugins/sign-suite/Animation.h"

namespace neon {
// Original 5x7 lettering artwork for this sign, not a general-purpose font.
inline constexpr std::array<std::array<uint8_t, 7>, 12> lettering{{
    {14, 17, 16, 16, 16, 17, 14},  // C
    {0, 0, 17, 17, 15, 1, 14},     // y
    {16, 16, 30, 17, 17, 17, 30},  // b
    {0, 0, 14, 17, 31, 16, 14},    // e
    {0, 0, 22, 25, 16, 16, 16},    // r
    {30, 17, 17, 17, 17, 17, 30},  // D
    {0, 0, 14, 17, 31, 16, 14},    // e
    {0, 0, 15, 16, 14, 1, 30},     // s
    {16, 16, 18, 20, 24, 20, 18},  // k
    {8, 8, 30, 8, 8, 9, 6},        // t
    {0, 0, 14, 17, 17, 17, 14},    // o
    {0, 0, 30, 17, 30, 16, 16},    // p
}};

uint32_t opacity(uint32_t color, double value);

class Sign {
public:
    cdsdk::Settings settings{
        {"animation", CD_SETTING_TOGGLE, 1},
        {"effect", CD_SETTING_CHOICE, 0, 0, "mixed", 0, 0, {"flicker", "breathe", "scan", "wave", "glitch", "mixed"}},
        {"speed", CD_SETTING_NUMBER, 0, 1.25, {}, .25, 2},
        {"intensity", CD_SETTING_INTEGER, 65, 0, {}, 0, 100},
        {"damage", CD_SETTING_INTEGER, 90, 0, {}, 0, 100},
        {"brightness", CD_SETTING_INTEGER, 85, 0, {}, 10, 100},
        {"glow", CD_SETTING_INTEGER, 65, 0, {}, 0, 100},
        {"plate", CD_SETTING_TOGGLE, 1},
        {"pink", CD_SETTING_COLOR, 0xFF3FADFF},
        {"cyan", CD_SETTING_COLOR, 0x35E8FFFF}};

    explicit Sign(const CdCreateContext& context);

    void invalidate();

    void layoutChanged(const CdLayout& layout);

    void setPaused(bool paused);

    CdResult applySettings(const CdSettingValue* values, uint32_t count);

    CdResult render(const CdFrame& frame);

    bool savePreview(const std::wstring& file, const CdFrame& frame);

private:
    static uint32_t interval(const cdsdk::Settings& settings);

    void plate(double damage);

    void lamp(double x, double y, double w, double h, uint32_t ink, double power, double glow);

    void paint(double width, double height);

    cdsdk::Canvas canvas_;
    cdsdk::RedrawRequests redraw_;
    cdsdk::UpdateLoop updates_;
    CdRect safe_{};
    double seconds_ = 0;
    bool minimal_ = false;
};
}  // namespace neon
