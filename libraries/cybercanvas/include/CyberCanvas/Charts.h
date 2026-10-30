#pragma once

#include "CyberCanvas/Components.h"
#include "CyberCanvas/Semantics.h"

namespace cybercanvas {
struct NumericAxis {
    double minimum = 0, maximum = 1;

    void validate() const;

    double fraction(double value) const;

    std::vector<double> ticks(uint32_t desired = 5) const;
};

enum class ChartKind { Bars, Donut, Heatmap };

struct ChartDatum {
    uint32_t id;
    std::wstring label;
    std::optional<double> value;
    std::optional<uint32_t> color;
};

struct ChartSelection {
    uint32_t id;
    size_t index;
    std::wstring label;
    double value;
    Point anchor;
};

// Controlled, bounded data visualization. IDs remain stable across appended or
// replaced datasets. Missing/nonfinite values are gaps, except finite negative
// donut weights are rejected. Share a NumericAxis explicitly between charts.
class DataChart {
public:
    explicit DataChart(ChartKind kind = ChartKind::Bars);

    void data(std::vector<ChartDatum> values);

    void append(ChartDatum value, size_t capacity = 256);

    void axis(std::shared_ptr<const NumericAxis> value);

    void columns(uint32_t value);

    void formatter(std::function<std::wstring(double)> value);

    const std::vector<ChartDatum>& data() const;

    uint32_t selected() const;

    void select(uint32_t id);

    NumericAxis axis() const;

    std::optional<ChartSelection> hit(Rect bounds, Point position) const;

    bool hover(Rect bounds, Point position);

    bool key(Key key);

    std::vector<SemanticNode> semantics(Rect bounds) const;

    void draw(Canvas& canvas, Rect bounds, const WidgetTheme& theme, bool tooltip = true) const;

    void drawLegend(Canvas& canvas, Rect bounds, const WidgetTheme& theme) const;

private:
    ChartKind kind_;
    std::vector<ChartDatum> values_;
    std::shared_ptr<const NumericAxis> axis_;
    std::function<std::wstring(double)> formatter_;
    uint32_t columns_ = 7, selected_ = 0;

    struct Ring {
        Point center;
        double radius, total;
    };

    static bool contains(Rect r, Point p);

    Rect plotBounds(Rect bounds) const;

    Ring ring(Rect plot) const;

    std::wstring format(double value) const;

    ChartSelection selection(size_t index, Point anchor) const;

    uint32_t ink(size_t index, const WidgetTheme& theme) const;

    std::vector<Rect> cells(Rect plot) const;

    void drawRing(Canvas& canvas, Rect plot, const WidgetTheme& theme) const;

    void drawTooltip(Canvas& canvas, Rect bounds, Rect plot, const WidgetTheme& theme) const;
};
}  // namespace cybercanvas
