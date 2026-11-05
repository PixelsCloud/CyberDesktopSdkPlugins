#pragma once

#include <memory>
#include <string>

namespace cybercanvas {
class Canvas;
struct Rect;
struct WidgetTheme;

// The Monitor dashboard's 80-DIP layout, drawn by the native Canvas backend.
class DashboardGauge {
public:
    DashboardGauge();
    ~DashboardGauge();
    void draw(Canvas& canvas, Rect bounds, double fraction, const std::wstring& number, const std::wstring& unit,
              const WidgetTheme& theme);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace cybercanvas
