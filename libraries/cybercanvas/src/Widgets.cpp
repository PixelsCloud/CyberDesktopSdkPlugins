#include "CyberCanvas/Widgets.h"

namespace cybercanvas {
uint32_t blended(uint32_t from, uint32_t to, double amount) {
    if (!std::isfinite(amount)) {
        throw std::invalid_argument("Invalid blend amount");
    }
    amount = std::clamp(amount, 0., 1.);
    uint32_t result = 0;
    for (int shift : {24, 16, 8, 0}) {
        const auto a = (from >> shift) & 255, b = (to >> shift) & 255;
        result |= uint32_t(std::lround(a + (double(b) - a) * amount)) << shift;
    }
    return result;
}

auto WidgetTheme::dark() -> WidgetTheme {
    return {};
}

auto WidgetTheme::light() -> WidgetTheme {
    WidgetTheme result;
    result.foreground = 0x243044FF;
    result.muted = 0x687A91FF;
    result.accent = 0x237C85FF;
    result.surface = 0xEEF2F7FF;
    result.border = result.track = 0xCCD6E2FF;
    result.onAccent = 0xFFFFFFFF;
    result.error = 0xB33746FF;
    return result;
}

auto WidgetTheme::desktop(bool lightAppearance) -> WidgetTheme {
    auto result = lightAppearance ? light() : dark();
    result.radius = 7;
    result.padding = 10;
    result.gap = 8;
    result.thumbRadius = 5;
    result.controlHeight = 32;
    return result;
}

auto WidgetState::sync(const InputRouter& router) -> void {
    hovered_ = router.hovered();
    pressed_ = router.pressed();
    ensure(hovered_);
    ensure(pressed_);
    for (auto& [id, track] : tracks_) {
        track.target(id == pressed_ ? 1 : id == hovered_ ? .55 : 0);
    }
    prune();
}

auto WidgetState::advance(double seconds) -> bool {
    if (!std::isfinite(seconds) || seconds < 0) {
        throw std::invalid_argument("Invalid widget feedback time");
    }
    bool changed = false;
    for (auto& [id, track] : tracks_) {
        changed |= track.advance(seconds);
    }
    prune();
    return changed;
}

auto WidgetState::active() const -> bool {
    return std::any_of(tracks_.begin(), tracks_.end(), [](const auto& pair) {
        return pair.second.active();
    });
}

auto WidgetState::visual(uint32_t id) const -> ControlVisual {
    const auto it = tracks_.find(id);
    return {id != 0 && id == hovered_, id != 0 && id == pressed_, it == tracks_.end() ? 0 : it->second.value()};
}

auto WidgetState::size() const -> size_t {
    return tracks_.size();
}

auto WidgetState::ensure(uint32_t id) -> void {
    if (!id || tracks_.contains(id)) {
        return;
    }
    if (tracks_.size() >= 256) {
        const auto discard = std::find_if(tracks_.begin(), tracks_.end(), [&](const auto& pair) {
            return pair.first != hovered_ && pair.first != pressed_;
        });
        if (discard != tracks_.end()) {
            tracks_.erase(discard);
        }
    }
    tracks_.try_emplace(id);
}

auto WidgetState::prune() -> void {
    std::erase_if(tracks_, [](const auto& pair) {
        return !pair.second.active() && pair.second.value() == 0;
    });
}

auto Control::region(Point origin, PointerBehavior behavior, bool round) const -> HitRegion {
    auto shape = round ? HitShape::ellipse(bounds) : HitShape::rectangle(bounds);
    shape = shape.transformed(D2D1::Matrix3x2F::Translation(float(origin.x), float(origin.y)));
    return {id, std::move(shape), behavior, priority, enabled && bounds.width > 0 && bounds.height > 0};
}

auto Control::clicked(const PointerResult& result) const -> bool {
    return enabled && id && result.clicked == id;
}

void widgetLabel(Canvas& canvas, std::wstring_view label, Rect bounds, const WidgetTheme& theme, uint32_t color) {
    if (bounds.width <= 0 || bounds.height <= 0 || label.empty()) {
        return;
    }
    auto font = theme.font;
    font.align = TextAlign::Center;
    font.vertical = VerticalAlign::Center;
    font.wrap = false;
    font.ellipsis = true;
    canvas.clipped(bounds, [&] {
        canvas.text(std::wstring(label), font, bounds, color);
    });
}

Button::Button(uint32_t key, Rect area, std::wstring text, ButtonStyle appearance)
    : Control{key, area}, label(std::move(text)), style(appearance) {}

auto Button::region(Point origin) const -> HitRegion {
    return Control::region(origin, PointerBehavior::Button, round);
}

auto Button::foreground(const WidgetTheme& theme) const -> uint32_t {
    return style == ButtonStyle::Filled || selected ? theme.onAccent : theme.foreground;
}

auto Button::background(Canvas& canvas, const WidgetTheme& theme, ControlVisual visual, bool applyDisabled) const
    -> void {
    if (bounds.width <= 0 || bounds.height <= 0) {
        return;
    }
    uint32_t fill = style == ButtonStyle::Filled || selected ? theme.accent : theme.surface;
    fill = blended(fill, theme.foreground, visual.feedback * .12);
    const auto shape = [&](uint32_t rgba, double stroke = 0) {
        if (round) {
            canvas.oval(bounds.x + bounds.width * .5, bounds.y + bounds.height * .5, bounds.width * .5,
                        bounds.height * .5, rgba, stroke);
        } else if (stroke) {
            canvas.strokeBox(bounds, theme.radius, rgba, {.width = stroke});
        } else {
            canvas.box(bounds.x, bounds.y, bounds.width, bounds.height, theme.radius, rgba);
        }
    };
    detail::widgetOpacity(canvas, !applyDisabled || enabled ? 1 : theme.disabledOpacity, [&] {
        if (style == ButtonStyle::Filled || selected) {
            shape(fill);
        } else if (visual.feedback != 0) {
            detail::widgetOpacity(canvas, visual.feedback, [&] {
                shape(fill);
            });
        }
        if (style == ButtonStyle::Outline) {
            shape(theme.border, 1);
        }
    });
}

auto Button::draw(Canvas& canvas, const WidgetTheme& theme, ControlVisual visual) const -> void {
    if (bounds.width <= 0 || bounds.height <= 0) {
        return;
    }
    detail::widgetOpacity(canvas, enabled ? 1 : theme.disabledOpacity, [&] {
        background(canvas, theme, visual, false);
        widgetLabel(canvas, label, inset(bounds, {theme.padding, 0, theme.padding, 0}), theme, foreground(theme));
    });
}

IconButton::IconButton(uint32_t key, Rect area, std::string name, std::wstring fallback, ButtonStyle appearance)
    : Button(key, area, std::move(fallback), appearance), icon(std::move(name)) {}

auto IconButton::draw(Canvas& canvas, const WidgetTheme& theme, const IconPainter& paint, ControlVisual visual) const
    -> void {
    if (bounds.width <= 0 || bounds.height <= 0) {
        return;
    }
    const auto area = aligned(bounds, {std::min(iconSize, bounds.width), std::min(iconSize, bounds.height)});
    // Icon providers are arbitrary callbacks: retain their original layer scope.
    canvas.withOpacity(enabled ? 1 : theme.disabledOpacity, [&] {
        background(canvas, theme, visual, false);
        if (!paint || !paint(canvas, area, icon, foreground(theme))) {
            widgetLabel(canvas, label, bounds, theme, foreground(theme));
        }
    });
}

auto ProgressBar::fraction(double value) const -> double {
    if (!std::isfinite(value) || !std::isfinite(minimum) || !std::isfinite(maximum) || maximum <= minimum ||
        !std::isfinite(maximum - minimum)) {
        throw std::invalid_argument("Invalid control range or value");
    }
    return std::clamp((value - minimum) / (maximum - minimum), 0., 1.);
}

auto ProgressBar::draw(Canvas& canvas, const WidgetTheme& theme, double value) const -> void {
    const double amount = fraction(value);
    if (bounds.width <= 0 || bounds.height <= 0) {
        return;
    }
    const double radius = std::min(theme.radius, bounds.height * .5);
    canvas.box(bounds.x, bounds.y, bounds.width, bounds.height, radius, theme.track);
    canvas.box(bounds.x, bounds.y, bounds.width * amount, bounds.height, radius, theme.accent);
}

Slider::Slider(uint32_t key, Rect area, double low, double high, double increment)
    : Control{key, area}, minimum(low), maximum(high), step(increment) {}

auto Slider::region(Point origin) const -> HitRegion {
    auto hit = *this;
    if (!std::isfinite(hitSlop) || hitSlop < 0) {
        throw std::invalid_argument("Invalid slider hit slop");
    }
    hit.bounds = {bounds.x - hitSlop, bounds.y - hitSlop, bounds.width + 2 * hitSlop, bounds.height + 2 * hitSlop};
    hit.enabled = enabled && bounds.width > 0 && bounds.height > 0;
    return hit.Control::region(origin, PointerBehavior::Drag);
}

auto Slider::change(const PointerResult& event) const -> std::optional<double> {
    if (!enabled || event.target != id || !id || bounds.width <= 0 ||
        (event.drag != DragPhase::Begin && event.drag != DragPhase::Update && event.drag != DragPhase::End)) {
        return {};
    }
    ProgressBar range{bounds, minimum, maximum};
    range.fraction(minimum);
    if (!std::isfinite(step) || step < 0 || !std::isfinite(event.local.x)) {
        throw std::invalid_argument("Invalid slider step or position");
    }
    const double amount = std::clamp((event.local.x - bounds.x) / bounds.width, 0., 1.);
    if (amount == 0) {
        return minimum;
    }
    if (amount == 1) {
        return maximum;
    }
    double value = minimum + amount * (maximum - minimum);
    if (step > 0) {
        value = minimum + std::round((value - minimum) / step) * step;
    }
    return std::clamp(value, minimum, maximum);
}

auto Slider::draw(Canvas& canvas, const WidgetTheme& theme, double value, ControlVisual visual) const -> void {
    const ProgressBar progress{bounds, minimum, maximum};
    const double amount = progress.fraction(value);
    if (bounds.width <= 0 || bounds.height <= 0) {
        return;
    }
    detail::widgetOpacity(canvas, enabled ? 1 : theme.disabledOpacity, [&] {
        progress.draw(canvas, theme, value);
        const double x = bounds.x + bounds.width * amount, y = bounds.y + bounds.height * .5;
        const double radius = theme.thumbRadius + visual.feedback * 1.6;
        if (visual.feedback != 0) {
            canvas.withOpacity(visual.feedback * .2, [&] {
                canvas.oval(x, y, radius + 4, radius + 4, theme.accent);
            });
        }
        canvas.oval(x, y + .7, radius + .5, radius + .5, 0x00000030);
        canvas.oval(x, y, radius, radius, theme.accent);
        canvas.oval(x, y, radius, radius, theme.foreground, .6);
    });
}

Toggle::Toggle(uint32_t key, Rect area) : Control{key, area} {}

auto Toggle::change(const PointerResult& event, bool value) const -> std::optional<bool> {
    return clicked(event) ? std::optional<bool>(!value) : std::nullopt;
}

auto Toggle::draw(Canvas& canvas, const WidgetTheme& theme, bool value, ControlVisual visual) const -> void {
    if (bounds.width <= 0 || bounds.height <= 0) {
        return;
    }
    const double h = std::min(bounds.height, bounds.width * .6), w = std::min(bounds.width, h * 1.8);
    const auto area = aligned(bounds, {w, h});
    detail::widgetOpacity(canvas, enabled ? 1 : theme.disabledOpacity, [&] {
        canvas.box(area.x, area.y, w, h, h * .5,
                   blended(value ? theme.accent : theme.track, theme.foreground, visual.feedback * .1));
        const double radius = h * .5 - 2;
        if (radius > 0) {
            canvas.oval(area.x + (value ? w - h * .5 : h * .5), area.y + h * .5, radius, radius,
                        value ? theme.onAccent : theme.foreground);
        }
    });
}
}  // namespace cybercanvas
