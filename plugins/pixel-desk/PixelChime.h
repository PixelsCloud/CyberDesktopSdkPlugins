#pragma once

#include <CyberCanvas/PanelLayout.h>

#include "plugins/physics-toys/Toys.h"
#include "plugins/pixel-desk/PixelLayout.h"

namespace pixeldesk {
cybercanvas::SceneViewport chimeView(CdRect safe, double height = 410);

cybercanvas::Point chimeDragTarget(cybercanvas::Point target, unsigned index = 0, double height = 410);

cybercanvas::Size chimePieceSize(unsigned index, double height);

class ChimeInstance : public toys::Instance {
public:
    explicit ChimeInstance(const CdCreateContext& context);

    ~ChimeInstance();
};
}  // namespace pixeldesk
