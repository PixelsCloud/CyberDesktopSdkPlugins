// Included in Renderer::Impl. The same object transform places input regions.
void objectInstrument(Spec spec, const cdsdk::Settings& settings, const Data& d, double w, double h) {
    using namespace mechanicalart;
    const bool timer = spec.kind == productivity::Kind::Timer;
    const auto kind = timer ? mechanicalart::Kind::Timer : mechanicalart::Kind::Disk;
    const auto stage = arrange(kind, w, h);
    stage.draw(canvas, [&] {
        artwork.body(canvas, kind, stage);
        if (timer) {
            const double minutes = double(settings.at(d.resting ? "break_minutes" : "focus_minutes").integer);
            const double step = minutes <= 6 ? 1 : minutes <= 12 ? 2 : std::ceil(minutes / 30) * 5;
            const double maximum = step * 6;
            mechanicalart::scale(canvas, {492, 497}, 289, maximum, true, true);
            needle(canvas, {492, 497}, 195, -90 + 360 * std::clamp(d.remaining / (maximum * 60), 0., 1.));
            if (d.finished) {
                canvas.oval(492, 757, 8, 8, 0xb55c48ff);
            }
        } else {
            const double used = d.total ? double(d.total - std::min(d.free, d.total)) / d.total : 0;
            mechanicalart::scale(canvas, {462, 461}, 292, 100);
            const double fraction = d.needles[0] >= 0 ? d.needles[0] : used;
            needle(canvas, {462, 461}, 245, 140 + 260 * std::clamp(fraction, 0., 1.), d.valid);
            const auto drive = settings.at("drive").text;
            mechanicalart::text(canvas, drive.empty() ? L"C" : wide(drive.substr(0, 1)), {399, 638, 126, 74}, 59);
            mechanicalart::text(canvas, d.valid ? capacity(d.available) : L"—", {303, 846, 390, 74}, 67,
                                d.stale ? 0xb7b8a8ff : kIvory);
        }
    });
    if (timer) {
        for (unsigned i = 0; i < 2; ++i) {
            const auto b = timerButton(i);
            const auto visual = controls.visual(i + 1);
            const auto mapped = stage.map(b);
            if (visual.pressed || visual.hovered) {
                canvas.oval(mapped.x + mapped.width / 2, mapped.y + mapped.height / 2, mapped.width * .45,
                            mapped.height * .45, visual.pressed ? 0x18222470 : 0xe9ce9440);
            }
            artwork.icon(canvas, stage, {b.x + 41, b.y + 41, 76, 76},
                         i           ? Icon::Reset
                         : d.running ? Icon::Pause
                                     : Icon::Play,
                         0x172024ff);
        }
    } else {
        // Calibrated to the visible numeral ink, rather than its line-box center.
        artwork.icon(canvas, stage, {234, 848, 72, 72}, Icon::Disk);
    }
}
