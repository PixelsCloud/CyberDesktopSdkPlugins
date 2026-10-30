#pragma once

#include <array>
#include <cmath>

#include "plugins/air-quality-suite/AirQuality.h"
#include "plugins/mechanical-instruments/Artwork.h"
#include "plugins/mechanical-instruments/HangingParts.h"

namespace airplugin {
struct MechanicalPlate {
    mechanicalart::physicsmaterials::Registration source;
    std::array<cybercanvas::Point, 3> centers;
    std::array<double, 3> radii;
};

MechanicalPlate mechanicalPlate(Spec spec);

double mechanicalAqiAngle(double value);

void mechanicalAirScale(cybercanvas::Canvas& canvas, cybercanvas::Point center, double radius, bool colored);

void mechanicalAirNeedle(cybercanvas::Canvas& canvas, cybercanvas::Point center, double radius, double angle,
                         bool valid);

void drawAirPlate(cybercanvas::Canvas& canvas, const cybercanvas::ImageData& image, Spec spec, cybercanvas::Size size);

void drawMechanicalAir(cybercanvas::Canvas& canvas, mechanicalart::Artwork& artwork, Spec spec, const Snapshot& data,
                       const std::wstring& city, double width, double height, bool english = false);
}  // namespace airplugin
