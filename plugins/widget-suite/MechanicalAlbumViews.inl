void mechanicalAlbum(const cdsdk::Settings& settings, const Photo& photo, const Photo& previous, float fade,
                     AlbumPlayback playback, double width, double height) {
    const auto layout = arrangeAlbum(width, height);
    const auto r = layout.photo;
    const auto area = D2D1::RectF(float(r.x), float(r.y), float(r.x + r.width), float(r.y + r.height));
    ComPtr<ID2D1RoundedRectangleGeometry> mask;
    const float radius = float(44 * layout.stage.scale);
    require(factory->CreateRoundedRectangleGeometry(D2D1::RoundedRect(area, radius, radius), mask.GetAddressOf()));
    canvas.masked(mask.Get(), [&] {
        canvas.box(r.x, r.y, r.width, r.height, radius, 0x111719ff);
        const bool cover = settings.at("fit").text == "cover";
        const bool motion =
            playback.motionEnabled && settings.at("photo_motion").integer && !settings.at("reduced_motion").integer;
        const auto imageArea = [&](double seconds, size_t index) {
            const auto bounds = motion ? albumPhotoRect(r, seconds, cover, index) : r;
            return D2D1::RectF(float(bounds.x), float(bounds.y), float(bounds.x + bounds.width),
                               float(bounds.y + bounds.height));
        };
        if (previous.bitmap && fade < 1) {
            photoImage(previous, imageArea(playback.previousMotionSeconds, previous.index), cover, 1);
        }
        if (photo.bitmap) {
            photoImage(photo, imageArea(playback.motionSeconds, photo.index), cover, previous.bitmap ? fade : 1);
        } else {
            controls.icons.paint(canvas, {r.x + r.width * .44, r.y + r.height * .25, r.width * .12, r.height * .24},
                                 "image", 0xada99c90);
            text(
                settings.at("language").text == "en" ? L"Choose a photo folder in settings" : L"请在设置中选择图片目录",
                float(std::clamp(26 * layout.stage.scale, 9., 15.)),
                D2D1::RectF(float(r.x + 8), float(r.y + r.height * .56), float(r.x + r.width - 8),
                            float(r.y + r.height * .77)),
                0xe6dfcfff);
        }
    });

    if (const auto body = artwork.material(canvas, "body", width)) {
        layout.stage.draw(canvas, [&] {
            // The matte follows the authored outer silhouette, excluding the tiny
            // isolated alpha/color fringes around the generated PNG cutout.
            ComPtr<ID2D1RoundedRectangleGeometry> casing;
            require(factory->CreateRoundedRectangleGeometry(D2D1::RoundedRect(D2D1::RectF(28, 41, 972, 580), 58, 58),
                                                            casing.GetAddressOf()));
            canvas.masked(casing.Get(), [&] {
                canvas.image(*body, {0, 0, 1000, 992000. / 1586},
                             {.fit = cybercanvas::ImageFit::Stretch, .clip = false});
            });
        });
    }
    // Preload the small atlas so the first hover never exposes blank controls.
    const auto materials = artwork.material(canvas, "controls", width * .7);
    if ((!albumHovered && !controls.hasFocus()) || !photo.bitmap) {
        return;
    }
    if (materials) {
        albumSprite(canvas, *materials, {73, 279, 1160, 249}, layout.dock);
        if (settings.at("counter").integer) {
            albumSprite(canvas, *materials, {1341, 321, 554, 200}, layout.counter);
        }
    }
    for (unsigned i = 0; i < 3; ++i) {
        auto button = layout.buttons[i];
        const bool enabled = i == 1 || (photo.count > 1 && !playback.pending);
        const auto visual = controls.visual(i + 1);
        if (visual.pressed || !enabled) {
            canvas.oval(button.x + button.width / 2, button.y + button.height / 2, button.width * .43,
                        button.height * .43, enabled ? 0x17151065 : 0x17151080);
        } else if (visual.hovered) {
            canvas.oval(button.x + button.width / 2, button.y + button.height / 2, button.width * .46,
                        button.height * .46, 0xf4d49845);
        }
        const double inset = button.width * .28, shift = visual.pressed ? .65 : 0;
        controls.icons.paint(
            canvas, {button.x + inset, button.y + inset + shift, button.width - inset * 2, button.height - inset * 2},
            i == 0            ? "chevron-left"
            : i == 2          ? "chevron-right"
            : playback.paused ? "play"
                              : "pause",
            enabled ? 0x25221bff : 0x656158ff);
    }
    if (settings.at("counter").integer) {
        const auto number = [](size_t value) {
            return (value < 10 ? L"0" : L"") + std::to_wstring(value);
        };
        mechanicalart::text(canvas, number(photo.index + 1) + L" / " + number(photo.count), layout.digits,
                            std::clamp(23 * layout.stage.scale, 7., 13.), mechanicalart::kIvory, false);
    }
}
