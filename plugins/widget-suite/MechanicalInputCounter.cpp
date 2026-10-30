#include "plugins/widget-suite/MechanicalInputCounter.h"

namespace widgetplugin {
void drawMechanicalInputCounter(cybercanvas::Canvas& canvas, mechanicalart::Artwork& artwork, const CdInputStats& stats,
                                const CounterDrums* drums, double width, double height, uint64_t now) {
    // One uniform transform preserves the round badges, screws and curved drums.
    const cybercanvas::Rect visible{7, 172, 2116, 403};
    const auto view = cybercanvas::sceneViewport({width * .055, height * .08, width * .89, height * .84},
                                                 {visible.width, visible.height});
    const mechanicalart::Stage stage{view.scale,
                                     {view.origin.x - visible.x * view.scale, view.origin.y - visible.y * view.scale}};
    const auto body = artwork.material(canvas, "body", 2137 * stage.scale);
    const auto keyboard = counterDigits(stats.keyboard_today), mouse = counterDigits(stats.mouse_today);
    CounterDrums::Positions positions{};
    for (size_t i = 0; i < 5; ++i) {
        positions[i] = keyboard[i];
        positions[i + 5] = mouse[i];
    }
    if (drums && drums->initialized()) {
        positions = drums->sample(now);
    }
    stage.draw(canvas, [&] {
        canvas.clipped(visible, [&] {
            if (body) {
                canvas.image(*body, {0, 0, 2137, 736}, {.fit = cybercanvas::ImageFit::Stretch, .clip = false});
            } else {
                for (double x : {7., 1090.}) {
                    canvas.gradientBox({x, 172, 1033, 376}, 174, 0xbbc0bdff, 0x5c6261ff);
                    canvas.box(x + 28, 208, 978, 296, 140, 0x151b1fff);
                }
            }
            constexpr std::array<double, 10> centers{390, 513, 636, 759, 882, 1464, 1588, 1711, 1835, 1958};
            cybercanvas::TextStyle style;
            style.family = L"Bahnschrift";
            style.size = 156;
            style.weight = cybercanvas::FontWeight::SemiBold;
            style.align = cybercanvas::TextAlign::Center;
            style.vertical = cybercanvas::VerticalAlign::Center;
            style.tabularFigures = true;
            const bool valid = (stats.status & CD_INPUT_STATS_ACTIVE) != 0;
            for (size_t i = 0; i < centers.size(); ++i) {
                const cybercanvas::Rect face{centers[i] - 51, 250, 102, 214};
                canvas.clipped(face, [&] {
                    if (!valid) {
                        canvas.text(L"–", style, face, mechanicalart::kIvory);
                        return;
                    }
                    const auto digit = uint64_t(std::floor(positions[i]));
                    const double shift = (positions[i] - digit) * face.height;
                    canvas.text(std::to_wstring(digit % 10), style, {face.x, face.y - shift, face.width, face.height},
                                mechanicalart::kIvory);
                    if (shift > .001) {
                        canvas.text(std::to_wstring((digit + 1) % 10), style,
                                    {face.x, face.y + face.height - shift, face.width, face.height},
                                    mechanicalart::kIvory);
                    }
                });
            }
        });
    });
}
}  // namespace widgetplugin
