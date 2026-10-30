#include "CyberCanvas/DataWidgets.h"

namespace cybercanvas {
auto ContentNotice::draw(Canvas& canvas, const WidgetTheme& theme) const -> void {
    cybercanvas::detail::layoutRect(bounds);
    if (bounds.width <= 0 || bounds.height <= 0) {
        return;
    }
    canvas.clipped(bounds, [&] {
        if (background) {
            canvas.box(bounds.x, bounds.y, bounds.width, bounds.height, theme.radius, theme.surface);
        }
        const auto area = inset(bounds, {theme.padding, theme.padding, theme.padding, theme.padding});
        if (area.width <= 0 || area.height <= 0) {
            return;
        }
        auto font = theme.font;
        font.wrap = true;
        font.ellipsis = true;
        font.vertical = VerticalAlign::Top;
        const auto color = state == ContentState::Error   ? theme.error
                           : state == ContentState::Ready ? theme.foreground
                                                          : theme.muted;
        double heading = 0;
        if (!title.empty()) {
            font.weight = FontWeight::SemiBold;
            heading = std::min(area.height, canvas.measureText(title, font, {area.width, area.height}).height);
            canvas.text(title, font, {area.x, area.y, area.width, heading}, color);
        }
        const double top = heading + (heading > 0 && !detail.empty() ? theme.gap : 0);
        if (!detail.empty() && top < area.height) {
            font.weight = theme.font.weight;
            canvas.text(detail, font, {area.x, area.y + top, area.width, area.height - top}, theme.muted);
        }
    });
}

ChoiceGroup::ChoiceGroup(std::vector<ChoiceOption> options) : options_(std::move(options)) {
    if (options_.size() > 64) {
        throw std::length_error("Choice group limit reached");
    }
    std::set<uint32_t> ids;
    for (const auto& option : options_) {
        detail::layoutRect(option.bounds);
        if (!option.id || !ids.insert(option.id).second) {
            throw std::invalid_argument("Choice IDs must be unique and nonzero");
        }
    }
}

auto ChoiceGroup::options() const -> const std::vector<ChoiceOption>& {
    return options_;
}

auto ChoiceGroup::change(const PointerResult& result) const -> std::optional<uint32_t> {
    for (const auto& option : options_) {
        if (option.enabled && option.id == result.clicked) {
            return option.id;
        }
    }
    return {};
}

auto MetricReadout::draw(Canvas& canvas, const WidgetTheme& theme) const -> void {
    cybercanvas::detail::layoutRect(bounds);
    canvas.clipped(bounds, [&] {
        Stack stack(inset(bounds, {theme.padding, theme.padding, theme.padding, theme.padding}), Axis::Vertical,
                    theme.gap);
        auto caption = theme.font;
        caption.wrap = false;
        caption.ellipsis = true;
        if (!label.empty()) {
            const auto measured = canvas.measureText(label, caption, {std::max(.01, bounds.width), 1e6});
            canvas.text(label, caption, stack.take(measured.height), theme.muted);
        }
        const double footer =
            detail.empty() ? 0 : canvas.measureText(detail, caption, {std::max(.01, bounds.width), 1e6}).height;
        auto number = theme.font;
        number.size = std::max(theme.font.size, std::min(30., bounds.height * .35));
        number.weight = FontWeight::SemiBold;
        number.wrap = false;
        number.ellipsis = true;
        number.vertical = VerticalAlign::Center;
        const auto text = state == ContentState::Loading                                 ? std::wstring(L"…")
                          : state == ContentState::Error || state == ContentState::Empty ? std::wstring(L"—")
                                                                                         : value;
        canvas.text(text, number, stack.take(std::max(0., stack.remaining().height - footer - theme.gap)),
                    state == ContentState::Ready   ? theme.foreground
                    : state == ContentState::Error ? theme.error
                                                   : theme.muted);
        if (footer > 0) {
            canvas.text(detail, caption, stack.take(footer), theme.muted);
        }
    });
}

auto LineChart::samples(std::vector<double> values) -> bool {
    if (values.size() > 4096) {
        throw std::length_error("Chart sample limit reached");
    }
    const bool equal = values.size() == values_.size() &&
                       std::equal(values.begin(), values.end(), values_.begin(), [](double a, double b) {
                           return a == b || (!std::isfinite(a) && !std::isfinite(b));
                       });
    if (equal) {
        return false;
    }
    values_ = std::move(values);
    dirty_ = true;
    return true;
}

auto LineChart::range(std::optional<std::pair<double, double>> limits) -> void {
    if (limits &&
        (!std::isfinite(limits->first) || !std::isfinite(limits->second) || limits->first >= limits->second)) {
        throw std::invalid_argument("Invalid chart range");
    }
    if (limits_ != limits) {
        limits_ = limits;
        dirty_ = true;
    }
}

auto LineChart::points(Rect bounds) const -> std::vector<std::vector<Point>> {
    detail::layoutRect(bounds);
    std::vector<std::vector<Point>> result;
    if (values_.empty() || bounds.width <= 0 || bounds.height <= 0) {
        return result;
    }
    double minimum = 0, maximum = 1;
    if (limits_) {
        std::tie(minimum, maximum) = *limits_;
    } else {
        bool first = true;
        for (double value : values_) {
            if (std::isfinite(value)) {
                minimum = first ? value : std::min(minimum, value);
                maximum = first ? value : std::max(maximum, value);
                first = false;
            }
        }
    }
    bool start = true;
    for (size_t i = 0; i < values_.size(); ++i) {
        if (!std::isfinite(values_[i])) {
            start = true;
            continue;
        }
        if (start) {
            result.emplace_back();
            start = false;
        }
        const double scale = std::max({std::abs(minimum), std::abs(maximum), 1e-300});
        const double span = maximum / scale - minimum / scale;
        const double fraction = span > 0 ? std::clamp((values_[i] / scale - minimum / scale) / span, 0., 1.) : .5;
        result.back().push_back(
            {bounds.x + bounds.width * (values_.size() == 1 ? .5 : double(i) / (values_.size() - 1)),
             bounds.y + bounds.height * (1 - fraction)});
    }
    return result;
}

auto LineChart::draw(Canvas& canvas, Rect bounds, const WidgetTheme& theme, double width) -> void {
    detail::layoutRect(bounds);
    if (!std::isfinite(width) || width <= 0) {
        throw std::invalid_argument("Invalid chart stroke");
    }
    if (bounds.width <= 0 || bounds.height <= 0) {
        return;
    }
    const bool moved = area_.x != bounds.x || area_.y != bounds.y || area_.width != bounds.width ||
                       area_.height != bounds.height || factory_.Get() != canvas.factory.Get();
    if (dirty_ || moved) {
        path_.reset();
        singles_.clear();
        auto path = canvas.path();
        bool segments = false;
        for (const auto& segment : points(bounds)) {
            if (segment.size() == 1) {
                singles_.push_back(segment.front());
            } else if (!segment.empty()) {
                path.moveTo(segment.front());
                for (size_t i = 1; i < segment.size(); ++i) {
                    path.lineTo(segment[i]);
                }
                segments = true;
            }
        }
        if (segments) {
            path_ = std::move(path.finish());
        }
        factory_ = canvas.factory;
        area_ = bounds;
        dirty_ = false;
        ++builds_;
    }
    canvas.clipped(bounds, [&] {
        if (path_) {
            canvas.stroke(*path_, theme.accent, {.width = width, .cap = LineCap::Round, .join = LineJoin::Round});
        }
        for (auto point : singles_) {
            canvas.oval(point.x, point.y, width * .5, width * .5, theme.accent);
        }
    });
}

auto LineChart::geometryBuilds() const -> uint64_t {
    return builds_;
}

auto MeterGauge::draw(Canvas& canvas, Rect area, double fraction, const WidgetTheme& theme) -> void {
    canvas.saved([&] {
        draw(canvas.factory.Get(), canvas.target.Get(), canvas.brush.Get(), area, fraction, theme);
    });
}

auto MeterGauge::draw(ID2D1Factory* factory, ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush, Rect area,
                      double fraction, const WidgetTheme& theme) -> void {
    detail::layoutRect(area);
    if (!factory || !target || !brush || !std::isfinite(fraction)) {
        throw std::invalid_argument("Invalid gauge context or value");
    }
    if (area.width <= 0 || area.height <= 0) {
        return;
    }
    if (factory_.Get() != factory) {
        paths_.clear();
        factory_ = factory;
        for (int i = 0; i < 24; ++i) {
            const double start = 130 + i * 280. / 24;
            paths_.push_back(arc(start, start + 280. / 24 - 3));
        }
    }

    struct Restore {
        ID2D1RenderTarget* target;
        ID2D1SolidColorBrush* brush;
        D2D1_MATRIX_3X2_F transform;
        D2D1_COLOR_F ink;

        ~Restore() {
            target->SetTransform(transform);
            brush->SetColor(ink);
        }
    } restore{target, brush, {}, brush->GetColor()};

    target->GetTransform(&restore.transform);
    target->SetTransform(D2D1::Matrix3x2F::Scale(float(area.width / 48), float(area.height / 48)) *
                         D2D1::Matrix3x2F::Translation(float(area.x), float(area.y)) * restore.transform);
    fraction = std::clamp(fraction, 0., 1.);
    for (int i = 0; i < 24; ++i) {
        brush->SetColor(color(theme.track));
        target->DrawGeometry(paths_[i].Get(), brush, 2.1f);
        const double amount = std::clamp(fraction * 24 - i, 0., 1.);
        if (amount > 0) {
            const double start = 130 + i * 280. / 24;
            auto active = amount >= 1 ? paths_[i] : arc(start, start + (280. / 24 - 3) * amount);
            brush->SetColor(color(theme.accent));
            target->DrawGeometry(active.Get(), brush, 2.1f);
        }
    }
}

auto MeterGauge::arc(double start, double end) -> ComPtr<ID2D1PathGeometry> {
    const auto point = [](double angle) {
        const auto radians = angle * 3.14159265358979323846 / 180;
        return D2D1::Point2F(float(24 + 21 * std::cos(radians)), float(24 + 21 * std::sin(radians)));
    };
    ComPtr<ID2D1PathGeometry> path;
    ComPtr<ID2D1GeometrySink> sink;
    require(factory_->CreatePathGeometry(path.GetAddressOf()));
    require(path->Open(sink.GetAddressOf()));
    sink->BeginFigure(point(start), D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddArc(
        D2D1::ArcSegment(point(end), D2D1::SizeF(21, 21), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    require(sink->Close());
    return path;
}

auto ChoiceGroup::regions(Point origin) const -> std::vector<HitRegion> {
    std::vector<HitRegion> result;
    for (const auto& option : options_) {
        Button button(option.id, option.bounds, option.label);
        button.enabled = option.enabled;
        result.push_back(button.region(origin));
    }
    return result;
}

auto ChoiceGroup::draw(Canvas& canvas, const WidgetTheme& theme, uint32_t selected,
                       const std::function<ControlVisual(uint32_t)>& visual, const IconPainter& icons) const -> void {
    for (const auto& option : options_) {
        Button button(option.id, option.bounds, option.label);
        button.enabled = option.enabled;
        button.selected = option.id == selected;
        auto colors = theme;
        colors.font.weight = button.selected ? FontWeight::SemiBold : theme.font.weight;
        const auto feedback = visual ? visual(option.id) : ControlVisual{};
        if (option.icon.empty()) {
            button.draw(canvas, colors, feedback);
        } else {
            IconButton icon(option.id, option.bounds, option.icon, option.label);
            static_cast<Button&>(icon) = button;
            icon.draw(canvas, colors, icons, feedback);
        }
    }
}
}  // namespace cybercanvas
