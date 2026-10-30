#pragma once

#include <string>
#include <string_view>

#include "CyberCanvas/Types.h"

namespace cybercanvas {
class Canvas;
}

namespace cybercanvas::mechanical {
inline constexpr uint32_t kInk = 0xF1E8D0FF, kMuted = 0xA9ACA3FF, kBrass = 0xDCBA72FF;
inline constexpr uint32_t kFace = 0x171D20FF, kGreen = 0x79B896FF, kRed = 0xCD6658FF;

// Native instrument parts share the caller's Canvas, font and actual DPI. No
// bitmaps, global resources, timers or SDK dependency are retained here.
class Instruments {
public:
    using TextFilter = std::wstring (*)(std::wstring_view);

    explicit Instruments(Canvas& canvas, TextStyle style = {}, TextFilter filter = nullptr);

    void text(const std::wstring& value, Rect area, double size, uint32_t color = kInk,
              TextAlign align = TextAlign::Center);

    void metal(Rect r, double radius, bool brass = false, bool pressed = false);

    void screw(Point p, double radius = 2.8);

    Rect chassis(double width, double height, const std::wstring& title = {});

    void window(Rect r);

    void lamp(Point p, bool on, uint32_t color = kGreen);

    // Angles run clockwise, starting at the lower left. Values and the moving
    // needle are separate so an eased needle never delays the exact readout.
    void gauge(Rect bounds, double fraction, const std::wstring& label, const std::wstring& value, bool valid = true,
               double maximum = 100, bool ivory = false);

    void button(Rect r, bool pressed = false, bool enabled = true);

    void rail(Rect r, double fraction);

private:
    Canvas& c_;
    TextStyle font_;
    TextFilter filter_ = nullptr;
};
}  // namespace cybercanvas::mechanical
