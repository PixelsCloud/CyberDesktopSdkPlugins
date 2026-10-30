void inputStats(const cdsdk::Settings& settings, const CdInputStats& stats, float w, float h) {
    const bool english = settings.at("language").text == "en";
    const auto foreground = uint32_t(settings.at("foreground").integer), muted = (foreground & 0xFFFFFF00) | 0x99;
    const float radius = float(settings.at("background_corner_radius").integer);
    if (settings.at("show_background").integer)
        card(w, h, radius, uint32_t(settings.at("background").integer));
    const float padding = std::clamp(w * .045f, 8.f, 16.f), split = (w - 2) * .5f;
    ink((foreground & 0xFFFFFF00) | 0x18);
    target->DrawLine(D2D1::Point2F(split, h * .20f), D2D1::Point2F(split, h * .79f), brush.Get(), 1);

    const auto number = [](uint64_t count) {
        auto value = std::to_wstring(count);
        for (int pos = int(value.size()) - 3; pos > 0; pos -= 3)
            value.insert(size_t(pos), 1, L',');
        return value;
    };
    const uint64_t today[]{stats.keyboard_today, stats.mouse_today}, total[]{stats.keyboard_total, stats.mouse_total};
    const wchar_t* titles[]{english ? L"KEYBOARD" : L"键盘", english ? L"MOUSE" : L"鼠标"};
    for (int i = 0; i < 2; ++i) {
        const float left = (i ? split : 0) + padding, right = (i ? w - 2 : split) - padding;
        const auto color = uint32_t(settings.at(i ? "mouse_color" : "keyboard_color").integer);
        const float icon = std::clamp(h * .14f, 9.f, 17.f), y = h * .17f;
        controls.icons.paint(canvas, {left, y - icon * .5, icon, icon}, i ? "mouse" : "keyboard", color);
        cybercanvas::MetricItem metric;
        metric.caption = cdsdk::label(*fonts, titles[i], std::clamp(h * .125f, 9.f, 13.f),
                                      D2D1::RectF(left + icon + 5, h * .06f, right, h * .28f), foreground, false, true);
        const bool active = (stats.status & CD_INPUT_STATS_ACTIVE) != 0;
        metric.number = cdsdk::label(*fonts, active ? number(today[i]) : L"—", std::clamp(h * .32f, 18.f, 40.f),
                                     D2D1::RectF(left, h * .30f, right, h * .65f), color, false, true);
        const auto caption = active ? (english ? L"TODAY" : L"今日次数") : (english ? L"NOT RECORDING" : L"统计未运行");
        metric.detail = cdsdk::label(*fonts, caption, std::clamp(h * .095f, 8.f, 11.f),
                                     D2D1::RectF(left, h * .66f, right, h * .79f), muted, false, true);
        metric.draw(canvas);
        if (settings.at("show_total").integer)
            text((english ? L"TOTAL  " : L"累计  ") + number(total[i]) +
                     ((stats.status & CD_INPUT_STATS_STORAGE_ERROR) ? (english ? L" · UNSAVED" : L" · 未保存") : L""),
                 std::clamp(h * .105f, 8.f, 12.f), D2D1::RectF(left, h * .82f, right, h * .98f), muted, false);
    }
}
