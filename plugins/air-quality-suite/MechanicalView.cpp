#include "plugins/air-quality-suite/MechanicalView.h"

namespace airplugin {
MechanicalPlate mechanicalPlate(Spec spec) {
    if (spec.rows == 2) {
        return {mechanicalart::hangingPart("air-2x4"), {{{495, 530}, {1198, 290}, {1235, 708}}}, {331, 192, 188}};
    }
    if (spec.columns == 4) {
        return {mechanicalart::hangingPart("air-1x4"), {{{430, 374}, {1085, 406}, {1677, 405}}}, {282, 255, 255}};
    }
    return {mechanicalart::hangingPart("air-1x2"), {{{457, 489}, {}, {}}}, {287, 0, 0}};
}

double mechanicalAqiAngle(double value) {
    constexpr std::array<double, 7> values{0, 50, 100, 150, 200, 300, 500};
    constexpr std::array<double, 7> angles{140, 190, 235, 280, 325, 365, 400};
    value = std::clamp(value, 0., 500.);
    for (size_t i = 1; i < values.size(); ++i) {
        if (value <= values[i]) {
            return angles[i - 1] + (angles[i] - angles[i - 1]) * (value - values[i - 1]) / (values[i] - values[i - 1]);
        }
    }
    return 400;
}

void mechanicalAirScale(cybercanvas::Canvas& canvas, cybercanvas::Point center, double radius, bool colored) {
    constexpr std::array<double, 7> angles{140, 190, 235, 280, 325, 365, 400};
    constexpr std::array<uint32_t, 6> colors{0x93cf72ff, 0xe5cc64ff, 0xde9b50ff, 0xd36d62ff, 0xb879a4ff, 0x99607bff};
    for (unsigned i = 0; i <= 60; ++i) {
        const double angle = 140 + 260. * i / 60;
        size_t zone = 0;
        while (zone + 1 < colors.size() && angle > angles[zone + 1]) {
            ++zone;
        }
        const bool major = i % 10 == 0;
        const auto a = mechanicalart::polar(center, radius * .97, angle);
        const auto b = mechanicalart::polar(center, radius * (major ? .84 : .91), angle);
        canvas.line(a.x, a.y, b.x, b.y, colored ? colors[zone] : mechanicalart::kIvory,
                    radius * (major ? .016 : .0105));
    }
    if (colored) {
        for (unsigned value : {0u, 100u, 300u, 500u}) {
            const auto point = mechanicalart::polar(center, radius * .74, mechanicalAqiAngle(value));
            mechanicalart::text(canvas, std::to_wstring(value),
                                {point.x - radius * .19, point.y - radius * .1, radius * .38, radius * .2},
                                radius * .135, mechanicalart::kIvory, false);
        }
    }
}

void mechanicalAirNeedle(cybercanvas::Canvas& canvas, cybercanvas::Point center, double radius, double angle,
                         bool valid) {
    canvas.saved([&] {
        canvas.translate(center.x, center.y);
        canvas.rotate(angle + 90);
        const double length = radius * .78, half = radius * .022;
        auto hand = canvas.path();
        hand.moveTo({0, -length})
            .lineTo({-half, -length * .58})
            .lineTo({-half, radius * .06})
            .lineTo({half, radius * .06})
            .lineTo({half, -length * .58})
            .close();
        canvas.saved([&] {
            canvas.translate(radius * .007, radius * .012);
            canvas.fill(hand, 0x03060780);
        });
        canvas.masked(hand, [&] {
            canvas.gradientBox({-half, -length, half * 2, length + radius * .07}, 0, valid ? 0xffe7abff : 0xb4b7adff,
                               valid ? 0xa87a36ff : 0x626c70ff);
        });
        canvas.line(0, -length * .9, -half * .3, 0, 0xffedc1a0, radius * .004);
    });
    canvas.oval(center.x, center.y, radius * .05, radius * .05, 0x6c512eff);
    canvas.oval(center.x - radius * .003, center.y - radius * .005, radius * .043, radius * .043, 0xd6b273ff);
    canvas.oval(center.x, center.y, radius * .024, radius * .024, 0x6b7070ff);
    canvas.oval(center.x, center.y - radius * .003, radius * .013, radius * .013, 0xe4dfcbff);
}

void drawAirPlate(cybercanvas::Canvas& canvas, const cybercanvas::ImageData& image, Spec spec, cybercanvas::Size size) {
    if (spec.rows != 2) {
        canvas.image(image, {0, 0, size.width, size.height}, {.fit = cybercanvas::ImageFit::Stretch, .clip = false});
        return;
    }
    // Native silhouette masking removes the generated environmental glow.
    // The single open center is the approved simplified two-bridge structure.
    auto outline = canvas.path();
    outline.moveTo({80, 530})
        .cubicTo({80, 290}, {252, 130}, {497, 130})
        .cubicTo({661, 130}, {752, 188}, {820, 190})
        .cubicTo({907, 190}, {987, 40}, {1196, 40})
        .cubicTo({1350, 40}, {1446, 154}, {1444, 293})
        .cubicTo({1444, 410}, {1402, 447}, {1390, 489})
        .cubicTo({1440, 531}, {1480, 608}, {1480, 711})
        .cubicTo({1480, 846}, {1388, 957}, {1250, 960})
        .cubicTo({1146, 953}, {1062, 912}, {1016, 876})
        .cubicTo({940, 819}, {890, 807}, {797, 830})
        .cubicTo({700, 881}, {600, 923}, {495, 923})
        .lineTo({303, 923})
        .lineTo({303, 920})
        .lineTo({183, 920})
        .lineTo({183, 865})
        .cubicTo({110, 771}, {80, 664}, {80, 530})
        .close();
    outline.moveTo({829, 262})
        .quadraticTo({882, 252}, {939, 242})
        .cubicTo({931, 347}, {956, 419}, {1041, 480})
        .quadraticTo({1054, 490}, {1038, 508})
        .cubicTo({998, 565}, {976, 663}, {989, 748})
        .quadraticTo({987, 758}, {974, 754})
        .lineTo({874, 727})
        .cubicTo({922, 591}, {920, 420}, {829, 262})
        .close();
    canvas.masked(outline, [&] {
        canvas.image(image, {0, 0, size.width, size.height}, {.fit = cybercanvas::ImageFit::Stretch, .clip = false});
    });
}

void drawMechanicalAir(cybercanvas::Canvas& canvas, mechanicalart::Artwork& artwork, Spec spec, const Snapshot& data,
                       const std::wstring& city, double width, double height, bool english) {
    const auto plate = mechanicalPlate(spec);
    const wchar_t* indexLabel = data.cached ? (english ? L"AQI · Cached" : L"AQI · 缓存") : L"AQI";
    const auto area = mechanicalart::physicsmaterials::sceneBounds(width, height);
    const auto view = cybercanvas::sceneViewport(area, {plate.source.visible.width, plate.source.visible.height});
    const mechanicalart::Stage stage{
        view.scale,
        {view.origin.x - plate.source.visible.x * view.scale, view.origin.y - plate.source.visible.y * view.scale}};
    const auto body = artwork.material(canvas, "body", plate.source.size.width * stage.scale);
    stage.draw(canvas, [&] {
        if (body) {
            drawAirPlate(canvas, *body, spec, plate.source.size);
        } else {
            for (size_t i = 0; i < (spec.columns == 2 ? 1u : 3u); ++i) {
                const auto center = plate.centers[i];
                canvas.oval(center.x, center.y, plate.radii[i] * 1.15, plate.radii[i] * 1.15, 0x7c8586ff);
                canvas.oval(center.x, center.y, plate.radii[i], plate.radii[i], 0x11181cff);
            }
        }
        const unsigned count = spec.columns == 2 ? 1 : 3;
        for (unsigned i = 0; i < count; ++i) {
            const auto center = plate.centers[i];
            const double r = plate.radii[i];
            const double value = i ? data.pollutants[i - 1] : data.aqi;
            const bool valid = data.valid && std::isfinite(value) && value >= 0;
            mechanicalAirScale(canvas, center, r, i == 0);
            mechanicalAirNeedle(canvas, center, r, mechanicalAqiAngle(valid ? value : 0), valid);
            if (spec.columns == 2) {
                continue;
            }
            if (i == 0) {
                if (spec.rows == 1) {
                    mechanicalart::text(canvas, city, {center.x - r * .62, center.y - r * .64, r * 1.24, r * .32},
                                        r * .24, mechanicalart::kIvory, false);
                } else {
                    mechanicalart::text(canvas, city, {center.x - r * .52, center.y - r * .65, r * 1.04, r * .22},
                                        r * .17, mechanicalart::kIvory, false);
                    mechanicalart::text(canvas, indexLabel, {center.x - r * .55, center.y - r * .42, r * 1.1, r * .18},
                                        r * .125, mechanicalart::kIvory, false);
                }
            } else {
                const double labelSize = spec.rows == 1 ? .25 : .19;
                mechanicalart::text(canvas, i == 1 ? L"PM2.5" : L"PM10",
                                    {center.x - r * .62, center.y - r * (spec.rows == 1 ? .66 : .63), r * 1.24,
                                     r * (spec.rows == 1 ? .32 : .27)},
                                    r * labelSize, mechanicalart::kIvory, false);
            }
            const bool compactPollutant = spec.rows == 1 && i > 0;
            mechanicalart::text(canvas, valid ? std::to_wstring(std::llround(value)) : L"--",
                                {center.x - r * .64, center.y + r * (compactPollutant ? .12 : .25), r * 1.28,
                                 r * (compactPollutant ? .48 : .57)},
                                r * .43);
        }
        if (spec.columns == 2) {
            const bool valid = data.valid && std::isfinite(data.aqi) && data.aqi >= 0;
            mechanicalart::text(canvas, city, {939, 398, 468, 110}, 88, mechanicalart::kIvory, false);
            mechanicalart::text(canvas, valid ? std::to_wstring(std::llround(data.aqi)) : L"--", {939, 535, 468, 150},
                                168);
        }
    });
}
}  // namespace airplugin
