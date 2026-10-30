#pragma once

#include "CyberCanvas/Types.h"

namespace cybercanvas {
class Canvas;
class ImageData;
struct Insets;
}  // namespace cybercanvas

namespace cybercanvas {

// Stretch only the empty center of an authored housing. Corners and edge
// thickness share one scale, so screws and machining details retain their shape.
void panelImage(Canvas& canvas, const ImageData& image, Rect area, Size design, Insets edges, double scale);

}  // namespace cybercanvas
