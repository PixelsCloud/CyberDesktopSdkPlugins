#pragma once

#include <optional>
#include <set>
#include <tuple>

#include "CyberCanvas/Components.h"

namespace cybercanvas {
enum class ContentState { Ready, Loading, Error, Empty };

// A static, localized status panel. No timer or implicit retry: the owner decides
// when content changes and whether to offer a separate action button.
struct ContentNotice {
    Rect bounds;
    std::wstring title, detail;
    ContentState state = ContentState::Empty;
    bool background = true;

    void draw(Canvas& canvas, const WidgetTheme& theme) const;
};

struct ChoiceOption {
    uint32_t id = 0;
    Rect bounds;
    std::wstring label;
    bool enabled = true;
    std::string icon;
};

// Controlled selection; IDs and geometry are shared by drawing and input.
class ChoiceGroup {
public:
    explicit ChoiceGroup(std::vector<ChoiceOption> options);

    const std::vector<ChoiceOption>& options() const;

    std::vector<HitRegion> regions(Point origin = {}) const;

    std::optional<uint32_t> change(const PointerResult& result) const;

    void draw(Canvas& canvas, const WidgetTheme& theme, uint32_t selected,
              const std::function<ControlVisual(uint32_t)>& visual = {}, const IconPainter& icons = {}) const;

private:
    std::vector<ChoiceOption> options_;
};

struct MetricReadout {
    Rect bounds;
    std::wstring label, value, detail;
    ContentState state = ContentState::Ready;

    void draw(Canvas& canvas, const WidgetTheme& theme) const;
};

// Bounded sparkline for small data cards. Nonfinite samples break the line;
// fixed and automatic ranges are supported. Geometry is rebuilt on demand.
class LineChart {
public:
    bool samples(std::vector<double> values);

    void range(std::optional<std::pair<double, double>> limits);

    std::vector<std::vector<Point>> points(Rect bounds) const;

    void draw(Canvas& canvas, Rect bounds, const WidgetTheme& theme, double width = 1.5);

    uint64_t geometryBuilds() const;

private:
    std::vector<double> values_;
    std::optional<std::pair<double, double>> limits_;
    Rect area_{};
    ComPtr<ID2D1Factory> factory_;
    std::optional<Path> path_;
    std::vector<Point> singles_;
    bool dirty_ = true;
    uint64_t builds_ = 0;
};

// Extracted from the system monitor's 24-segment gauge. Native drawing allows
// incremental adoption by existing Direct2D renderers; Canvas owns frame state.
class MeterGauge {
public:
    void draw(Canvas& canvas, Rect area, double fraction, const WidgetTheme& theme);

    void draw(ID2D1Factory* factory, ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush, Rect area, double fraction,
              const WidgetTheme& theme);

private:
    ComPtr<ID2D1PathGeometry> arc(double start, double end);

    ComPtr<ID2D1Factory> factory_;
    std::vector<ComPtr<ID2D1PathGeometry>> paths_;
};
}  // namespace cybercanvas
