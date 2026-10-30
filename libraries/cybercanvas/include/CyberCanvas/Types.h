#pragma once

#include <cstdint>
#include <string>

namespace cybercanvas {
struct Point {
    double x = 0, y = 0;
};

struct Size {
    double width = 0, height = 0;
};

struct Rect {
    double x = 0, y = 0, width = 0, height = 0;
};

struct Insets {
    double left = 0, top = 0, right = 0, bottom = 0;
};

struct Pose {
    Point translation{}, pivot{};
    Size scale{1, 1};
    double rotation = 0, opacity = 1;
};

enum class Axis { Horizontal, Vertical };
enum class Alignment { Start, Center, End };
enum class FillRule { Alternate, Winding };
enum class ArcSweep { Clockwise, CounterClockwise };
enum class ArcSize { Small, Large };
enum class LineCap { Butt, Round, Square };
enum class LineJoin { Miter, Round, Bevel };
enum class DashStyle { Solid, Dash, Dot, DashDot };

struct StrokeStyle {
    double width = 1;
    LineCap cap = LineCap::Butt;
    LineJoin join = LineJoin::Miter;
    DashStyle dash = DashStyle::Solid;
    double miterLimit = 10;
    double dashOffset = 0;
};

// Borrowed, premultiplied RGBA8 output. Dimensions and stride are physical
// pixels/bytes; drawing coordinates use logical pixels at device_pixel_ratio.
struct FrameBuffer {
    uint32_t width = 0, height = 0, stride = 0;
    uint64_t capacity = 0;
    uint8_t* rgba = nullptr;
    double device_pixel_ratio = 1;
};

enum class TextAlign { Start, Center, End };
enum class VerticalAlign { Top, Center, Bottom };
enum class TextDirection { Auto, LeftToRight, RightToLeft };
enum class FontWeight { Normal = 400, Medium = 500, SemiBold = 600, Bold = 700 };

struct TextMetrics {
    double width = 0, height = 0, widthIncludingTrailingWhitespace = 0;
    uint32_t lineCount = 0;
};

struct TextLineMetrics {
    uint32_t offset = 0, length = 0;  // UTF-16 offsets, including an authored newline.
    double height = 0;
};

struct TextStyle {
    double size = 14;
    std::wstring family = L"Segoe UI";
    std::wstring locale = L"en-US";
    TextAlign align = TextAlign::Start;
    VerticalAlign vertical = VerticalAlign::Center;
    bool bold = false;
    bool wrap = false;
    bool ellipsis = false;
    FontWeight weight = FontWeight::Normal;
    TextDirection direction = TextDirection::Auto;
    double lineHeight = 0, baseline = 0;
    bool tabularFigures = false;
    double characterSpacing = 0;
};
}  // namespace cybercanvas
