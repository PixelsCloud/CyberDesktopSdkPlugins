#pragma once

#include <sdk/Controls.h>
#include <sdk/Language.h>

#include "plugins/lunar-suite/Lunar.h"
#include "plugins/mechanical-instruments/Artwork.h"

namespace lunarplugin {
struct MechanicalLunarLayout {
    mechanicalart::Stage stage;
    cybercanvas::Size source;
    cybercanvas::Rect visible, previous, next, header, weeks, dates, footer;
};

MechanicalLunarLayout mechanicalLunarLayout(Spec spec, double width, double height);

void lunarText(cybercanvas::Canvas& canvas, const std::wstring& value, cybercanvas::Rect bounds, double size,
               uint32_t ink = mechanicalart::kIvory, bool numeric = false);

std::wstring mechanicalLunarDate(const Lunar& value, bool english);

std::wstring mechanicalWeekday(Date value, bool english);

void lunarRollerText(cybercanvas::Canvas& canvas, const std::wstring& current, const std::wstring& previous,
                     cybercanvas::Rect bounds, double size, View view);

void lunarTwoDigits(cybercanvas::Canvas& canvas, int number, int previous, View view, double first, double second,
                    double y, double width, double height, double size);

void drawMechanicalLunar(cybercanvas::Canvas& canvas, mechanicalart::Artwork& artwork, cdsdk::Controls& controls,
                         Spec spec, const cdsdk::Settings& settings, View view, double width, double height);
}  // namespace lunarplugin
