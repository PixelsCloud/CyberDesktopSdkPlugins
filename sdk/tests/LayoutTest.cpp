#include <CyberCanvas/Components.h>
#include <CyberCanvas/PanelDrawing.h>
#include <CyberCanvas/PanelLayout.h>
#include <sdk/Canvas.h>

#include <iostream>

int main() {
    try {
        for (double width : {80., 96., 112., 128., 160.}) {
            const auto panel = cybercanvas::PanelLayout::make({3, 7, width * 4 - 6, width * 2.5 - 6}, 4);
            const auto viewport = cybercanvas::sceneViewport(panel.content(), {840, 360});
            for (const cybercanvas::Point point : {cybercanvas::Point{0, 0}, {412, 153}, {840, 360}}) {
                const auto roundtrip = viewport.toScene(viewport.toPanel(point));
                if (std::hypot(point.x - roundtrip.x, point.y - roundtrip.y) > 1e-8) {
                    return 1;
                }
            }
            if (viewport.bounds.x < panel.bounds.x || viewport.bounds.y < panel.bounds.y ||
                viewport.bounds.x + viewport.bounds.width > panel.bounds.x + panel.bounds.width ||
                viewport.bounds.y + viewport.bounds.height > panel.bounds.y + panel.bounds.height) {
                return 1;
            }
        }
        cdsdk::Canvas canvas;
        const auto theme = cybercanvas::WidgetTheme::dark();
        const auto flow = cybercanvas::wrappingRow({0, 0, 120, 100}, {{70, 22}, {70, 22}});
        if (flow.items.size() != 2 || flow.items[1].y <= flow.items[0].y) {
            return 1;
        }
        std::vector<uint8_t> pixels(240 * 200 * 4);
        CdFrame frame{sizeof(frame), 240, 200, 240 * 4, pixels.size(), pixels.data(), 2};
        auto painting = canvas.frame(frame, {});
        cybercanvas::LabelledSlider volume{{5, 5, 110, 70}, L"音量 / Volume", {1, {0, 0, 0, 4}}};
        const auto track = volume.layout(canvas, theme).slider.bounds;
        cybercanvas::InputRouter input;
        input.regions({volume.region(canvas, theme)});
        const auto event = input.dispatch(
            {cybercanvas::PointerKind::Press, {track.x + track.width * .5, track.y + track.height * .5}, true});
        const auto next = volume.change(canvas, theme, event);
        if (!next || std::abs(*next - .5) > .01) {
            return 1;
        }
        volume.draw(canvas, theme, *next);
        painting.finish();
        std::vector<uint8_t> source(12 * 12 * 4, 255);
        for (unsigned y = 0; y < 12; ++y) {
            for (unsigned x = 0; x < 12; ++x) {
                const auto offset = (y * 12 + x) * 4;
                source[offset] = y >= 9 && x < 3 ? 255 : 0;
                source[offset + 1] = x >= 9 ? 255 : 0;
                source[offset + 2] = y < 3 && x < 3 ? 255 : 0;
            }
        }
        const auto image = cybercanvas::ImageData::fromPremultipliedBgra(12, 12, std::move(source));
        canvas.render(frame, {}, [&](auto& c, auto) {
            cybercanvas::panelImage(c, *image, {5, 5, 110, 90}, {12, 12}, {3, 3, 3, 3}, 1);
        });
        const auto component = [&](unsigned x, unsigned y, unsigned channel) {
            return pixels[(y * 240 + x) * 4 + channel];
        };
        // The SDK frame is RGBA, while decoded image resources are premultiplied BGRA.
        if (component(12, 12, 0) != 255 || component(227, 12, 1) != 255 || component(12, 187, 2) != 255) {
            return 1;
        }
        for (unsigned y = 11; y < 189; ++y) {
            for (unsigned x = 11; x < 229; ++x) {
                if (component(x, y, 3) != 255) {
                    return 1;
                }
            }
        }
        canvas.render(frame, {}, [&](auto& c, auto) {
            cybercanvas::panelImage(c, *image, {5.25, 5.25, 109.5, 89.5}, {12, 12}, {3, 3, 3, 3}, 1.13);
        });
        for (unsigned y = 12; y < 188; ++y) {
            for (unsigned x = 12; x < 228; ++x) {
                if (component(x, y, 3) != 255) {
                    return 1;
                }
            }
        }
        std::cout << "PASS SDK automatic layout, measured text, composite rendering and pointer geometry\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
