// Included in Canvas::Impl. Rendering and hit testing share arrangePlayer().
void record(const Media& media, const CdRect& area, uint32_t accent, bool detailed = false) {
    const auto center = D2D1::Point2F(float(area.x + area.width * .5), float(area.y + area.height * .5));
    const float radius = std::max(1.f, float(std::min(area.width, area.height)) * .5f - 1.5f);
    recordShadow.ellipse(canvas, {center.x - radius, center.y - radius, radius * 2, radius * 2}, 1.5, {1, 2},
                         0x00000048);
    ink(0x111419FF);
    target->FillEllipse(D2D1::Ellipse(center, radius, radius), brush.Get());
    ink(0x747C8690);
    target->DrawEllipse(D2D1::Ellipse(center, radius - .5f, radius - .5f), brush.Get(), .75f);
    for (int groove = 0; groove < (detailed ? 40 : 10); ++groove) {
        const float r = radius * (detailed ? .53f + groove * .0114f : .67f + groove * .031f);
        ink(groove % 3 == 0 ? 0xA9B8C532 : 0xA9B8C51C);
        target->DrawEllipse(D2D1::Ellipse(center, r, r), brush.Get(), .55f);
    }

    // Reflections stay under the light while the artwork and label rotate.
    ComPtr<ID2D1EllipseGeometry> discMask;
    require(factory->CreateEllipseGeometry(D2D1::Ellipse(center, radius - 1, radius - 1), discMask.GetAddressOf()));
    canvas.masked(discMask.Get(), [&] {
        for (const float direction : {-1.f, 1.f}) {
            auto sheen = canvas.path();
            sheen.moveTo({center.x, center.y})
                .lineTo({center.x + direction * radius * .15f, center.y - direction * radius * 1.1f})
                .lineTo({center.x + direction * radius * .72f, center.y - direction * radius * .9f})
                .close();
            canvas.fill(sheen, 0xDAE5F016);
        }
    });

    const float label = radius * (detailed ? .50f : .62f);
    ComPtr<ID2D1EllipseGeometry> labelMask;
    require(factory->CreateEllipseGeometry(D2D1::Ellipse(center, label, label), labelMask.GetAddressOf()));
    canvas.masked(labelMask.Get(), [&] {
        if (!recordLabelNode) {
            cybercanvas::View group;
            group.kind = cybercanvas::ViewKind::Decoration;
            group.layout.grow = 1;
            group.paintLocal = [this](cybercanvas::Canvas&, cybercanvas::Rect bounds, const cybercanvas::WidgetTheme&) {
                const auto& media = *recordLabelMedia;
                const auto center = D2D1::Point2F(float(bounds.width * .5), float(bounds.height * .5));
                const float label = float(bounds.width * .5);
                const auto accent = recordLabelAccent;
                ink(accent);
                target->FillEllipse(D2D1::Ellipse(center, label, label), brush.Get());
                if (media.cover.bitmap) {
                    photoImage(media.cover,
                               D2D1::RectF(center.x - label, center.y - label, center.x + label, center.y + label),
                               true, 1);
                } else {
                    ink(0xFFFFFF72);
                    for (int line = 0; line < 3; ++line) {
                        const float half = label * (line == 0 ? .42f : .28f),
                                    y = center.y - label * (.55f - line * .14f);
                        target->DrawLine(D2D1::Point2F(center.x - half, y), D2D1::Point2F(center.x + half, y),
                                         brush.Get(), std::max(.65f, label * .045f));
                    }
                    ink(0x17232B44);
                    target->FillEllipse(
                        D2D1::Ellipse(D2D1::Point2F(center.x, center.y + label * .65f), label * .46f, label * .15f),
                        brush.Get());
                }
            };
            recordLabelNode = recordLabelScene.add(0, std::move(group));
        }
        recordLabelMedia = &media;
        recordLabelAccent = accent;
        recordLabelScene.arrange(canvas, {center.x - label, center.y - label, label * 2, label * 2}, dpr);
        recordLabelScene.transform(recordLabelNode, {.pivot = {label, label}, .rotation = media.recordDegrees});
        recordLabelScene.draw(canvas);
        recordLabelMedia = nullptr;
    });
    ink(0x00000055);
    target->DrawEllipse(D2D1::Ellipse(center, label, label), brush.Get(), .8f);
    ink(0xD0D5DCB0);
    target->FillEllipse(D2D1::Ellipse(center, radius * .065f, radius * .065f), brush.Get());
    ink(0x10151CFF);
    target->FillEllipse(D2D1::Ellipse(center, radius * .035f, radius * .035f), brush.Get());
}

