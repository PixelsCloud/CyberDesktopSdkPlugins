#pragma once

#include <sdk/Controls.h>
#include <sdk/Environment.h>
#include <sdk/Images.h>
#include <sdk/Update.h>

#include <filesystem>
#include <span>

#include "plugins/lunar-suite/Lunar.h"

namespace pixeldesk {
struct LunarSpec {
    const char* id;
    const char* folder;
    unsigned columns, rows;
    cybercanvas::Size source;
    cybercanvas::Rect visible, date, day, lunar, badge, weekday, divider;
    std::array<cybercanvas::Rect, 2> buttons{};
    cybercanvas::Rect weeks{}, grid{}, footer{}, today{}, paper{};
    bool darkHeader = false;
};

inline constexpr std::array<LunarSpec, 3> kLunarCatalog{{
    {"com.cyberdesktop.pixeldesk.lunar.2x1",
     "lunar-2x1",
     2,
     1,
     {1557, 957},
     {6, 6, 1545, 945},
     {765, 250, 645, 75},
     {210, 280, 450, 360},
     {765, 390, 645, 132},
     {825, 540, 465, 150},
     {},
     {708, 219, 3, 540}},
    {"com.cyberdesktop.pixeldesk.lunar.2x2",
     "lunar-2x2",
     2,
     2,
     {954, 1443},
     {6, 6, 942, 1431},
     {225, 300, 510, 81},
     {180, 405, 600, 330},
     {175, 930, 610, 105},
     {270, 1080, 414, 150},
     {195, 780, 570, 105},
     {165, 900, 620, 3},
     {{{110, 300, 75, 75}, {780, 300, 75, 75}}}},
    {"com.cyberdesktop.pixeldesk.lunar.4x4",
     "lunar-4x4",
     4,
     4,
     {1527, 1845},
     {6, 6, 1515, 1833},
     {396, 362, 510, 105},
     {},
     {},
     {},
     {},
     {},
     {{{216, 372, 108, 105}, {936, 372, 108, 105}}},
     {207, 510, 1047, 123},
     {207, 633, 1047, 804},
     {231, 1446, 1000, 126},
     {1086, 372, 192, 105},
     {207, 633, 1047, 804},
     true},
}};

const LunarSpec& lunarSpec(std::string_view id);
cdsdk::Settings lunarDefaults();
lunarplugin::Date lunarHostDate(CdEnvironmentSnapshot snapshot);

struct LunarViewport {
    cybercanvas::Point origin;
    double scale;
    cybercanvas::Rect map(cybercanvas::Rect area) const;
};

LunarViewport lunarViewport(CdRect safe, const LunarSpec& spec);
cybercanvas::Rect lunarCell(const LunarSpec& spec, const LunarViewport& viewport, unsigned index, unsigned rows);

class LunarRenderer {
public:
    LunarRenderer(const CdCreateContext& context, const LunarSpec& spec);
    ~LunarRenderer();
    void onChange(std::function<void()> callback);
    void onAction(std::function<void(uint32_t)> callback);
    void pause(bool paused);
    void input(const CdInput& input);
    void cancelInput();
    void syncInput(const cdsdk::Settings& settings, lunarplugin::View view, CdRect safe);
    bool ready() const;
    bool render(const cdsdk::Settings& settings, lunarplugin::View view, const CdFrame& frame, CdRect safe = {});
    bool save(const std::filesystem::path& path, const CdFrame& frame);
    uint64_t cacheBuilds() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class LunarInstance {
public:
    explicit LunarInstance(const CdCreateContext& context);
    ~LunarInstance();
    cdsdk::Settings settings;
    CdResult render(const CdFrame& frame);
    CdResult applySettings(const CdSettingValue* values, uint32_t count);
    void layoutChanged(const CdLayout& layout);
    void input(const CdInput& event);
    void setPaused(bool paused);
    void invalidate();
    std::vector<uint8_t> saveState() const;
    CdResult restoreState(std::span<const uint8_t> bytes);
    CdResult selectDate(lunarplugin::Date date, bool animated = true);
    CdResult navigate(int months);
    const lunarplugin::View& view() const;
    bool followsToday() const;

private:
    CdResult schedule(uint32_t interval);
    void tick();
    void action(uint32_t id);
    void updateDate(bool animated);
    static void CD_CALL wake(void* pointer, uint64_t now) noexcept;
    const LunarSpec& spec_;
    LunarRenderer renderer_;
    cdsdk::Environment environment_;
    cdsdk::Timer timer_;
    cdsdk::RedrawRequests redraw_;
    lunarplugin::View view_;
    CdRect safe_{};
    uint64_t rollStart_ = 0;
    bool hidden_ = false, followToday_ = true;
};
}  // namespace pixeldesk
