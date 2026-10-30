#include "CyberCanvas/Charts.h"

#include <iomanip>
#include <numbers>
#include <set>
#include <sstream>

namespace cybercanvas {
auto NumericAxis::validate() const -> void {
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum >= maximum || std::abs(minimum) > 1e12 ||
        std::abs(maximum) > 1e12) {
        throw std::invalid_argument("Invalid chart axis range");
    }
}

auto NumericAxis::fraction(double value) const -> double {
    validate();
    return std::clamp((value - minimum) / (maximum - minimum), 0., 1.);
}

auto NumericAxis::ticks(uint32_t desired) const -> std::vector<double> {
    validate();
    if (desired < 2 || desired > 20) {
        throw std::invalid_argument("Invalid chart tick count");
    }
    const double raw = (maximum - minimum) / (desired - 1), base = std::pow(10., std::floor(std::log10(raw)));
    if (raw == 0 || base == 0 || !std::isfinite(base)) {
        return {minimum, maximum};
    }
    const double multiple = raw / base;
    const double step = base * (multiple <= 1 ? 1 : multiple <= 2 ? 2 : multiple <= 5 ? 5 : 10);
    std::vector<double> result;
    for (double tick = std::ceil(minimum / step) * step; tick <= maximum + step * 1e-8 && result.size() < 32;) {
        result.push_back(std::abs(tick) < step * 1e-8 ? 0 : tick);
        const double next = tick + step;
        if (next == tick) {
            break;
        }
        tick = next;
    }
    return result;
}

DataChart::DataChart(ChartKind kind) : kind_(kind) {}

auto DataChart::data(std::vector<ChartDatum> values) -> void {
    if (values.size() > (kind_ == ChartKind::Donut ? 64 : 4096)) {
        throw std::length_error("Chart data limit exceeded");
    }
    std::set<uint32_t> ids;
    for (auto& value : values) {
        if (!value.id || !ids.insert(value.id).second) {
            throw std::invalid_argument("Chart IDs must be unique and nonzero");
        }
        if (value.value && !std::isfinite(*value.value)) {
            value.value.reset();
        }
        if (value.value && (std::abs(*value.value) > 1e12 || (kind_ == ChartKind::Donut && *value.value < 0))) {
            throw std::invalid_argument("Chart value outside supported range");
        }
    }
    values_ = std::move(values);
    if (selected_ && std::none_of(values_.begin(), values_.end(), [&](const auto& value) {
            return value.id == selected_ && value.value;
        })) {
        selected_ = 0;
    }
}

auto DataChart::append(ChartDatum value, size_t capacity) -> void {
    if (!capacity || capacity > 4096) {
        throw std::invalid_argument("Invalid live chart capacity");
    }
    auto values = values_;
    values.push_back(std::move(value));
    if (values.size() > capacity) {
        values.erase(values.begin(), values.begin() + ptrdiff_t(values.size() - capacity));
    }
    data(std::move(values));
}

auto DataChart::axis(std::shared_ptr<const NumericAxis> value) -> void {
    if (value) {
        value->validate();
    }
    axis_ = std::move(value);
}

auto DataChart::columns(uint32_t value) -> void {
    if (!value || value > 256) {
        throw std::invalid_argument("Invalid heatmap columns");
    }
    columns_ = value;
}

auto DataChart::formatter(std::function<std::wstring(double)> value) -> void {
    formatter_ = std::move(value);
}

auto DataChart::data() const -> const std::vector<ChartDatum>& {
    return values_;
}

auto DataChart::selected() const -> uint32_t {
    return selected_;
}

auto DataChart::select(uint32_t id) -> void {
    if (id && std::none_of(values_.begin(), values_.end(), [&](const auto& value) {
            return value.id == id && value.value;
        })) {
        throw std::invalid_argument("Invalid chart selection");
    }
    selected_ = id;
}

