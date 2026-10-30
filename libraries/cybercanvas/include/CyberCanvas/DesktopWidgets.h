#pragma once

#include <tuple>

#include "CyberCanvas/DataWidgets.h"

namespace cybercanvas {
// A label keeps typography, clipping and optional relief together. No implicit
// background: the host/plugin owns its surface, including transparent cards.
struct Label {
    Rect bounds;
    std::wstring value;
    TextStyle font;
    uint32_t color = 0xFFFFFFFF;
    bool fit = false;
    double minimumSize = 7;
    Point shadowOffset{};
    uint32_t shadow = 0;

    void draw(Canvas& canvas) const;
};

struct Checkbox : Control {
    explicit Checkbox(uint32_t key, Rect area);

    std::optional<bool> change(const PointerResult& event, bool value) const;

    void draw(Canvas& canvas, const WidgetTheme& theme, bool checked, const IconPainter& icons,
              ControlVisual visual = {}) const;
};

// A continuous circle is deliberately distinct from MeterGauge's 24 segments.
class ProgressRing {
public:
    void draw(Canvas& canvas, Rect bounds, double value, const WidgetTheme& theme, double stroke = 5);

private:
    std::optional<std::tuple<ID2D1Factory*, double, double, double, double, double, double>> key_;
    ComPtr<ID2D1Factory> factory_;
    ComPtr<ID2D1PathGeometry> arc_;
};

// Explicit slots support tiny desktop cards without forcing a single design.
struct MetricItem {
    Label caption, number, detail;
    ContentState state = ContentState::Ready;

    void draw(Canvas& canvas) const;
};

struct StatusLine {
    Label source, updated;

    void draw(Canvas& canvas) const;
};

struct TextRow {
    Rect bounds;
    Label title, detail;
    uint32_t separator = 0;
    double separatorWidth = .7;

    void draw(Canvas& canvas, const WidgetTheme& theme, ControlVisual visual = {}) const;
};

struct TaskRow {
    TextRow row;
    Checkbox checkbox;
    bool checked = false;

    void draw(Canvas& canvas, const WidgetTheme& theme, const IconPainter& icons, ControlVisual visual = {}) const;
};

// Equal visual segment widths with domain thresholds supplied by the plugin.
// AQI classification and unit conversion never enter the renderer.
struct SegmentedScale {
    Rect bounds;
    std::vector<double> thresholds;
    std::vector<uint32_t> colors;
    double gap = 1, markerRadius = 2.1, radius = 1;

    double position(double value) const;

    void draw(Canvas& canvas, const WidgetTheme& theme, std::optional<double> value) const;
};

// Calendar algorithms stay in the plugin; the view shares geometry for paint
// and hit testing and supports both plain dates and lunar/festival subtitles.
struct CalendarGrid {
    Rect bounds;
    size_t rows = 6;

    Rect cell(size_t index) const;

    std::optional<size_t> hit(Point point) const;
};

struct CalendarCell {
    Rect bounds;
    Label day, detail;
    bool selected = false, today = false, current = true;
    Rect highlight;
    double radius = 4;
    uint32_t selection = 0x237C85FF, todayColor = 0x237C8530;

    void draw(Canvas& canvas) const;
};
}  // namespace cybercanvas
