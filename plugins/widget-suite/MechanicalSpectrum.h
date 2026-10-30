#pragma once

#include "plugins/mechanical-instruments/Artwork.h"
#include "plugins/mechanical-instruments/PhysicsMaterials.h"
#include "plugins/widget-suite/Widget.h"

namespace widgetplugin {
void spectrumRod(cybercanvas::Canvas& canvas, const cybercanvas::ImageData* image, double x, double top, double width,
                 double length);

void spectrumPeak(cybercanvas::Canvas& canvas, double x, double y, double width);

double mechanicalBand(const Spectrum& spectrum, unsigned index, unsigned bands, bool peaks = false);

void drawMechanicalSpectrum(cybercanvas::Canvas& canvas, mechanicalart::Artwork& artwork, Spec spec,
                            const Spectrum& spectrum, bool peaks, double width, double height);
}  // namespace widgetplugin