auto DataChart::axis() const -> NumericAxis {
    if (axis_) {
        axis_->validate();
        return *axis_;
    }
    NumericAxis result{0, 0};
    bool first = true;
    for (const auto& value : values_) {
        if (!value.value) {
            continue;
        }
        result.minimum = first ? *value.value : std::min(result.minimum, *value.value);
        result.maximum = first ? *value.value : std::max(result.maximum, *value.value);
        first = false;
    }
    if (first) {
        return {};
    }
    if (kind_ == ChartKind::Bars) {
        result.minimum = std::min(0., result.minimum);
        result.maximum = std::max(0., result.maximum);
    }
    if (result.minimum == result.maximum) {
        const double padding = std::max(1., std::abs(result.minimum) * .1);
        result.minimum = std::max(-1e12, result.minimum - padding);
        result.maximum = std::min(1e12, result.maximum + padding);
    }
    return result;
}

auto DataChart::hit(Rect bounds, Point position) const -> std::optional<ChartSelection> {
    const auto plot = plotBounds(bounds);
    if (!contains(plot, position)) {
        return {};
    }
    if (kind_ == ChartKind::Donut) {
        const auto geometry = ring(plot);
        const double x = position.x - geometry.center.x, y = position.y - geometry.center.y;
        const double radius = std::hypot(x, y);
        if (radius < geometry.radius * .58 || radius > geometry.radius || geometry.total <= 0) {
            return {};
        }
        double angle = std::atan2(y, x) + std::numbers::pi / 2;
        if (angle < 0) {
            angle += 2 * std::numbers::pi;
        }
        double end = 0;
        for (size_t i = 0; i < values_.size(); ++i) {
            if (!values_[i].value || *values_[i].value <= 0) {
                continue;
            }
            end += *values_[i].value / geometry.total * 2 * std::numbers::pi;
            if (angle <= end) {
                return selection(i, position);
            }
        }
    } else {
        const auto rectangles = cells(plot);
        for (size_t i = 0; i < rectangles.size(); ++i) {
            if (values_[i].value && contains(rectangles[i], position)) {
                return selection(i, position);
            }
        }
    }
    return {};
}

auto DataChart::hover(Rect bounds, Point position) -> bool {
    const auto value = hit(bounds, position);
    const auto id = value ? value->id : 0;
    return std::exchange(selected_, id) != id;
}

auto DataChart::key(Key key) -> bool {
    if (values_.empty()) {
        return false;
    }
    const auto found = std::find_if(values_.begin(), values_.end(), [&](const auto& value) {
        return value.id == selected_;
    });
    int position = found == values_.end() ? -1 : int(found - values_.begin());
    int step = 1;
    if (key == Key::Home) {
        position = -1;
    } else if (key == Key::End) {
        position = int(values_.size());
        step = -1;
    } else if (key == Key::Left || key == Key::Up) {
        step = key == Key::Up && kind_ == ChartKind::Heatmap ? -int(columns_) : -1;
    } else if (key == Key::Right || key == Key::Down) {
        step = key == Key::Down && kind_ == ChartKind::Heatmap ? int(columns_) : 1;
    } else {
        return false;
    }
    if (position < 0 && step < 0) {
        position = int(values_.size());
    }
    for (size_t attempt = 0; attempt < values_.size(); ++attempt) {
        position = std::clamp(position + step, 0, int(values_.size() - 1));
        if (values_[position].value) {
            const auto id = values_[position].id;
            return std::exchange(selected_, id) != id;
        }
        if ((position == 0 && step < 0) || (position == int(values_.size() - 1) && step > 0)) {
            break;
        }
    }
    return false;
}

auto DataChart::semantics(Rect bounds) const -> std::vector<SemanticNode> {
    const auto plot = plotBounds(bounds);
    const auto rectangles = cells(plot);
    std::vector<SemanticNode> result;
    const auto limits = axis();
    for (size_t i = 0; i < values_.size(); ++i) {
        const auto& value = values_[i];
        SemanticNode node;
        node.id = value.id;
        node.role = SemanticRole::ListItem;
        node.bounds = kind_ == ChartKind::Donut ? plot : rectangles[i];
        node.name = value.label;
        node.value = value.value ? format(*value.value) : L"—";
        node.number = value.value.value_or(0);
        node.minimum = limits.minimum;
        node.maximum = limits.maximum;
        node.focusable = node.enabled = value.value.has_value();
        node.selected = node.focused = selected_ == value.id;
        result.push_back(std::move(node));
    }
    return result;
}

