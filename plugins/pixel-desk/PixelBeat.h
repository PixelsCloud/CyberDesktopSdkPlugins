#pragma once

#include <CyberCanvas/PanelLayout.h>

#include "plugins/beat-machine/Machine.h"
#include "plugins/pixel-desk/PixelLayout.h"

namespace pixeldesk {
struct BeatSpec {
    const char* id;
    const char* folder;
    unsigned columns;
    bool compact;
    cybercanvas::Size source;
    cybercanvas::Rect visible;
};

inline constexpr std::array<BeatSpec, 2> kBeatCatalog{{
    {"com.cyberdesktop.pixeldesk.beat-machine.4x2", "beat-machine-4x2", 4, true, {847, 421}, {2, 2, 843, 418}},
    {"com.cyberdesktop.pixeldesk.beat-machine.6x2", "beat-machine-6x2", 6, false, {1394, 440}, {2, 1, 1390, 437}},
}};

cybercanvas::Rect beatCase(bool compact);

cybercanvas::SceneViewport beatView(CdRect safe, bool compact);
}  // namespace pixeldesk
