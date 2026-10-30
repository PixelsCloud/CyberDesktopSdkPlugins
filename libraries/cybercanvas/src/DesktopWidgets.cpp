#include "CyberCanvas/DesktopWidgets.h"

namespace cybercanvas {
auto Label::draw(Canvas& canvas) const -> void {
    detail::layoutRect(bounds);
    if (!std::isfinite(minimumSize) || minimumSize <= 0 || minimumSize > 4096) {
        throw std::invalid_argument("Invalid minimum label size");
    }
    if (bounds.width <= 0 || bounds.height <= 0 || value.empty()) {
        return;
    }
    auto style = font;
    if (fit) {
        const auto natural = canvas.measureText(value, style, {1e6, 1e6});
        const double ratio = std::min({1., bounds.width / std::max(1., natural.widthIncludingTrailingWhitespace),
                                       bounds.height / std::max(1., natural.height)});
        style.size = std::min(style.size, std::max(minimumSize, style.size * ratio));
    }
    style.ellipsis = true;
    canvas.clipped(bounds, [&] {
        if (shadow & 255) {
            auto offset = bounds;
            offset.x += shadowOffset.x;
            offset.y += shadowOffset.y;
            canvas.text(value, style, offset, shadow);
        }
        canvas.text(value, style, bounds, color);
    });
}

Checkbox::Checkbox(uint32_t key, Rect area) : Control{key, area} {}

auto Checkbox::change(const PointerResult& event, bool value) const -> std::optional<bool> {
    return clicked(event) ? std::optional<bool>(!value) : std::nullopt;
}

auto ProgressRing::draw(Canvas& canvas, Rect bounds, double value, const WidgetTheme& theme, double stroke) -> void {
    detail::layoutRect(bounds);
    if (!std::isfinite(value) || !std::isfinite(stroke) || stroke <= 0) {
        throw std::invalid_argument("Invalid ring value or stroke");
    }
    const double radius = (std::min(bounds.width, bounds.height) - stroke) * .5;
    if (radius <= 0) {
        return;
    }
    value = std::clamp(value, 0., 1.);
    const auto key =
        std::make_tuple(canvas.factory.Get(), bounds.x, bounds.y, radius, value, bounds.width, bounds.height);
    const Point center{bounds.x + bounds.width * .5, bounds.y + bounds.height * .5};
    if (!key_ || *key_ != key) {
        arc_.Reset();
        if (value > 0 && value < 1) {
            ComPtr<ID2D1GeometrySink> sink;
            require(canvas.factory->CreatePathGeometry(arc_.GetAddressOf()));
            require(arc_->Open(sink.GetAddressOf()));
            sink->BeginFigure(D2D1::Point2F(float(center.x), float(center.y - radius)), D2D1_FIGURE_BEGIN_HOLLOW);
            const double angle = value * 6.283185307179586 - 1.5707963267948966;
            sink->AddArc(D2D1::ArcSegment(
                D2D1::Point2F(float(center.x + radius * std::cos(angle)), float(center.y + radius * std::sin(angle))),
                D2D1::SizeF(float(radius), float(radius)), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE,
                value > .5 ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            require(sink->Close());
        }
        factory_ = canvas.factory;
        key_ = key;
    }
    canvas.saved([&] {
        canvas.oval(center.x, center.y, radius, radius, theme.track, stroke);
        if (value == 1) {
            canvas.oval(center.x, center.y, radius, radius, theme.accent, stroke);
        } else if (arc_) {
            canvas.brush->SetColor(color(theme.accent));
            canvas.target->DrawGeometry(arc_.Get(), canvas.brush.Get(), float(stroke));
        }
    });
}

auto MetricItem::draw(Canvas& canvas) const -> void {
    caption.draw(canvas);
    auto display = number;
    if (state != ContentState::Ready) {
        display.value = state == ContentState::Loading ? L"…" : L"—";
    }
    display.draw(canvas);
    detail.draw(canvas);
}

auto StatusLine::draw(Canvas& canvas) const -> void {
    source.draw(canvas);
    updated.draw(canvas);
}

auto SegmentedScale::position(double value) const -> double {
    if (thresholds.size() != colors.size() + 1 || colors.empty() || colors.size() > 64 || !std::isfinite(value)) {
        throw std::invalid_argument("Invalid segmented scale");
    }
    for (size_t i = 0; i < thresholds.size(); ++i) {
        if (!std::isfinite(thresholds[i]) ||
            (i && (thresholds[i] <= thresholds[i - 1] || !std::isfinite(thresholds[i] - thresholds[i - 1])))) {
            throw std::invalid_argument("Scale thresholds must increase");
        }
    }
    const auto upper = std::upper_bound(thresholds.begin(), thresholds.end(), value);
    const auto index = std::min(colors.size() - 1, size_t(std::max<ptrdiff_t>(0, upper - thresholds.begin() - 1)));
    const double part = std::clamp((value - thresholds[index]) / (thresholds[index + 1] - thresholds[index]), 0., 1.);
    return (index + part) / colors.size();
}

auto SegmentedScale::draw(Canvas& canvas, const WidgetTheme& theme, std::optional<double> value) const -> void {
    detail::layoutRect(bounds);
    const double amount = position(value.value_or(thresholds.empty() ? 0 : thresholds.front()));
    if (!std::isfinite(gap) || gap < 0 || !std::isfinite(markerRadius) || markerRadius < 0 || !std::isfinite(radius) ||
        radius < 0) {
        throw std::invalid_argument("Invalid scale geometry");
    }
    if (bounds.width <= 0 || bounds.height <= 0) {
        return;
    }
    const double width = bounds.width / colors.size();
    for (size_t i = 0; i < colors.size(); ++i) {
        canvas.box(bounds.x + i * width, bounds.y, std::max(0., width - gap), bounds.height, radius, colors[i]);
    }
    if (value) {
        const double radius = std::min(markerRadius, bounds.width * .5);
        canvas.oval(std::clamp(bounds.x + amount * bounds.width, bounds.x + radius, bounds.x + bounds.width - radius),
                    bounds.y + bounds.height * .5, radius, radius, theme.foreground);
    }
}

auto CalendarGrid::cell(size_t index) const -> Rect {
    detail::layoutRect(bounds);
    if (rows < 1 || rows > 6 || index >= rows * 7) {
        throw std::out_of_range("Invalid calendar cell");
    }
    return {bounds.x + index % 7 * bounds.width / 7, bounds.y + index / 7 * bounds.height / rows, bounds.width / 7,
            bounds.height / rows};
}

auto CalendarGrid::hit(Point point) const -> std::optional<size_t> {
    cell(0);
    if (bounds.width <= 0 || bounds.height <= 0 || !std::isfinite(point.x) || !std::isfinite(point.y) ||
        point.x < bounds.x || point.y < bounds.y || point.x >= bounds.x + bounds.width ||
        point.y >= bounds.y + bounds.height) {
        return {};
    }
    return std::min(rows - 1, size_t((point.y - bounds.y) * rows / bounds.height)) * 7 +
           std::min(size_t(6), size_t((point.x - bounds.x) * 7 / bounds.width));
}

auto CalendarCell::draw(Canvas& canvas) const -> void {
    canvas.clipped(bounds, [&] {
        if (selected || today) {
            canvas.box(highlight.x, highlight.y, highlight.width, highlight.height, radius,
                       selected ? selection : todayColor);
        }
        day.draw(canvas);
        detail.draw(canvas);
    });
}

auto Checkbox::draw(Canvas& canvas, const WidgetTheme& theme, bool checked, const IconPainter& icons,
                    ControlVisual visual) const -> void {
    if (bounds.width <= 0 || bounds.height <= 0) {
        return;
    }
    const double size = std::min(bounds.width, bounds.height);
    const auto square = aligned(bounds, {size, size});
    detail::widgetOpacity(canvas, enabled ? 1 : theme.disabledOpacity, [&] {
        canvas.box(square.x, square.y, size, size, std::min(theme.radius, size * .3),
                   blended(checked ? theme.accent : theme.track, theme.foreground, visual.feedback * .12));
        if (checked && icons) {
            icons(canvas, inset(square, {2, 2, 2, 2}), "check", theme.onAccent);
        }
    });
}

auto TextRow::draw(Canvas& canvas, const WidgetTheme& theme, ControlVisual visual) const -> void {
    if (bounds.width <= 0 || bounds.height <= 0) {
        return;
    }
    canvas.clipped(bounds, [&] {
        if (visual.hovered || visual.pressed) {
            canvas.withOpacity(visual.pressed ? .14 : .07, [&] {
                canvas.box(bounds.x, bounds.y, bounds.width, bounds.height, theme.radius, theme.foreground);
            });
        }
        title.draw(canvas);
        detail.draw(canvas);
        if (separator & 255) {
            canvas.line(bounds.x, bounds.y + bounds.height - 1, bounds.x + bounds.width, bounds.y + bounds.height - 1,
                        separator, separatorWidth);
        }
    });
}

auto TaskRow::draw(Canvas& canvas, const WidgetTheme& theme, const IconPainter& icons, ControlVisual visual) const
    -> void {
    row.draw(canvas, theme, visual);
    checkbox.draw(canvas, theme, checked, icons, visual);
}
}  // namespace cybercanvas