void player(const Spec& spec, const cdsdk::Settings& settings, const Media& media, int pressed, float w, float h) {
    if (spec.mechanical) {
        objectPlayer(spec, settings, media, pressed, w, h);
        return;
    }
    const bool english = settings.at("language").text == "en";
    const bool mechanical = spec.mechanical;
    using namespace cybercanvas::mechanical;
    Instruments instrument(canvas, cdsdk::textStyle(*fonts, 10), cdsdk::displayText);
    const auto accent = mechanical ? kBrass : uint32_t(settings.at("accent").integer);
    const auto foreground = mechanical ? kInk : uint32_t(settings.at("foreground").integer);
    const auto muted = mechanical ? kMuted : (foreground & 0xFFFFFF00) | 0xA0;
    const float radius = float(settings.at("background_corner_radius").integer);
    if (settings.at("show_background").integer) {
        if (mechanical) {
            instrument.chassis(w, h);
        } else {
            card(w, h, radius, uint32_t(settings.at("background").integer));
        }
    }
    const auto layout = arrangePlayer(spec, w, h);
    const auto rect = [](const CdRect& r) {
        return D2D1::RectF(float(r.x), float(r.y), float(r.x + r.width), float(r.y + r.height));
    };
    const auto label = [&](const std::wstring& value, float size, D2D1_RECT_F area, uint32_t color,
                           bool center = true) {
        if (mechanical) {
            instrument.text(value, {area.left, area.top, area.right - area.left, area.bottom - area.top}, size, color,
                            center ? cybercanvas::TextAlign::Center : cybercanvas::TextAlign::Start);
        } else {
            text(value, size, area, color, center);
        }
    };
    record(media, layout.cover, accent);
    if (mechanical) {
        const auto& r = layout.cover;
        const double cx = r.x + r.width * .5, cy = r.y + r.height * .5, radius = std::min(r.width, r.height) * .5;
        canvas.oval(cx, cy, radius + .5, radius + .5, 0xD2D4C5FF, .7);
        canvas.oval(cx, cy, radius * .25, radius * .25, 0x8E713BFF);
        canvas.oval(cx - .3, cy - .3, radius * .22, radius * .22, kBrass);
        canvas.oval(cx, cy, std::max(1., radius * .04), std::max(1., radius * .04), 0x242925FF);
        const cybercanvas::Point anchor{r.x + r.width * .90, r.y + r.height * .13};
        const bool engaged = media.status == CD_MEDIA_PLAYING;
        const cybercanvas::Point tip{r.x + r.width * (engaged ? .67 : .94), r.y + r.height * .78};
        canvas.line(anchor.x + 1, anchor.y + 2, tip.x + 1, tip.y + 2, 0x000000CC, std::max(2., radius * .075));
        canvas.line(anchor.x, anchor.y, tip.x, tip.y, 0x89938FFF, std::max(1.5, radius * .06));
        canvas.line(anchor.x - .4, anchor.y, tip.x - .4, tip.y, 0xE9E7CDFF, std::max(.6, radius * .018));
        canvas.oval(anchor.x, anchor.y, std::max(2., radius * .12), std::max(2., radius * .12), 0xBEC4B8FF);
        instrument.screw(anchor, std::max(1., radius * .055));
        instrument.metal({tip.x - radius * .07, tip.y - radius * .05, radius * .14, radius * .20}, 1, true);
    }

    const bool available = !media.source.empty();
    const bool centered = spec.columns == 2;
    if (mechanical && !centered) {
        instrument.window({layout.title.x - 2, layout.title.y, layout.title.width + 3,
                           layout.artist.y + layout.artist.height - layout.title.y + 1});
    }
    label(media.title.empty() ? (english ? L"No music playing" : L"暂无播放歌曲") : media.title, layout.titleSize,
          rect(layout.title), foreground, centered);
    label(media.artist.empty() ? (english ? L"Open a music player" : L"请打开音乐播放器") : media.artist,
          layout.detailSize, rect(layout.artist), muted, centered);
    if (layout.album.width > 0)
        label(media.album, layout.detailSize, rect(layout.album), muted, false);
    if (layout.source.width > 0)
        label(media.commandResult < 0 && media.commandResult != CD_ERROR_BUSY
                  ? (english ? L"Command unavailable" : L"操作未成功")
                  : playerName(media.source, english),
              layout.detailSize * .85f, rect(layout.source), muted, false);

    constexpr uint32_t flags[]{CD_MEDIA_CAN_PREVIOUS, CD_MEDIA_CAN_TOGGLE, CD_MEDIA_CAN_NEXT};
    for (int i = 0; i < 3; ++i) {
        const auto& r = layout.buttons[i];
        auto theme = cybercanvas::WidgetTheme::dark();
        theme.foreground = foreground;
        theme.surface = 0;
        theme.accent = accent;
        theme.onAccent = 0x14252EFF;
        cybercanvas::IconButton button(uint32_t(i + 1), {r.x, r.y, r.width, r.height},
                                       i == 0                             ? "skip-back"
                                       : i == 2                           ? "skip-forward"
                                       : media.status == CD_MEDIA_PLAYING ? "pause"
                                                                          : "play",
                                       i == 0                             ? L"Previous"
                                       : i == 2                           ? L"Next"
                                       : media.status == CD_MEDIA_PLAYING ? L"Pause"
                                                                          : L"Play",
                                       i == 1 ? cybercanvas::ButtonStyle::Filled : cybercanvas::ButtonStyle::Quiet);
        button.round = i == 1;
        button.enabled = available && (media.controls & flags[i]) && media.commandResult != CD_ERROR_BUSY;
        button.iconSize = r.width * .42;
        auto visual = controls.visual(button.id);
        if (pressed == i + 1) {
            visual.pressed = true;
            visual.feedback = 1;
        }
        if (mechanical) {
            instrument.button({r.x, r.y, r.width, r.height}, visual.pressed, button.enabled);
            const bool painted =
                controls.icons.paint(canvas, {r.x + r.width * .28, r.y + r.height * .28, r.width * .44, r.height * .44},
                                     i == 0                             ? "skip-back"
                                     : i == 2                           ? "skip-forward"
                                     : media.status == CD_MEDIA_PLAYING ? "pause"
                                                                        : "play",
                                     button.enabled ? 0x152024FF : 0x68706BFF);
            if (!painted) {
                instrument.text(i == 0                             ? (english ? L"Prev" : L"上一首")
                                : i == 2                           ? (english ? L"Next" : L"下一首")
                                : media.status == CD_MEDIA_PLAYING ? (english ? L"Pause" : L"暂停")
                                                                   : (english ? L"Play" : L"播放"),
                                {r.x + 2, r.y, r.width - 4, r.height}, std::clamp(r.width * .20, 5., 9.),
                                button.enabled ? 0x152024FF : 0x68706BFF);
            }
            if (visual.hovered && button.enabled) {
                canvas.oval(r.x + r.width * .5, r.y + r.height * .5, r.width * .5 - 1, r.height * .5 - 1, kBrass, .9);
            }
        } else {
            button.draw(canvas, theme, controls.icons.painter(), visual);
        }
    }

    if (settings.at("show_progress").integer) {
        const auto r = rect(layout.timeline);
        const double position = mediaPosition(media, GetTickCount64());
        auto theme = cybercanvas::WidgetTheme::dark();
        theme.track = (foreground & 0xFFFFFF00) | 0x25;
        theme.accent = accent;
        theme.radius = 1.5;
        if (mechanical) {
            instrument.rail({layout.timeline.x, layout.timeline.y, layout.timeline.width, layout.timeline.height},
                            media.duration > 0 ? position / media.duration : 0);
        } else {
            cybercanvas::ProgressBar{cdsdk::rect(r)}.draw(canvas, theme,
                                                          media.duration > 0 ? position / media.duration : 0);
        }
        const auto time = [](double seconds) {
            const auto total = uint64_t(std::max(0.0, seconds));
            return std::to_wstring(total / 60) + L":" + (total % 60 < 10 ? L"0" : L"") + std::to_wstring(total % 60);
        };
        if (layout.elapsed.width > 0) {
            label(time(position), layout.detailSize * .85f, rect(layout.elapsed), muted, false);
            label(time(media.duration), layout.detailSize * .85f, rect(layout.duration), muted);
        }
    }
}
