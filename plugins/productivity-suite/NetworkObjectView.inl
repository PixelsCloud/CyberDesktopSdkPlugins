// Included in Renderer::Impl. Coordinates match the approved PNG backplate.
void networkObject(const cdsdk::Settings& settings, const Data& data, double width, double height) {
    using namespace mechanicalart;
    const auto plate = networkPlate();
    const auto stage = arrangePlate(plate, width, height);
    stage.draw(canvas, [&] {
        const bool material = plateImage(canvas, artwork, plate, stage);
        for (unsigned i = 0; i < 2; ++i) {
            const uint32_t accent = i ? 0xdabb65ff : 0x78b981ff;
            const auto face = plate.faces[i];
            if (!material) {
                canvas.oval(face.center.x, face.center.y, face.radius * 1.12, face.radius * 1.12, 0x88938dff);
                canvas.oval(face.center.x, face.center.y, face.radius, face.radius, 0x151e22ff);
            }
            // Both channels use the same logarithmic byte-rate scale.
            const uint64_t rate = i ? data.up : data.down;
            const double fraction =
                data.needles[i] >= 0 ? data.needles[i] : std::log1p(double(rate) / 1024) / std::log1p(1048576.);
            instrumentScale(canvas, face, false, accent, L"1G");
            needle(canvas, face.center, face.radius * .73, 140 + 260 * std::clamp(fraction, 0., 1.), data.valid);

            const double x = i ? 1045 : 528;
            const auto formatted = data.valid ? capacity(rate, true) : L"—";
            const auto split = formatted.find_last_of(L' ');
            const auto number = split == std::wstring::npos ? formatted : formatted.substr(0, split);
            const auto unit = split == std::wstring::npos ? L"" : formatted.substr(split + 1);
            canvas.saved([&] {
                canvas.translate(x + 37, 342);
                if (i) {
                    canvas.rotate(180);
                }
                auto arrow = canvas.path();
                arrow.moveTo({-10, -28})
                    .lineTo({10, -28})
                    .lineTo({10, 0})
                    .lineTo({25, 0})
                    .lineTo({0, 29})
                    .lineTo({-25, 0})
                    .lineTo({-10, 0})
                    .close();
                canvas.fill(arrow, accent);
            });
            mechanicalart::text(canvas, number, {x + 81, 292, 200, 95}, 69);
            mechanicalart::text(canvas, unit, {x + 280, 313, 132, 64}, 42);

            if (settings.at("history").integer) {
                std::vector<double> samples;
                double maximum = 1024;
                for (const auto& sample : data.history) {
                    const double value = double(i ? sample.second : sample.first);
                    samples.push_back(value);
                    maximum = std::max(maximum, value);
                }
                historyCharts[i].samples(std::move(samples));
                historyCharts[i].range(std::pair{0., maximum});
                auto theme = cybercanvas::WidgetTheme::dark();
                theme.accent = accent;
                theme.track = 0x00000000;
                const cybercanvas::Rect graph{i ? 1038. : 476., 454, i ? 451. : 470., 75};
                canvas.clipped({graph.x, graph.y, graph.width, graph.height}, [&] {
                    historyCharts[i].draw(canvas, graph, theme, 6);
                });
            }
        }
    });
}
