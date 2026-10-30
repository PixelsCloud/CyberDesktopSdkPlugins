#include "CyberCanvas/Components.h"

namespace cybercanvas {
FlexLayout::Measure measuredText(Canvas& canvas, std::wstring text, TextStyle style) {
    return [&canvas, text = std::move(text), style = std::move(style)](MeasureConstraint constraint) {
        const double width = constraint.widthMode == MeasureMode::Undefined ? 1e6 : constraint.width;
        const double height = constraint.heightMode == MeasureMode::Undefined ? 1e6 : constraint.height;
        if (text.empty()) {
            return Size{};
        }
        const auto result = canvas.measureText(text, style, {std::max(.01, width), std::max(.01, height)});
        return Size{result.widthIncludingTrailingWhitespace, result.height};
    };
}

HeaderParts titleActions(Rect area, size_t count, double edge, double gap, double dpr) {
    if (count > 64) {
        throw std::length_error("Too many header actions");
    }
    FlexStyle row;
    row.axis = Axis::Horizontal;
    row.align = FlexAlign::Center;
    row.gap = gap;
    FlexLayout tree(row);
    FlexStyle text;
    text.grow = 1;
    text.basis = Length::points(0);
    text.height = Length::percent(100);
    const auto title = tree.add(0, text);
    std::vector<FlexLayout::Node> actions;
    FlexStyle action;
    action.width = Length::points(edge);
    action.height = Length::points(std::min(edge, area.height));
    for (size_t i = 0; i < count; ++i) {
        actions.push_back(tree.add(0, action));
    }
    tree.calculate(area, dpr);
    HeaderParts result{tree.clippedBounds(title)};
    for (auto id : actions) {
        result.actions.push_back(tree.clippedBounds(id));
    }
    return result;
}

FlowParts wrappingRow(Rect area, const std::vector<Size>& preferred, double gap, bool expand, double dpr, bool rtl) {
    FlexStyle root;
    root.axis = Axis::Horizontal;
    root.wrap = true;
    root.gap = gap;
    FlexLayout tree(root);
    std::vector<FlexLayout::Node> nodes;
    for (auto size : preferred) {
        FlexStyle item;
        item.basis = Length::points(size.width);
        item.height = Length::points(size.height);
        item.grow = expand ? 1 : 0;
        item.shrink = 1;
        nodes.push_back(tree.add(0, item));
    }
    tree.calculate(area, dpr, rtl);
    FlowParts result;
    for (auto id : nodes) {
        const auto rect = tree.clippedBounds(id);
        result.items.push_back(rect);
        result.height = std::max(result.height, rect.y + rect.height - area.y);
    }
    return result;
}

TextParts textSections(Canvas& canvas, Rect area, const std::wstring& title, TextStyle style, double titleLimit,
                       double footerHeight, double gap, double dpr) {
    FlexStyle column;
    column.gap = gap;
    FlexLayout tree(column);
    FlexStyle titleStyle;
    titleStyle.shrink = 0;
    titleStyle.maximum = Size{1e7, std::max(0., std::min(titleLimit, area.height - footerHeight - 2 * gap))};
    const auto heading = tree.add(0, titleStyle, measuredText(canvas, title, style));
    FlexStyle body;
    body.grow = 1;
    body.basis = Length::points(0);
    const auto summary = tree.add(0, body);
    FlexStyle footer;
    footer.height = Length::points(std::min(footerHeight, area.height));
    footer.shrink = 0;
    const auto meta = tree.add(0, footer);
    tree.calculate(area, dpr);
    return {tree.clippedBounds(heading), tree.clippedBounds(summary), tree.clippedBounds(meta)};
}

auto IconLabel::draw(Canvas& canvas, const WidgetTheme& theme, const IconPainter& painter) const -> void {
    if (bounds.width <= 0 || bounds.height <= 0) {
        return;
    }
    FlexStyle row;
    row.axis = Axis::Horizontal;
    row.align = FlexAlign::Center;
    row.gap = gap;
    FlexLayout tree(row);
    FlexStyle picture;
    picture.width = picture.height = Length::points(std::min(iconSize, bounds.height));
    picture.shrink = 0;
    const auto image = tree.add(0, picture);
    FlexStyle text;
    text.grow = 1;
    const auto title = tree.add(0, text, measuredText(canvas, label, theme.font));
    tree.calculate(bounds, canvas.devicePixelRatio());
    if (painter) {
        painter(canvas, tree.clippedBounds(image), icon, theme.foreground);
    }
    const auto area = tree.clippedBounds(title);
    auto font = theme.font;
    font.ellipsis = true;
    font.align = TextAlign::Start;
    font.vertical = VerticalAlign::Center;
    canvas.clipped(area, [&] {
        canvas.text(label, font, area, theme.foreground);
    });
}

auto TitleActions::layout(double dpr) const -> HeaderParts {
    return titleActions(bounds, actions.size(), edge, gap, dpr);
}

auto TitleActions::draw(Canvas& canvas, const WidgetTheme& theme, const WidgetState& state,
                        const IconPainter& painter) const -> void {
    const auto parts = layout(canvas.devicePixelRatio());
    auto font = theme.font;
    font.ellipsis = true;
    font.align = TextAlign::Start;
    font.vertical = VerticalAlign::Center;
    canvas.clipped(parts.title, [&] {
        canvas.text(title, font, parts.title, theme.foreground);
    });
    for (size_t i = 0; i < actions.size(); ++i) {
        auto button = actions[i];
        button.bounds = parts.actions[i];
        button.draw(canvas, theme, painter, state.visual(button.id));
    }
}

auto LabelledSlider::layout(Canvas& canvas, const WidgetTheme& theme) const -> Parts {
    FlexStyle column;
    column.gap = gap;
    column.justify = FlexJustify::SpaceBetween;
    FlexLayout tree(column);
    FlexStyle text;
    text.maximum = Size{1e7, std::max(0., bounds.height - slider.bounds.height - gap)};
    auto font = theme.font;
    font.wrap = true;
    const auto title = tree.add(0, text, measuredText(canvas, label, font));
    FlexStyle track;
    track.height = Length::points(std::min(slider.bounds.height, bounds.height));
    track.shrink = 0;
    const auto control = tree.add(0, track);
    tree.calculate(bounds, canvas.devicePixelRatio());
    auto result = slider;
    result.bounds = tree.clippedBounds(control);
    return {tree.clippedBounds(title), result};
}

auto LabelledSlider::change(Canvas& canvas, const WidgetTheme& theme, const PointerResult& input) const
    -> std::optional<double> {
    return layout(canvas, theme).slider.change(input);
}

auto TitleActions::regions(Point origin, double dpr) const -> std::vector<HitRegion> {
    const auto parts = layout(dpr);
    std::vector<HitRegion> result;
    for (size_t i = 0; i < actions.size(); ++i) {
        auto button = actions[i];
        button.bounds = parts.actions[i];
        result.push_back(button.region(origin));
    }
    return result;
}

auto LabelledSlider::region(Canvas& canvas, const WidgetTheme& theme, Point origin) const -> HitRegion {
    auto result = layout(canvas, theme).slider.region(origin);
    result.clips.push_back(
        HitShape::rectangle({bounds.x + origin.x, bounds.y + origin.y, bounds.width, bounds.height}));
    return result;
}

auto LabelledSlider::draw(Canvas& canvas, const WidgetTheme& theme, double value, ControlVisual visual) const -> void {
    const auto parts = layout(canvas, theme);
    auto font = theme.font;
    font.wrap = font.ellipsis = true;
    font.align = TextAlign::Start;
    canvas.clipped(parts.label, [&] {
        canvas.text(label, font, parts.label, theme.foreground);
    });
    parts.slider.draw(canvas, theme, value, visual);
}
}  // namespace cybercanvas
