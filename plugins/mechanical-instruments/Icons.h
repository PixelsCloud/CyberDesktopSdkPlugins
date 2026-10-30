#pragma once

#include <CyberCanvas/Canvas.h>

#include <algorithm>

namespace mechanicalart {
enum class Icon { Play, Pause, Stop, Previous, Next, Reset, Disk, Humidity, Wind };

// Original solid, engraved marks for the mechanical instruments. Coordinates
// describe visible ink, without the padding of a general-purpose icon set.
cybercanvas::Path iconPath(cybercanvas::Canvas& canvas, Icon icon);

void paintIcon(cybercanvas::Canvas& canvas, cybercanvas::Rect bounds, Icon icon, uint32_t ink = 0x172024ff);
}  // namespace mechanicalart
