#pragma once

#include <sdk/Canvas.h>
#include <sdk/Environment.h>
#include <sdk/Icons.h>
#include <sdk/Images.h>
#include <sdk/Settings.h>
#include <sdk/Update.h>

#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>

#include "plugins/widget-suite/Widget.h"

namespace mechanicalcalendar {
using widgetplugin::Date;
inline constexpr const char* kPluginId = "com.cyberdesktop.calendar.mechanical.4x2";

struct Day {
    Date date;
    bool current = false, today = false, weekend = false;
    bool operator==(const Day&) const = default;
};

struct Month {
    Date date, today;
    unsigned start = 0, days = 0, weeks = 0;
    bool monday = true;
    std::array<Day, 42> cells;
    bool operator==(const Month&) const = default;
};

cdsdk::Settings defaults();
Date localDate(CdEnvironmentSnapshot snapshot);
Month month(Date today, int offset, bool monday);
std::wstring headerDigits(Date date);

struct Geometry {
    cybercanvas::Rect body, grid, yearLabel, monthLabel;
    std::array<cybercanvas::Rect, 6> wheels;
    std::array<cybercanvas::Rect, 2> buttons;
    double rowHeight = 0, columnWidth = 0, scale = 0;
    cybercanvas::Rect cell(unsigned column, unsigned row) const;
};

Geometry arrange(double width, double height, unsigned weeks);
unsigned buttonAt(const Geometry& geometry, cybercanvas::Point point);

struct Visual {
    Date previous;
    double progress = 1;
    int direction = 1;
    unsigned hovered = 0, pressed = 0;
};

class Renderer {
public:
    explicit Renderer(const CdCreateContext& context);
    ~Renderer();
    void onChange(std::function<void()> callback);
    void pause(bool paused);
    bool ready() const;
    bool render(const Month& month, const cdsdk::Settings& settings, const Visual& visual, const CdFrame& frame,
                CdRect safe = {});
    bool save(const std::filesystem::path& path, const CdFrame& frame);
    uint64_t cacheBuilds() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class Instance {
public:
    explicit Instance(const CdCreateContext& context);
    ~Instance();
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
    const Month& displayed() const;
    int offset() const;
    bool rolling() const;

private:
    cdsdk::Environment environment_;
    cdsdk::Timer timer_;
    cdsdk::RedrawRequests redraw_;
    Renderer renderer_;
    Month month_;
    Visual visual_;
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
}  // namespace mechanicalcalendar
