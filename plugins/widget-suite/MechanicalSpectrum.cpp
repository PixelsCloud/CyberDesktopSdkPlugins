#include "plugins/widget-suite/MechanicalSpectrum.h"

namespace widgetplugin {
void spectrumRod(cybercanvas::Canvas& canvas, const cybercanvas::ImageData* image, double x, double top, double width,
                 double length) {
    if (image) {
        // Registration uses the original transparent PNG, regardless of decode DPI.
        // A rigid sprite slides through a guide; its texture never stretches with amplitude.
        const double sx = width / 172, sy = length / 1401;
        canvas.image(*image, {x - 444 * sx, top - 42 * sy, 1060 * sx, 1484 * sy},
                     {.fit = cybercanvas::ImageFit::Stretch, .clip = false});
    } else {
        canvas.gradientBox({x, top, width, length}, width * .15, 0xf1d7a0ff, 0xa17b40ff);
    }
}

void spectrumPeak(cybercanvas::Canvas& canvas, double x, double y, double width) {
    canvas.gradientBox({x, y, width, 4.5}, 1.7, 0xfff6dfff, 0xb8aa8aff);
}

double mechanicalBand(const Spectrum& spectrum, unsigned index, unsigned bands, bool peaks) {
    const double first = double(index) * Spectrum::count / bands;
    const double last = double(index + 1) * Spectrum::count / bands - 1;
    // A range maximum keeps a narrow FFT transient visible when reducing bins.
    return std::clamp(double(spectrum.range(first, std::max(first, last), peaks)), 0., 1.);
}

void drawMechanicalSpectrum(cybercanvas::Canvas& canvas, mechanicalart::Artwork& artwork, Spec spec,
                            const Spectrum& spectrum, bool peaks, double width, double height) {
    const bool radial = spec.kind == Kind::Radial;
    const cybercanvas::Rect visible =
        radial ? cybercanvas::Rect{55, 73, 1141, 1126} : cybercanvas::Rect{31, 40, 1959, 545};
    const auto view = cybercanvas::sceneViewport(mechanicalart::physicsmaterials::sceneBounds(width, height),
                                                 {visible.width, visible.height});
    const mechanicalart::Stage stage{view.scale,
                                     {view.origin.x - visible.x * view.scale, view.origin.y - visible.y * view.scale}};
    const auto body = artwork.material(canvas, "body", (radial ? 1254 : 2021) * stage.scale);
    const auto rod = artwork.material(canvas, "rod", 400 * stage.scale);
    const auto ball = !radial && peaks ? artwork.material(canvas, "ball", 40 * stage.scale) : nullptr;
    stage.draw(canvas, [&] {
        canvas.clipped(visible, [&] {
            if (radial) {
                if (body) {
                    canvas.image(*body, {0, 0, 1254, 1254}, {.fit = cybercanvas::ImageFit::Stretch, .clip = false});
                } else {
                    canvas.oval(625, 650, 570, 570, 0xa8aaa7ff);
                    canvas.oval(625, 650, 478, 478, 0x171c20ff);
                    canvas.oval(625, 650, 173, 173, 0x666b6bff);
                }
                constexpr unsigned bands = 40;
                constexpr double inner = 197, travel = 250, rodWidth = 20;
                for (unsigned i = 0; i < bands; ++i) {
                    const double level = mechanicalBand(spectrum, i, bands);
                    const double peak = mechanicalBand(spectrum, i, bands, true);
                    canvas.saved([&] {
                        canvas.translate(625, 650);
                        canvas.rotate(360. * i / bands);
                        canvas.box(-rodWidth * .64, -462, rodWidth * 1.28, 272, 7, 0x101417ff);
                        canvas.line(-rodWidth * .64, -454, -rodWidth * .64, -201, 0x6c747454, 1.6);
                        canvas.clipped({-rodWidth * .7, -452, rodWidth * 1.4, 255}, [&] {
                            if (level > 0) {
                                spectrumRod(canvas, rod.get(), -rodWidth * .5, -inner - travel * level, rodWidth,
                                            travel + 8);
                            }
                        });
                        if (peaks && peak > .01) {
                            spectrumPeak(canvas, -rodWidth * .32, -inner - travel * peak - 9, rodWidth * .64);
                        }
                    });
                }
            } else {
                constexpr unsigned bands = 32;
                constexpr double left = 176, right = 1844, bottom = 438, travel = 354, rodWidth = 35;
                const double pitch = (right - left) / bands;
                for (unsigned i = 0; i < bands; ++i) {
                    const double x = left + i * pitch + (pitch - rodWidth) * .5;
                    const double level = mechanicalBand(spectrum, i, bands);
                    const double peak = mechanicalBand(spectrum, i, bands, true);
                    // Empty travel stays transparent above the hardware, as in the concept.
                    canvas.box(x - 2, bottom - 25, rodWidth + 4, 42, 6, 0x11171bff);
                    canvas.clipped({x - 2, bottom - travel, rodWidth + 4, travel}, [&] {
                        if (level > 0) {
                            spectrumRod(canvas, rod.get(), x, bottom - travel * level, rodWidth, travel + 8);
                        }
                    });
                    if (peaks && peak > .01) {
                        const double diameter = rodWidth * .92;
                        const cybercanvas::Rect bounds{x + (rodWidth - diameter) * .5,
                                                       bottom - travel * peak - diameter - 6, diameter, diameter};
                        if (ball) {
                            mechanicalart::physicsmaterials::registeredImage(
                                canvas, *ball, {{1254, 1254}, {91, 96, 1071, 1053}}, bounds);
                        } else {
                            const double cx = bounds.x + diameter * .5, cy = bounds.y + diameter * .5;
                            canvas.oval(cx, cy, diameter * .5, diameter * .5, 0x434c52ff);
                            canvas.oval(cx - diameter * .12, cy - diameter * .14, diameter * .32, diameter * .32,
                                        0xb6c2c7ff);
                            canvas.oval(cx - diameter * .2, cy - diameter * .23, diameter * .13, diameter * .13,
                                        0xf4f1e5ff);
                        }
                    }
                }
                // The rail covers the rod tails, so every slider enters the same real slot.
                if (body) {
                    canvas.image(*body, {0, 0, 2021, 778}, {.fit = cybercanvas::ImageFit::Stretch, .clip = false});
                } else {
                    canvas.gradientBox({31, bottom, 1959, 120}, 24, 0xb6b7b1ff, 0x606665ff);
                }
            }
        });
    });
}
}  // namespace widgetplugin
