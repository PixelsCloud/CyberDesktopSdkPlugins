// Included inside Renderer::Impl. All coordinates are logical pixels.
void box(D2D1_RECT_F rect, float radius, uint32_t color) {
    ink(color);
    target->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush.Get());
}

void line(float x, float y, float x2, float y2, uint32_t color, float stroke = 1) {
    ink(color);
    target->DrawLine(D2D1::Point2F(x, y), D2D1::Point2F(x2, y2), brush.Get(), stroke);
}

void ring(float cx, float cy, float radius, float fraction, uint32_t accent) {
    auto theme = cybercanvas::WidgetTheme::dark();
    theme.track = 0xFFFFFF1C;
    theme.accent = accent;
    progressRing.draw(canvas, {cx - radius - 2.5, cy - radius - 2.5, radius * 2 + 5, radius * 2 + 5}, fraction, theme);
    constexpr double tau = 6.283185307179586;
    for (int i = 0; i < 12; ++i) {
        const double a = tau * i / 12 - tau / 4;
        line(cx + (radius + 8) * float(std::cos(a)), cy + (radius + 8) * float(std::sin(a)),
             cx + (radius + 10) * float(std::cos(a)), cy + (radius + 10) * float(std::sin(a)), 0xB1C7CD60, .8f);
    }
}

std::wstring capacity(uint64_t bytes, bool rate = false) {
    const wchar_t* units[]{rate ? L"B/s" : L"B", rate ? L"KB/s" : L"KB", rate ? L"MB/s" : L"MB", rate ? L"GB/s" : L"GB",
                           L"TB"};
    double amount = double(bytes);
    int i = 0;
    while (amount >= 1024 && i < 4) {
        amount /= 1024;
        ++i;
    }
    wchar_t result[64]{};
    swprintf_s(result, L"%.*f %s", amount < 10 && i ? 2 : amount < 100 && i ? 1 : 0, amount, units[i]);
    return result;
}

void shadowText(const std::wstring& value, float size, D2D1_RECT_F rect, uint32_t color, bool center = true) {
    auto shadow = rect;
    shadow.top += 1;
    shadow.bottom += 1;
    text(value, size, shadow, 0x00000020, center);
    text(value, size, rect, color, center);
}

void nav(Spec spec, const Data& data, const cdsdk::Settings& settings, float w, float h, uint32_t muted) {
    for (const auto& hit : hitRegions(spec, data, settings, {0, 0, w, h}))
        if (hit.action == 3 || hit.action == 4) {
            const auto& r = hit.rect;
            auto theme = cybercanvas::WidgetTheme::dark();
            theme.foreground = muted;
            theme.surface = 0;
            cybercanvas::IconButton button(uint32_t(hit.action), {r.x, r.y, r.width, r.height},
                                           hit.action == 3 ? "chevron-left" : "chevron-right");
            button.iconSize = 12;
            button.draw(canvas, theme, controls.icons.painter(), controls.visual(button.id));
        }
}

