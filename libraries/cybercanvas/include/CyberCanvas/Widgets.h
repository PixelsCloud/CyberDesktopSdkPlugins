#pragma once
#include <functional>
#include <map>
#include <string_view>

#include "CyberCanvas/Canvas.h"
#include "CyberCanvas/Interaction.h"

namespace cybercanvas {
namespace detail {
// These built-in painters only add primitives. Fully opaque controls need state
// isolation, but not an offscreen group. Keep Canvas::withOpacity unchanged for
// arbitrary callbacks, which may use native Clear or other layer-local commands.
template <typename Paint>
void widgetOpacity(Canvas& canvas, double opacity, Paint&& paint) {
    if (opacity == 1) {
        canvas.saved(std::forward<Paint>(paint));
    } else {
        canvas.withOpacity(opacity, std::forward<Paint>(paint));
    }
}
}  // namespace detail

uint32_t blended(uint32_t from, uint32_t to, double amount);

struct WidgetTheme {
    uint32_t foreground = 0xE6EEF8FF, muted = 0x9BA9BBFF, accent = 0x6DD4C7FF;
    uint32_t surface = 0x253142FF, border = 0x42516AFF, track = 0x42516AFF, onAccent = 0x102A29FF;
    uint32_t error = 0xF39090FF;
    double radius = 6, padding = 8, gap = 6, thumbRadius = 4;
    TextStyle font{};
    // Appended tokens preserve existing aggregate initializers and compact defaults.
    double controlHeight = 0, disabledOpacity = .4;

    static WidgetTheme dark();

    static WidgetTheme light();

    static WidgetTheme desktop(bool lightAppearance = false);
};

struct ControlVisual {
    bool hovered = false, pressed = false;
    double feedback = 0;
};

// Tracks only controls with visible feedback. A caller owns frame scheduling.
class WidgetState {
public:
    void sync(const InputRouter& router);

    bool advance(double seconds);

    bool active() const;

    ControlVisual visual(uint32_t id) const;

    size_t size() const;

private:
    void ensure(uint32_t id);

    void prune();

    std::map<uint32_t, InteractionFeedback> tracks_;
    uint32_t hovered_ = 0, pressed_ = 0;
};

enum class ButtonStyle { Filled, Outline, Quiet };
using IconPainter = std::function<bool(Canvas&, Rect, std::string_view, uint32_t)>;

struct Control {
    uint32_t id = 0;
    Rect bounds{};
    bool enabled = true;
    int priority = 0;

    HitRegion region(Point origin = {}, PointerBehavior behavior = PointerBehavior::Button, bool round = false) const;

    bool clicked(const PointerResult& result) const;
};

void widgetLabel(Canvas& canvas, std::wstring_view label, Rect bounds, const WidgetTheme& theme, uint32_t color);

struct Button : Control {
    std::wstring label;
    ButtonStyle style = ButtonStyle::Quiet;
    bool selected = false, round = false;

    Button(uint32_t key, Rect area, std::wstring text = {}, ButtonStyle appearance = ButtonStyle::Quiet);

    HitRegion region(Point origin = {}) const;

    uint32_t foreground(const WidgetTheme& theme) const;

    void background(Canvas& canvas, const WidgetTheme& theme, ControlVisual visual, bool applyDisabled = true) const;

    void draw(Canvas& canvas, const WidgetTheme& theme, ControlVisual visual = {}) const;
};

struct IconButton : Button {
    std::string icon;
    double iconSize = 16;

    IconButton(uint32_t key, Rect area, std::string name, std::wstring fallback = {},
               ButtonStyle appearance = ButtonStyle::Quiet);

    void draw(Canvas& canvas, const WidgetTheme& theme, const IconPainter& paint, ControlVisual visual = {}) const;
};

struct ProgressBar {
    Rect bounds{};
    double minimum = 0, maximum = 1;

    double fraction(double value) const;

    void draw(Canvas& canvas, const WidgetTheme& theme, double value) const;
};

struct Slider : Control {
    double minimum = 0, maximum = 1, step = 0, hitSlop = 8;

    Slider(uint32_t key, Rect area, double low = 0, double high = 1, double increment = 0);

    HitRegion region(Point origin = {}) const;

    std::optional<double> change(const PointerResult& event) const;

    void draw(Canvas& canvas, const WidgetTheme& theme, double value, ControlVisual visual = {}) const;
};

struct Toggle : Control {
    explicit Toggle(uint32_t key, Rect area);

    std::optional<bool> change(const PointerResult& event, bool value) const;

    void draw(Canvas& canvas, const WidgetTheme& theme, bool value, ControlVisual visual = {}) const;
};
}  // namespace cybercanvas
