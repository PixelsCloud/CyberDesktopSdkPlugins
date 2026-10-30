#pragma once

#include "plugins/mechanical-instruments/Artwork.h"
#include "plugins/widget-suite/InputCounter.h"

namespace widgetplugin {
void drawMechanicalInputCounter(cybercanvas::Canvas& canvas, mechanicalart::Artwork& artwork, const CdInputStats& stats,
                                const CounterDrums* drums, double width, double height, uint64_t now);
}  // namespace widgetplugin
