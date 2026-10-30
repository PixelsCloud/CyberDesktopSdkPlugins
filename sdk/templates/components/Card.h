#pragma once

#include <sdk/Canvas.h>
#include <sdk/Controls.h>
#include <sdk/Environment.h>
#include <sdk/Settings.h>
#include <sdk/Update.h>
#include <sdk/WindowsFonts.h>

#include <array>
#include <chrono>
#include <span>

#include "templates/components/ServiceModel.h"

namespace componentdemo {
// The four starters and Workbench share these editable controls. ServiceModel
// owns live subscriptions; the explicit demo mode remains useful offline.
class Card {
public:
    cdsdk::Settings settings{{"language", CD_SETTING_TEXT, 0, 0, "auto", 0, 31},
                             {"show_background", CD_SETTING_TOGGLE, 1},
                             {"data_source", CD_SETTING_CHOICE, 0, 0, "host", 0, 0, {"host", "demo"}}};

    Card(const CdCreateContext& context, int kind);

    void invalidate();

    void layoutChanged(const CdLayout& layout);

    void input(const CdInput& event);

    void setPaused(bool paused);

    CdResult applySettings(const CdSettingValue* values, uint32_t count);

    CdResult render(const CdFrame& frame);

    std::vector<uint8_t> saveState() const;

    static bool validState(std::span<const uint8_t> state, int kind);

    CdResult restoreState(std::span<const uint8_t> state);

private:
    using Rect = cybercanvas::Rect;
    using Theme = cybercanvas::WidgetTheme;
    cdsdk::Canvas canvas_;
    cdsdk::Environment environment_;
    cdsdk::RedrawRequests redraw_;
    cdsdk::Controls controls_;
    cdsdk::Timer timer_;
    cybercanvas::ProgressRing ring_;
    CdRect safe_{};
    int kind_ = 0, monthOffset_ = 0;
    uint8_t tasks_ = 0, track_ = 0, selected_ = 0, sample_ = 64;
    bool playing_ = false, paused_ = false;
    unsigned position_ = 0;
    uint64_t lastSecond_ = 0;
    int lastDay_ = 0;
    std::vector<cdsdk::Controls::Item> items_;
    ServiceModel live_;

    bool live() const;

    bool playing() const;

    std::wstring serviceStatus() const;

    bool cn() const;

    cybercanvas::Label label(std::wstring text, Rect bounds, const Theme& theme, double size = 13,
                             bool bold = false) const;

    std::array<std::wstring, 4> tasks() const;

    std::chrono::year_month_day firstDay() const;

    unsigned daysInMonth() const;

    size_t dayOffset() const;

    std::wstring monthTitle() const;

    cybercanvas::CalendarGrid grid() const;

    void arrange();

    void button(const cdsdk::Controls::Item& item, const Theme& theme, std::string icon, std::wstring fallback);

    void drawInfo(const Theme& theme);

    void drawTasks(const Theme& theme);

    void drawMedia(const Theme& theme);

    void drawCalendar(const Theme& theme);

    void activate(uint32_t id);

    void schedule();

    static void CD_CALL tick(void* user, uint64_t) noexcept;
};
}  // namespace componentdemo
