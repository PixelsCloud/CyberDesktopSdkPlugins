void objectPlayer(Spec spec, const cdsdk::Settings& settings, const Media& media, int pressed, double w, double h) {
    using namespace mechanicalart;
    const auto stage = playerArtworkStage(spec, w, h);
    const auto layout = arrangePlayer(spec, float(w), float(h));
    recordMaterial(media, layout.cover);
    const bool background = settings.at("show_background").integer != 0;
    if (background) {
        stage.draw(canvas, [&] {
            artwork.body(canvas, mechanicalart::Kind::Player, stage);
        });
    }
    const auto armStage = playerTonearmStage(layout);
    armStage.draw(canvas, [&] {
        tonearm(media, armStage);
    });
    const auto box = [](CdRect r) {
        return cybercanvas::Rect{r.x, r.y, r.width, r.height};
    };
    const auto title = playerTitleInk(
        canvas, media.title.empty() ? (settings.at("language").text == "en" ? L"No music" : L"暂无播放") : media.title,
        layout);
    ink(kIvory);
    canvas.clipped(layout.title, [&] {
        target->DrawTextLayout(D2D1::Point2F(float(title.origin.x), float(title.origin.y)), title.layout.Get(),
                               brush.Get());
    });
    constexpr uint32_t flags[]{CD_MEDIA_CAN_PREVIOUS, CD_MEDIA_CAN_TOGGLE, CD_MEDIA_CAN_NEXT};
    for (unsigned i = 0; i < 3; ++i) {
        const auto r = box(layout.buttons[i]);
        // BUSY is a transient transport lock, not a capability change. Rendering it
        // as disabled made every button flash whenever any command was submitted.
        const bool enabled = !media.source.empty() && (media.controls & flags[i]);
        const auto visual = controls.visual(i + 1);
        if (!background) {
            cybercanvas::mechanical::Instruments instruments(canvas);
            instruments.button(r, visual.pressed, enabled);
        }
        if (visual.pressed || pressed == int(i + 1) || !enabled) {
            canvas.oval(r.x + r.width / 2, r.y + r.height / 2, r.width * .46, r.height * .46,
                        enabled ? 0x13202b65 : 0x13202b40);
        } else if (visual.hovered) {
            canvas.oval(r.x + r.width / 2, r.y + r.height / 2, r.width * .45, r.height * .45, kBrass, .8);
        }
        controls.icons.paint(canvas, {r.x + r.width * .26, r.y + r.height * .26, r.width * .48, r.height * .48},
                             i == 0                             ? "skip-back"
                             : i == 2                           ? "skip-forward"
                             : media.status == CD_MEDIA_PLAYING ? "pause"
                                                                : "play",
                             enabled ? 0x172024ff : 0x737c78ff);
    }
    if (settings.at("show_progress").integer) {
        const auto r = layout.timeline;
        const double position = mediaPosition(media, GetTickCount64());
        const double fraction = media.duration > 0 ? std::clamp(position / media.duration, 0., 1.) : 0;
        const double x = r.x + r.width * fraction;
        canvas.line(r.x, r.y + r.height / 2, x, r.y + r.height / 2, kBrass, std::max(1., r.height * .28));
        const double radius = spec.columns == 4 ? std::max(1., r.height * .42) : std::max(2., r.height * .7);
        canvas.oval(x, r.y + r.height / 2, radius, radius, kBrass);
        const auto time = [](double seconds) {
            const auto whole = uint64_t(std::max(0., seconds));
            return std::to_wstring(whole / 60) + L":" + (whole % 60 < 10 ? L"0" : L"") + std::to_wstring(whole % 60);
        };
        if (layout.elapsed.width > 0) {
            mechanicalart::text(canvas, time(position), box(layout.elapsed), layout.detailSize, kIvory, false);
            mechanicalart::text(canvas, time(media.duration), box(layout.duration), layout.detailSize, kIvory, false);
        }
    }
}
