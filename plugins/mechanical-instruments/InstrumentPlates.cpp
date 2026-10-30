#include "plugins/mechanical-instruments/InstrumentPlates.h"

namespace mechanicalart {
InstrumentPlate monitorPlate(int rows, int columns) {
    if (rows == 2) {
        return {{1280, 1229},
                {33, 19, 1212, 1185},
                {DialFace{{352, 342}, 232}, DialFace{{922, 342}, 232}, DialFace{{352, 877}, 232},
                 DialFace{{922, 877}, 232}},
                4};
    }
    if (columns == 4) {
        return {{2172, 724},
                {63, 82, 2045, 514},
                {DialFace{{329, 343}, 191}, DialFace{{836, 343}, 191}, DialFace{{1342, 343}, 191},
                 DialFace{{1848, 343}, 191}},
                4};
    }
    if (columns == 2) {
        return {{1774, 887}, {50, 40, 1674, 797}, {DialFace{{466, 446}, 305}, DialFace{{1305, 446}, 305}, {}, {}}, 2};
    }
    return {{1226, 1283}, {123, 59, 976, 1127}, {DialFace{{612, 695}, 385}, {}, {}, {}}, 1};
}

InstrumentPlate networkPlate() {
    return {{1983, 793}, {38, 141, 1904, 464}, {DialFace{{263, 374}, 170}, DialFace{{1714, 374}, 170}, {}, {}}, 2};
}

Stage arrangePlate(const InstrumentPlate& plate, double width, double height) {
    const double margin = std::min(width, height) * .045;
    const auto view = cybercanvas::sceneViewport(
        {margin, margin, std::max(1., width - 2 * margin), std::max(1., height - 2 * margin)},
        {plate.bounds.width, plate.bounds.height});
    return {view.scale, {view.origin.x - plate.bounds.x * view.scale, view.origin.y - plate.bounds.y * view.scale}};
}

bool plateImage(Canvas& canvas, Artwork& artwork, const InstrumentPlate& plate, const Stage& stage) {
    const auto image = artwork.material(canvas, "body", plate.size.width * stage.scale);
    if (!image) {
        return false;
    }
    canvas.image(*image, {0, 0, plate.size.width, plate.size.height},
                 {.fit = cybercanvas::ImageFit::Stretch, .clip = false});
    return true;
}

void instrumentScale(Canvas& canvas, DialFace face, bool zones, uint32_t accent, const wchar_t* maximum) {
    for (unsigned i = 0; i <= 50; ++i) {
        const double fraction = double(i) / 50;
        const bool major = i % 10 == 0;
        const double angle = 140 + 260 * fraction;
        const auto a = polar(face.center, face.radius * .94, angle);
        const auto b = polar(face.center, face.radius * (major ? .82 : .88), angle);
        canvas.line(a.x, a.y, b.x, b.y, zones ? monitorZone(fraction) : accent, face.radius * (major ? .016 : .009));
    }
    for (unsigned i : {0u, 1u}) {
        const Point p{face.center.x + face.radius * (i ? .78 : -.78), face.center.y + face.radius * .2};
        text(canvas, i ? maximum : L"0",
             {p.x - face.radius * .22, p.y - face.radius * .13, face.radius * .44, face.radius * .26},
             face.radius * .16);
    }
}

void temperatureMark(Canvas& canvas, Rect bounds) {
    canvas.saved([&] {
        canvas.translate(bounds.x + bounds.width / 2, bounds.y + bounds.height / 2);
        const double scale = bounds.height / 30;
        canvas.scale(scale, scale);
        auto outline = canvas.path();
        outline.moveTo({-4, 3})
            .lineTo({-4, -10})
            .cubicTo({-4, -16}, {4, -16}, {4, -10})
            .lineTo({4, 3})
            .cubicTo({13, 13}, {-13, 13}, {-4, 3})
            .close();
        canvas.stroke(outline, kIvory, {.width = 2});
        canvas.line(0, -9, 0, 5, kIvory, 2);
        canvas.oval(0, 7, 2, 2, kIvory);
    });
}

void instrumentReadout(Canvas& canvas, DialFace face, const std::wstring& label, const std::wstring& value,
                       double fraction, bool valid, bool temperature) {
    const auto [center, radius] = face;
    needle(canvas, center, radius * .74, 140 + 260 * std::clamp(fraction, 0., 1.), valid);
    if (temperature) {
        temperatureMark(canvas, {center.x - radius * .13, center.y - radius * .62, radius * .26, radius * .32});
    } else {
        text(canvas, label, {center.x - radius * .6, center.y - radius * .64, radius * 1.2, radius * .36},
             radius * .26);
    }
    text(canvas, value, {center.x - radius * .76, center.y + radius * .24, radius * 1.52, radius * .52}, radius * .4);
}
}  // namespace mechanicalart
