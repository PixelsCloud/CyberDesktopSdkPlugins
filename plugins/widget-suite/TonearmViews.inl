// PNG materials only: geometry below masks and places existing image pixels;
// it does not redraw the grooves, spindle, tube, fittings or headshell.
void materialCircle(cybercanvas::Path& path, double x, double y, double radius) {
    path.moveTo({x + radius, y})
        .arcTo({x - radius, y}, {radius, radius})
        .arcTo({x + radius, y}, {radius, radius})
        .close();
}

void recordMaterial(const Media& media, const CdRect& area) {
    const double radius = std::max(1., std::min(area.width, area.height) * .5);
    // Bleed one physical pixel under the PNG platter rim. Shrinking by a fixed
    // logical margin exposed the desktop through the opening on compact cases.
    const double materialRadius = radius + 1 / canvas.devicePixelRatio();
    const auto vinyl = artwork.material(canvas, "vinyl", materialRadius * 2);
    canvas.saved([&] {
        canvas.translate(area.x + area.width * .5, area.y + area.height * .5);
        canvas.rotate(media.recordDegrees);
        if (vinyl) {
            auto rim = canvas.path();
            materialCircle(rim, 0, 0, materialRadius);
            canvas.masked(rim, [&] {
                // Register the PNG's visible circular edge, excluding transparent padding.
                canvas.image(
                    *vinyl,
                    {-materialRadius - materialRadius * 34 / 1220, -materialRadius - materialRadius * 32 / 1214,
                     materialRadius * 2508 / 1220, materialRadius * 2508 / 1214},
                    {.fit = cybercanvas::ImageFit::Stretch, .clip = false});
            });
        }
        if (media.cover.bitmap) {
            auto cover = canvas.path();
            materialCircle(cover, 0, 0, radius * .5);
            materialCircle(cover, 0, 0, radius * .07);
            canvas.masked(cover, [&] {
                const auto edge = float(radius * .5);
                photoImage(media.cover, D2D1::RectF(-edge, -edge, edge, edge), true, 1);
            });
        }
    });
}

void tonearm(const Media& media, const mechanicalart::Stage& stage) {
    const auto image = artwork.material(canvas, "arm", 1000 * stage.scale);
    if (!image) {
        return;
    }
    const auto pose = tonearmPose(media.tonearmProgress);
    canvas.saved([&] {
        canvas.translate(pose.pivot.x, pose.pivot.y);
        canvas.rotate(pose.rotation);
        canvas.translate(-pose.pivot.x, -pose.pivot.y);
        // Reuse the ORIGINAL PNG, unchanged. Its platter opening is transparent:
        // the inner circle selects the original cartridge/tube without the rim.
        auto assembly = canvas.path(cybercanvas::FillRule::Winding);
        materialCircle(assembly, 279, 300, 190);
        materialCircle(assembly, pose.pivot.x, pose.pivot.y, 34);
        assembly.moveTo({521, 145})
            .lineTo({541, 157})
            .lineTo({531, 186})
            .lineTo({507, 244})
            .lineTo({486, 295})
            .lineTo({466, 287})
            .lineTo({488, 230})
            .lineTo({505, 172})
            .lineTo({507, 154})
            .close();
        canvas.masked(assembly, [&] {
            canvas.image(*image, {0, 0, 1000, 1000. * image->height() / image->width()},
                         {.fit = cybercanvas::ImageFit::Stretch, .clip = false});
        });
    });
}
