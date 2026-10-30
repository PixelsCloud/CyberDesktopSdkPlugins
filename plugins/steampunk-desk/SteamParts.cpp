#include "plugins/steampunk-desk/SteamParts.h"

namespace steamdesk {
void pixelDisc(cybercanvas::Canvas& canvas, double x, double y, double radius, uint32_t color) {
    canvas.oval(x, y, radius, radius, color);
}

void stepped(cybercanvas::Canvas& canvas, cybercanvas::Rect area, double radius, uint32_t color) {
    canvas.box(area.x, area.y, area.width, area.height, radius, color);
}

void pixelBolt(cybercanvas::Canvas& canvas, double x, double y, double radius) {
    canvas.oval(x, y, radius, radius, 0x100e0bff);
    canvas.oval(x, y - radius * .06, radius * .86, radius * .86, 0x745030ff);
    canvas.oval(x - radius * .1, y - radius * .17, radius * .63, radius * .63, 0xd7b478ff);
    canvas.box(x - radius * .48, y - radius * .11, radius * .96, radius * .22, radius * .08, 0x33281aff);
}
}  // namespace steamdesk
