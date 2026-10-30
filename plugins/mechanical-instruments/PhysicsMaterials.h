#pragma once

#include <CyberCanvas/PanelLayout.h>
#include <sdk/Canvas.h>
#include <sdk/Images.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

namespace mechanicalart::physicsmaterials {
using cybercanvas::Canvas;
using cybercanvas::Rect;

Rect sceneBounds(double width, double height);

struct Registration {
    cybercanvas::Size size;
    Rect visible;
};

// Coordinates refer to the original PNG, independently of its decoded DPI.
Registration registration(std::string_view slot);

void registeredImage(Canvas& canvas, const cybercanvas::ImageData& image, Registration source, Rect bounds);

bool sprite(Canvas& canvas, const cdsdk::Images& images, const char* slot, Rect bounds);

void anchored(Canvas& canvas, const cybercanvas::ImageData& image, cybercanvas::Size source, cybercanvas::Point pivot,
              double scale);

bool plate(Canvas& canvas, const cdsdk::Images& images, bool clock, bool compact, Rect bounds);

void requestPlate(cdsdk::Images& images, const std::filesystem::path& root, bool compact, double physicalWidth);
}  // namespace mechanicalart::physicsmaterials