auto DataChart::draw(Canvas& canvas, Rect bounds, const WidgetTheme& theme, bool tooltip) const -> void {
    const auto plot = plotBounds(bounds);
    if (plot.width <= 0 || plot.height <= 0 || values_.empty()) {
        return;
    }
    canvas.clipped(bounds, [&] {
        if (kind_ == ChartKind::Donut) {
            drawRing(canvas, plot, theme);
        } else {
            const auto rectangles = cells(plot);
            const auto limits = axis();
            if (kind_ == ChartKind::Bars) {
                auto font = theme.font;
                font.size = std::min(11., font.size);
                font.align = TextAlign::End;
                font.wrap = false;
                font.ellipsis = true;
                const auto desired = uint32_t(std::clamp(int(plot.height / 20) + 1, 2, 5));
                for (double tick : limits.ticks(desired)) {
                    const auto y = plot.y + plot.height * (1 - limits.fraction(tick));
                    canvas.line(plot.x, y, plot.x + plot.width, y, theme.border, 1);
                    if (plot.height >= 24) {
                        canvas.text(format(tick), font, {bounds.x, y - 8, 36, 16}, theme.muted);
                    }
                }
            }
            for (size_t i = 0; i < rectangles.size(); ++i) {
                const auto r = rectangles[i];
                if (r.width <= 0 || r.height <= 0) {
                    continue;
                }
                uint32_t color = ink(i, theme);
                if (kind_ == ChartKind::Heatmap) {
                    color = values_[i].value
                                ? blended(theme.track, theme.accent, .15 + .85 * limits.fraction(*values_[i].value))
                                : theme.track;
                }
                if (!values_[i].value && kind_ == ChartKind::Bars) {
                    continue;
                }
                canvas.box(r.x, r.y, r.width, r.height, std::min(3., std::min(r.width, r.height) / 4), color);
                if (selected_ == values_[i].id) {
                    canvas.line(r.x, r.y, r.x + r.width, r.y, theme.foreground, 2);
                }
            }
        }
        if (tooltip && selected_) {
            drawTooltip(canvas, bounds, plot, theme);
        }
    });
}

auto DataChart::drawLegend(Canvas& canvas, Rect bounds, const WidgetTheme& theme) const -> void {
    detail::layoutRect(bounds);
    auto font = theme.font;
    font.ellipsis = true;
    canvas.clipped(bounds, [&] {
        double y = bounds.y;
        for (size_t i = 0; i < values_.size() && y + 20 <= bounds.y + bounds.height; ++i, y += 22) {
            canvas.box(bounds.x, y + 5, 10, 10, 3, ink(i, theme));
            canvas.text(values_[i].label, font, {bounds.x + 18, y, std::max(0., bounds.width - 18), 20},
                        theme.foreground);
        }
    });
}

auto DataChart::contains(Rect r, Point p) -> bool {
    return p.x >= r.x && p.y >= r.y && p.x < r.x + r.width && p.y < r.y + r.height;
}

auto DataChart::plotBounds(Rect bounds) const -> Rect {
    detail::layoutRect(bounds);
    if (kind_ == ChartKind::Bars) {
        return inset(bounds, {44, 8, 4, 8});
    }
    return inset(bounds, {4, 4, 4, 4});
}

auto DataChart::ring(Rect plot) const -> Ring {
    double total = 0;
    for (const auto& value : values_) {
        if (value.value) {
            total += *value.value;
        }
    }
    return {{plot.x + plot.width / 2, plot.y + plot.height / 2}, std::min(plot.width, plot.height) / 2, total};
}

auto DataChart::format(double value) const -> std::wstring {
    if (formatter_) {
        return formatter_(value);
    }
    std::wostringstream stream;
    stream << std::setprecision(4) << value;
    return stream.str();
}

auto DataChart::selection(size_t index, Point anchor) const -> ChartSelection {
    return {values_[index].id, index, values_[index].label, *values_[index].value, anchor};
}

