#pragma once

#include "CyberCanvas/Flex.h"
#include "CyberCanvas/Widgets.h"

namespace cybercanvas {
FlexLayout::Measure measuredText(Canvas& canvas, std::wstring text, TextStyle style);

struct HeaderParts {
    Rect title;
    std::vector<Rect> actions;
};

HeaderParts titleActions(Rect area, size_t count, double edge = 24, double gap = 3, double dpr = 1);

struct FlowParts {
    std::vector<Rect> items;
    double height = 0;
};

// Yoga handles packing and line breaks; preferred width is the flex basis.
FlowParts wrappingRow(Rect area, const std::vector<Size>& preferred, double gap = 3, bool expand = true, double dpr = 1,
                      bool rtl = false);

struct TextParts {
    Rect title, body, footer;
};

TextParts textSections(Canvas& canvas, Rect area, const std::wstring& title, TextStyle style, double titleLimit,
                       double footerHeight = 13, double gap = 3, double dpr = 1);

struct IconLabel {
    Rect bounds;
    std::string icon;
    std::wstring label;
    double iconSize = 16, gap = 6;

    void draw(Canvas& canvas, const WidgetTheme& theme, const IconPainter& painter) const;
};

struct TitleActions {
    Rect bounds;
    std::wstring title;
    std::vector<IconButton> actions;
    double edge = 24, gap = 3;

    HeaderParts layout(double dpr = 1) const;

    std::vector<HitRegion> regions(Point origin = {}, double dpr = 1) const;

    void draw(Canvas& canvas, const WidgetTheme& theme, const WidgetState& state, const IconPainter& painter) const;
};

struct LabelledSlider {
    Rect bounds;
    std::wstring label;
    Slider slider;
    double gap = 6;

    struct Parts {
        Rect label;
        Slider slider;
    };

    Parts layout(Canvas& canvas, const WidgetTheme& theme) const;

    HitRegion region(Canvas& canvas, const WidgetTheme& theme, Point origin = {}) const;

    std::optional<double> change(Canvas& canvas, const WidgetTheme& theme, const PointerResult& input) const;

    void draw(Canvas& canvas, const WidgetTheme& theme, double value, ControlVisual visual = {}) const;
};
}  // namespace cybercanvas
