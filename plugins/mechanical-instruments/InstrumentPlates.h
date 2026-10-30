#pragma once

#include <array>

#include "plugins/mechanical-instruments/Artwork.h"

namespace mechanicalart {
struct DialFace {
    Point center;
    double radius;
};

struct InstrumentPlate {
    cybercanvas::Size size;
    Rect bounds;
    std::array<DialFace, 4> faces;
    unsigned count;
};

InstrumentPlate monitorPlate(int rows, int columns);

InstrumentPlate networkPlate();

Stage arrangePlate(const InstrumentPlate& plate, double width, double height);

bool plateImage(Canvas& canvas, Artwork& artwork, const InstrumentPlate& plate, const Stage& stage);

inline constexpr uint32_t monitorZone(double fraction) {
    return fraction < .6 ? 0x78b981ff : fraction < .8 ? 0xdabb65ff : 0xd86b5eff;
}

void instrumentScale(Canvas& canvas, DialFace face, bool zones = true, uint32_t accent = kIvory,
                     const wchar_t* maximum = L"100");

void temperatureMark(Canvas& canvas, Rect bounds);

void instrumentReadout(Canvas& canvas, DialFace face, const std::wstring& label, const std::wstring& value,
                       double fraction, bool valid, bool temperature = false);
}  // namespace mechanicalart