auto DataChart::ink(size_t index, const WidgetTheme& theme) const -> uint32_t {
    if (values_[index].color) {
        return *values_[index].color;
    }
    const uint32_t palette[]{theme.accent, 0x39c8aeff, 0xf1b64cff, 0xa88cf2ff, 0xf48491ff, 0x58b8e8ff};
    return palette[index % std::size(palette)];
}

auto DataChart::cells(Rect plot) const -> std::vector<Rect> {
    std::vector<Rect> result;
    result.reserve(values_.size());
    if (values_.empty()) {
        return result;
    }
    const auto limits = axis();
    for (size_t i = 0; i < values_.size(); ++i) {
        if (kind_ == ChartKind::Heatmap) {
            const auto rows = (values_.size() + columns_ - 1) / columns_;
            const double width = plot.width / columns_, height = plot.height / rows;
            result.push_back({plot.x + (i % columns_) * width, plot.y + (i / columns_) * height,
                              std::max(0., width - 2), std::max(0., height - 2)});
        } else {
            const double width = plot.width / values_.size(), zero = plot.y + plot.height * (1 - limits.fraction(0));
            const double y = plot.y + plot.height * (1 - limits.fraction(values_[i].value.value_or(0)));
            const double height = std::min(plot.height, std::max(1., std::abs(y - zero)));
            result.push_back({plot.x + i * width + width * .1,
                              std::clamp(std::min(y, zero), plot.y, plot.y + std::max(0., plot.height - height)),
                              width * .8, height});
        }
    }
    return result;
}

auto DataChart::drawRing(Canvas& canvas, Rect plot, const WidgetTheme& theme) const -> void {
    const auto geometry = ring(plot);
    if (geometry.radius <= 0 || geometry.total <= 0) {
        return;
    }
    double start = -std::numbers::pi / 2;
    for (size_t i = 0; i < values_.size(); ++i) {
        if (!values_[i].value || *values_[i].value <= 0) {
            continue;
        }
        const double sweep = *values_[i].value / geometry.total * 2 * std::numbers::pi;
        const auto segments = std::max(1, int(std::ceil(sweep / std::numbers::pi)));
        Path path(canvas.factory.Get());
        const auto point = [&](double angle, double radius) {
            return Point{geometry.center.x + std::cos(angle) * radius, geometry.center.y + std::sin(angle) * radius};
        };
        auto p = point(start, geometry.radius);
        path.moveTo(p);
        for (int segment = 1; segment <= segments; ++segment) {
            p = point(start + sweep * segment / segments, geometry.radius);
            path.arcTo(p, {geometry.radius, geometry.radius});
        }
        const double inner = geometry.radius * .58;
        path.lineTo(point(start + sweep, inner));
        for (int segment = segments - 1; segment >= 0; --segment) {
            p = point(start + sweep * segment / segments, geometry.radius * .58);
            path.arcTo(p, {inner, inner}, 0, ArcSweep::CounterClockwise);
        }
        path.close();
        canvas.fill(path, selected_ == values_[i].id ? blended(ink(i, theme), theme.foreground, .15) : ink(i, theme));
        start += sweep;
    }
}

auto DataChart::drawTooltip(Canvas& canvas, Rect bounds, Rect plot, const WidgetTheme& theme) const -> void {
    const auto found = std::find_if(values_.begin(), values_.end(), [&](const auto& value) {
        return value.id == selected_ && value.value;
    });
    if (found == values_.end()) {
        return;
    }
    const auto text = found->label + L"  " + format(*found->value);
    auto font = theme.font;
    font.size = std::min(12., font.size);
    font.ellipsis = true;
    font.align = TextAlign::Center;
    const double width =
        std::min(bounds.width, std::max(40., canvas.measureText(text, font, {bounds.width, 24}).width + 16));
    const Rect box{
        std::clamp(plot.x + plot.width / 2 - width / 2, bounds.x, bounds.x + std::max(0., bounds.width - width)),
        bounds.y, width, std::min(26., bounds.height)};
    canvas.box(box.x, box.y, box.width, box.height, 5, theme.surface);
    canvas.text(text, font, inset(box, {6, 2, 6, 2}), theme.foreground);
}
}  // namespace cybercanvas
