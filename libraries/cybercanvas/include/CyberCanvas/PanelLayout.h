#pragma once

#include "CyberCanvas/Layout.h"

namespace cybercanvas {

enum class PanelDensity { Compact, Standard, Detailed };

// A coordinate adapter around existing layout primitives. It owns no widgets,
// textures or simulation state. The caller keeps housing and scene independent.
struct SceneViewport {
    Rect bounds;
    Point origin;
    double scale = 1.0;

    Point toScene(Point panel) const;

    Point toPanel(Point scene) const;
};

SceneViewport sceneViewport(Rect available, Size scene);

struct PanelLayout {
    Rect bounds;
    double baseWidth = 96.0;
    PanelDensity density = PanelDensity::Standard;

    static PanelLayout make(Rect area, unsigned columns);

    bool detailed() const;

    // Host gaps are already excluded from bounds. This is an optional internal
    // design padding, independent of the grid and spanning-cell boundaries.
    Rect content(double padding = 6.0) const;
};

}  // namespace cybercanvas