void draw(Spec spec, const cdsdk::Settings& settings, const Data& d, float w, float h) {
    if (spec.mechanical && (spec.kind == Kind::Timer || spec.kind == Kind::Network || spec.kind == Kind::Disk)) {
        mechanical(spec, settings, d, w, h);
        return;
    }
    const bool en = settings.at("language").text == "en", large = spec.rows > 1;
    const float header = std::clamp(h * .13f, 10.f, 13.f);
    const bool dark = spec.kind == Kind::Timer || spec.kind == Kind::Network || spec.kind == Kind::Disk;
    const uint32_t inkColor = dark ? 0xEFF9F8FF : 0x263039FF, muted = dark ? 0xA0B6BFFF : 0x77766FFF;
    constexpr uint32_t bg[]{0x203A37FF, 0xEEF3F9FF, 0xEFE8DDFF, 0x162735FF, 0x273441FF, 0xFFF0BBFF, 0xF4F0E8FF};
    constexpr uint32_t accents[]{0x83E2B6FF, 0x4785CFFF, 0xCB704EFF, 0x66DBE6FF, 0x8DBEF0FF, 0xAD7834FF, 0xDE8640FF};
    const auto accent = accents[int(spec.kind)];
    card(w, h, float(settings.at("background_corner_radius").integer), bg[int(spec.kind)]);
    const auto rect = [](float x, float y, float right, float bottom) {
        return D2D1::RectF(x, y, right, bottom);
    };
    if (spec.kind == Kind::Timer) {
        const int total = int(settings.at(d.resting ? "break_minutes" : "focus_minutes").integer * 60);
        const auto time = std::to_wstring(d.remaining / 60) + L":" + (d.remaining % 60 < 10 ? L"0" : L"") +
                          std::to_wstring(d.remaining % 60);
        const auto title = d.finished                                ? (en ? L"Completed" : L"计时完成")
                           : d.resting                               ? (en ? L"Break" : L"休息时间")
                           : settings.at("mode").text == "countdown" ? (en ? L"Countdown" : L"倒计时")
                                                                     : (en ? L"Focus" : L"专注时间");
        if (large) {
            const float cy = h * .44f, r = std::min(w * .32f, h * .29f);
            ring(w * .5f - 2, cy, r, float(d.remaining) / std::max(1, total), accent);
            shadowText(time, std::min(w * .18f, r * .63f), rect(10, cy - r * .42f, w - 15, cy + r * .24f), inkColor);
            text(title, header, rect(10, cy + r * .24f, w - 15, cy + r * .65f), muted);
        } else {
            text(title, header, rect(12, 6, w - 17, 27), accent, false);
            shadowText(time, std::min(32.f, (w - 20) * .21f), rect(10, 26, w - 15, h - 34), inkColor);
        }
        const auto hits = hitRegions(spec, d, settings, {0, 0, w, h});
        for (const auto& hit : hits) {
            const auto& r = hit.rect;
            const auto label = hit.action == 2 ? (en ? L"Reset" : L"重置")
                               : d.running     ? (en ? L"Pause" : L"暂停")
                                               : (en ? L"Start" : L"开始");
            auto theme = cybercanvas::WidgetTheme::dark();
            theme.font = cdsdk::textStyle(*fonts, std::min(11.f, h * .13f));
            theme.radius = 8;
            theme.surface = 0xFFFFFF18;
            theme.foreground = inkColor;
            theme.accent = theme.surface;
            theme.onAccent = inkColor;
            cybercanvas::Button button(uint32_t(hit.action), {r.x, r.y, r.width, r.height}, cdsdk::displayText(label),
                                       cybercanvas::ButtonStyle::Filled);
            button.draw(canvas, theme, controls.visual(button.id));
        }
        return;
    }
    if (spec.kind == Kind::Countdown) {
        text(wide(settings.at("title").text), header, rect(12, 6, w - 16, 27), muted, false);
        const auto value = std::to_wstring(std::abs(d.days));
        const float cx = spec.columns > 2 ? w * .34f : w * .49f,
                    tileWidth = std::min(w * .7f, spec.columns > 2 ? 110.f : 95.f);
        const auto tile = rect(cx - tileWidth / 2, 31, cx + tileWidth / 2, h - 15);
        box(rect(tile.left + 1, tile.top + 2, tile.right + 1, tile.bottom + 2), 5, 0x00000012);
        box(tile, 5, 0xFAF8F3FF);
        shadowText(value, std::min(39.f, (tile.bottom - tile.top) * .80f), tile, inkColor);
        line(tile.left, tile.top + (tile.bottom - tile.top) * .52f, tile.right,
             tile.top + (tile.bottom - tile.top) * .52f, 0xDDD7CC99, .7f);
        if (spec.columns > 2) {
            text(d.days < 0    ? (en ? L"days ago" : L"天前")
                 : d.days == 0 ? (en ? L"Today" : L"就是今天")
                               : (en ? L"days left" : L"天后"),
                 16, rect(w * .62f, 31, w - 14, 59), accent, false);
            text(wide(settings.at("date").text), 11, rect(w * .62f, 59, w - 14, h - 15), muted, false);
        } else
            text(d.days < 0 ? (en ? L"ago" : L"天前") : (en ? L"days" : L"天"), 10, rect(w - 49, 6, w - 15, 27),
                 accent);
        return;
    }
    if (spec.kind == Kind::Network) {
        text(en ? L"LIVE NETWORK" : L"实时网速", header, rect(12, 6, w - 15, 27), muted, false);
        const bool horizontal = spec.columns >= 4;
        const float size = std::clamp((w - 28) * (horizontal ? .055f : .112f), 12.f, 20.f);
        for (int i = 0; i < 2; ++i) {
            const float x = 12 + (horizontal ? w * .5f : 0) * i, top = 30 + (horizontal ? 0.f : 25.f) * i;
            const auto color = i ? 0xB2A3EFFF : accent;
            controls.icons.paint(canvas, {x, top + 6, 15, 14}, i ? "arrow-up" : "arrow-down", color);
            cybercanvas::MetricItem metric;
            metric.number =
                cdsdk::label(*fonts, capacity(i ? d.up : d.down, true), size,
                             rect(x + 18, top, x + (horizontal ? w * .46f : w - 26), top + 26), inkColor, false);
            metric.number.shadow = 0x00000020;
            metric.number.shadowOffset = {0, 1};
            metric.state = d.valid ? cybercanvas::ContentState::Ready : cybercanvas::ContentState::Empty;
            metric.draw(canvas);
        }
        if (horizontal && settings.at("history").integer) {
            uint64_t max = 1024;
            for (const auto& sample : d.history)
                max = std::max({max, sample.first, sample.second});
            const float left = 12, right = w - 17, upper = 62, bottom = h - 14, plot = std::max(1.f, bottom - upper);
            line(left, bottom, right, bottom, 0xA0B6BF20, .6f);
            for (int channel = 0; channel < 2; ++channel) {
                std::vector<double> samples(60 - std::min<size_t>(60, d.history.size()), NAN);
                for (const auto& sample : d.history) {
                    samples.push_back(double(channel ? sample.second : sample.first));
                }
                historyCharts[channel].samples(std::move(samples));
                historyCharts[channel].range(std::pair{0., double(max)});
                auto theme = cybercanvas::WidgetTheme::dark();
                theme.accent = channel ? 0xB2A3EFFF : accent;
                historyCharts[channel].draw(canvas, {left, upper, right - left, plot}, theme, 1.2);
            }
        }
        return;
    }
    if (spec.kind == Kind::Disk) {
        text(wide(settings.at("drive").text.substr(0, 2)) + (en ? L"  STORAGE" : L"  本地磁盘"), header,
             rect(12, 6, w - 15, 27), muted, false);
        if (!d.valid) {
            text(d.loading ? (en ? L"Reading…" : L"读取中…") : (en ? L"Drive unavailable" : L"磁盘不可用"), 13,
                 rect(10, 30, w - 16, h - 15), muted);
            return;
        }
        const float used = d.total ? float(double(d.total - std::min(d.free, d.total)) / d.total) : 0;
        const auto remaining = capacity(d.available);
        const auto usedColor = used > .9f ? 0xF4AE83FF : accent;
        if (large) {
            const float cy = h * .45f, r = std::min(w * .3f, h * .25f);
            ring(w * .5f - 2, cy, r, used, usedColor);
            shadowText(std::to_wstring(int(used * 100)) + L"%", 29, rect(12, cy - r * .48f, w - 17, cy + r * .15f),
                       inkColor);
            text(en ? L"Used" : L"已用", 11, rect(10, cy + r * .13f, w - 15, cy + r * .62f), muted);
            text((en ? L"Free " : L"剩余 ") + remaining, std::clamp(w * .092f, 12.f, 17.f),
                 rect(10, h - 51, w - 15, h - 28), inkColor);
            text((en ? L"Total " : L"总计 ") + capacity(d.total), 10, rect(10, h - 28, w - 15, h - 10), muted);
        } else {
            shadowText(remaining, std::clamp(w * .16f, 18.f, 27.f), rect(12, 28, w - 15, 60), inkColor, false);
            text((en ? L"Free · total " : L"剩余 · 总计 ") + capacity(d.total), 10, rect(12, 58, w - 15, h - 24), muted,
                 false);
            auto theme = cybercanvas::WidgetTheme::dark();
            theme.track = 0xFFFFFF22;
            theme.accent = usedColor;
            cybercanvas::ProgressBar{{12, h - 19, w - 29, 4}}.draw(canvas, theme, used);
        }
        if (d.stale)
            text(en ? L"STALE" : L"旧数据", 8, rect(w - 64, 7, w - 15, 23), 0xF4AE83FF);
        return;
    }
    if (spec.kind == Kind::Notes) {
        box(rect(w * .35f, 0, w * .62f, 8), 2, 0xD1B97B80);
        text(en ? L"QUICK NOTE" : L"随手记", header, rect(12, 8, w - 17, 29), accent, false);
    }
    if (spec.kind == Kind::Todo) {
        text(en ? L"TODAY'S TASKS" : L"今日待办", header, rect(12, 6, w * .7f, 29), inkColor, false);
        const auto done = std::count_if(d.tasks.begin(), d.tasks.end(), [](const Task& t) {
            return t.done;
        });
        text(std::to_wstring(done) + L" / " + std::to_wstring(d.tasks.size()), 11, rect(w * .7f, 6, w - 17, 29),
             accent);
    }
    if (spec.kind == Kind::Rss) {
        box(rect(12, 10, 38, 28), 4, accent);
        text(L"RSS", 9, rect(12, 10, 38, 28), 0xFFFFFFFF);
        text(d.source.empty() ? (en ? L"Headlines" : L"资讯订阅") : wide(d.source), header, rect(45, 7, w - 17, 30),
             muted, false);
        if (!d.valid || d.articles.empty()) {
            const auto status = d.loading ? (en ? L"Loading headlines…" : L"正在读取订阅…")
                                : !d.valid ? (en ? L"Feed unavailable · check settings" : L"订阅暂不可用，请检查设置")
                                           : (en ? L"No headlines" : L"暂无文章");
            auto theme = cybercanvas::WidgetTheme::light();
            theme.font = cdsdk::textStyle(*fonts, 13, false);
            theme.padding = 0;
            theme.muted = theme.error = muted;
            cybercanvas::ContentNotice{{12, 35, w - 29, h - 55},
                                       {},
                                       cdsdk::displayText(status),
                                       d.loading  ? cybercanvas::ContentState::Loading
                                       : !d.valid ? cybercanvas::ContentState::Error
                                                  : cybercanvas::ContentState::Empty,
                                       false}
                .draw(canvas, theme);
            return;
        }
    }
    const auto noteLines = spec.kind == Kind::Notes ? lines(settings.at("text").text) : std::vector<std::string>{};
    bool any = false;
    for (const auto& hit : hitRegions(spec, d, settings, {0, 0, w, h})) {
        if (hit.action < 100)
            continue;
        any = true;
        const int index = hit.action - 100;
        const auto& a = hit.rect;
        const float y = float(a.y), bottom = float(a.y + a.height), right = float(a.x + a.width);
        if (spec.kind == Kind::Todo) {
            const auto& task = d.tasks[size_t(index)];
            auto theme = cybercanvas::WidgetTheme::light();
            theme.track = 0xDDE6F0FF;
            theme.accent = accent;
            theme.onAccent = 0xFFFFFFFF;
            const cybercanvas::Rect bounds{12, y, w - 29, bottom - y};
            cybercanvas::TextRow row{
                bounds,
                cdsdk::label(*fonts, wide(task.text), std::clamp(h * (large ? .08f : .16f), 13.f, 17.f),
                             rect(36, y + 2, right, bottom - 4), task.done ? muted : inkColor, false),
                {},
                0xDCE3EAFF};
            cybercanvas::TaskRow taskRow{
                row, cybercanvas::Checkbox(uint32_t(hit.action), {13, (y + bottom) * .5 - 7, 14, 14}), task.done};
            taskRow.draw(canvas, theme, controls.icons.painter(), controls.visual(uint32_t(hit.action)));
        } else if (spec.kind == Kind::Notes) {
            cybercanvas::TextRow row{
                {12, y, w - 29, bottom - y},
                cdsdk::label(*fonts, wide(noteLines[size_t(index)]), std::clamp(h * (large ? .08f : .16f), 13.f, 17.f),
                             rect(12, y, right, bottom - 4), inkColor, false),
                {},
                0xE2CE94FF};
            row.draw(canvas, cybercanvas::WidgetTheme::light(), controls.visual(uint32_t(hit.action)));
        } else {
            const auto& article = d.articles[size_t(index)];
            cybercanvas::TextRow row{
                {12, y, w - 29, bottom - y},
                cdsdk::label(*fonts, wide(article.title), std::clamp(h * (large ? .085f : .17f), 13.f, 17.f),
                             rect(12, y, right, bottom - 20), inkColor, false),
                cdsdk::label(
                    *fonts,
                    article.date.empty() ? (en ? L"Open article" : L"点击阅读全文") : wide(article.date.substr(0, 32)),
                    10, rect(12, bottom - 20, right, bottom - 4), muted, false),
                0xDED8CEFF};
            row.draw(canvas, cybercanvas::WidgetTheme::light(), controls.visual(uint32_t(hit.action)));
        }
    }
    if (!any && spec.kind != Kind::Rss)
        text(en ? L"Add content in settings" : L"在设置中填写内容", 13, rect(12, 35, w - 17, h - 24), muted, false);
    const auto footer = spec.kind == Kind::Notes
                            ? (d.copiedUntil ? (en ? L"Copied" : L"已复制") : (en ? L"Click to copy" : L"点击复制"))
                        : spec.kind == Kind::Rss
                            ? (d.stale ? (en ? L"Offline · previous headlines" : L"离线 · 显示上次标题")
                                       : (en ? L"Click to read" : L"点击阅读"))
                            : (en ? L"Click to complete" : L"点击勾选完成");
    cybercanvas::StatusLine{cdsdk::label(*fonts, footer, 9, rect(12, h - 23, w - 67, h - 7),
                                         spec.kind == Kind::Notes ? accent : muted, false)}
        .draw(canvas);
    nav(spec, d, settings, w, h, muted);
}
