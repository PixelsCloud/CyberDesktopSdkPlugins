#pragma once

#include "plugins/mechanical-calendar/Calendar.h"

namespace pixeldesk {
inline constexpr const char* kCalendarId = "com.cyberdesktop.pixeldesk.calendar.4x2";
using CalendarMonth = mechanicalcalendar::Month;
using CalendarVisual = mechanicalcalendar::Visual;
using CalendarDate = mechanicalcalendar::Date;

cdsdk::Settings calendarDefaults();

struct CalendarGeometry {
    cybercanvas::Rect body, grid, header;
    std::array<cybercanvas::Rect, 2> buttons;
    double rowHeight = 0, columnWidth = 0, scale = 0;
    cybercanvas::Rect cell(unsigned column, unsigned row) const;
};

CalendarGeometry calendarGeometry(double width, double height, unsigned weeks);
unsigned calendarButtonAt(const CalendarGeometry& geometry, cybercanvas::Point point);

class CalendarRenderer {
public:
    explicit CalendarRenderer(const CdCreateContext& context);
    ~CalendarRenderer();
    void onChange(std::function<void()> callback);
    void pause(bool paused);
    bool ready() const;
    bool render(const CalendarMonth& month, const cdsdk::Settings& settings, const CalendarVisual& visual,
                const CdFrame& frame, CdRect safe = {});
    bool save(const std::filesystem::path& path, const CdFrame& frame);
    uint64_t cacheBuilds() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class CalendarInstance {
public:
    explicit CalendarInstance(const CdCreateContext& context);
    ~CalendarInstance();
    cdsdk::Settings settings;
    CdResult render(const CdFrame& frame);
    CdResult applySettings(const CdSettingValue* values, uint32_t count);
    void layoutChanged(const CdLayout& layout);
    void input(const CdInput& event);
    void setPaused(bool paused);
    void invalidate();
    std::vector<uint8_t> saveState() const;
    CdResult restoreState(std::span<const uint8_t> bytes);
    CdResult navigate(int offset);
    const CalendarMonth& displayed() const;
    int offset() const;
    bool rolling() const;

private:
    cdsdk::Environment environment_;
    cdsdk::Timer timer_;
    cdsdk::RedrawRequests redraw_;
    CalendarRenderer renderer_;
    CalendarMonth month_;
    CalendarVisual visual_;
    CdRect safe_{};
    uint64_t rollStart_ = 0;
    int offset_ = 0;
    bool paused_ = false, dragging_ = false;
    double pressX_ = 0, pressY_ = 0;
    CdResult schedule(uint32_t interval);
    CdResult setOffset(int offset, bool animate);
    void tick();
    void cancelInput();
    static void CD_CALL onTick(void* pointer, uint64_t now) noexcept;
};
}  // namespace pixeldesk
