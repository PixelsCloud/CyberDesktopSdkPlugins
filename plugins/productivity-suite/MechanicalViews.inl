// Included in Renderer::Impl. Drawing and interaction share hitRegions().
void mechanical(Spec spec, const cdsdk::Settings& settings, const Data& d, double w, double h) {
    if (spec.kind == Kind::Network && spec.rows == 1 && spec.columns == 4) {
        networkObject(settings, d, w, h);
        return;
    }
    if (spec.rows == 2 && spec.columns == 2 && (spec.kind == Kind::Timer || spec.kind == Kind::Disk)) {
        objectInstrument(spec, settings, d, w, h);
        return;
    }
    using namespace cybercanvas::mechanical;
    Instruments m(canvas, cdsdk::textStyle(*fonts, 10), cdsdk::displayText);
    const bool en = settings.at("language").text == "en", large = spec.rows > 1;
    const auto title = spec.kind == Kind::Timer  ? (en ? L"FOCUS TIMER" : L"专注计时")
                       : spec.kind == Kind::Disk ? (en ? L"STORAGE" : L"磁盘空间")
                                                 : (en ? L"NETWORK" : L"实时网速");
    const auto a = m.chassis(w, h, large ? title : L"");
    if (spec.kind == Kind::Network) {
        const bool horizontal = spec.columns >= 4;
        const double row = horizontal ? a.height : (a.height - 4) * .5;
        const double column = horizontal ? (a.width - 12) * .5 : a.width;
        for (int i = 0; i < 2; ++i) {
            const double x = a.x + (horizontal ? i * (column + 12) : 0);
            const double y = a.y + (horizontal ? 0 : i * (row + 4));
            const uint64_t rate = i ? d.up : d.down;
            const bool dial = row >= 28;
            const double diameter = dial ? std::min(row - 2, column * .34) : 0;
            const double fraction =
                d.needles[i] >= 0 ? d.needles[i] : std::log1p(double(rate) / 1024) / std::log1p(1048576.);
            if (dial) {
                m.gauge({x, y, diameter, diameter}, fraction, L"", L"", d.valid, 0);
            }
            const double left = x + (dial ? diameter + 8 : 30);
            const double valueWidth = column - (left - x);
            const double labelHeight = horizontal ? 12 : 10;
            if (dial) {
                m.text(i ? (en ? L"UPLOAD" : L"上传") : (en ? L"DOWNLOAD" : L"下载"),
                       {left, y, valueWidth, labelHeight}, 10, kMuted, cybercanvas::TextAlign::Start);
            } else {
                m.text(i ? (en ? L"UP" : L"上传") : (en ? L"DN" : L"下载"), {x, y, 27, row}, 9, kMuted);
            }
            const double top = y + (dial ? labelHeight + 2 : 0);
            const double valueHeight = std::max(10., std::min(horizontal ? 27. : 21., row - (top - y)));
            const cybercanvas::Rect readout{left, top, valueWidth, valueHeight};
            m.window(readout);
            m.text(d.valid ? capacity(rate, true) : L"—", {left + 3, top, valueWidth - 6, valueHeight},
                   horizontal ? 15 : 12);
            if (horizontal && settings.at("history").integer && row >= 54) {
                std::vector<double> values;
                double maximum = 1024;
                for (const auto& pair : d.history) {
                    const double value = double(i ? pair.second : pair.first);
                    values.push_back(value);
                    maximum = std::max(maximum, value);
                }
                historyCharts[i].samples(std::move(values));
                historyCharts[i].range(std::pair{0., maximum});
                auto theme = cybercanvas::WidgetTheme::dark();
                theme.accent = i ? kBrass : kGreen;
                historyCharts[i].draw(
                    canvas, {left, top + valueHeight + 5, valueWidth, row - labelHeight - valueHeight - 8}, theme, 1);
            }
        }
        return;
    }
    if (spec.kind == Kind::Disk) {
        const double used = d.total ? double(d.total - std::min(d.free, d.total)) / d.total : 0;
        const auto drive = wide(settings.at("drive").text.substr(0, 2));
        const double diameter =
            large ? std::min(a.width, std::max(20., a.height - 40)) : std::min(a.height, a.width * .42);
        const double x = large ? a.x + (a.width - diameter) * .5 : a.x;
        m.gauge({x, large ? a.y : a.y + (a.height - diameter) * .5, diameter, diameter},
                d.needles[0] >= 0 ? d.needles[0] : used, drive,
                d.valid ? std::to_wstring(int(std::lround(used * 100))) + L"%" : L"—", d.valid);
        const double left = large ? a.x : x + diameter + 8;
        double top = large ? a.y + diameter + 5 : a.y + std::max(0., (a.height - (a.height >= 52 ? 49 : 35)) * .5);
        const double width = large ? a.width : a.width - diameter - 8;
        const double readoutHeight = large ? 22 : std::min(22., a.height - 13);
        if (!large) {
            m.text(d.stale ? (en ? L"CACHED" : L"旧数据") : (en ? L"FREE" : L"可用"), {left, top, width, 11}, 10,
                   d.stale ? kRed : kMuted);
            top += 13;
        }
        m.window({left, top, width, readoutHeight});
        m.text(d.valid     ? capacity(d.available) + (large ? (en ? L" free" : L" 可用") : L"")
               : d.loading ? (en ? L"Reading…" : L"读取中…")
                           : (en ? L"Unavailable" : L"不可用"),
               {left + 3, top, width - 6, readoutHeight}, 13);
        if (large || a.height >= 52) {
            m.text(d.stale   ? (en ? L"CACHED" : L"旧数据")
                   : d.valid ? (large ? (en ? L"Total " : L"总计 ") : L"/ ") + capacity(d.total)
                             : L"",
                   {left, top + readoutHeight + 3, width, 11}, 10, d.stale ? kRed : kMuted);
        }
        return;
    }
    const int total = int(settings.at(d.resting ? "break_minutes" : "focus_minutes").integer * 60);
    const auto time = std::to_wstring(d.remaining / 60) + L":" + (d.remaining % 60 < 10 ? L"0" : L"") +
                      std::to_wstring(d.remaining % 60);
    const auto hits = hitRegions(spec, d, settings, {0, 0, w, h});
    const double available = std::max(8., hits.front().rect.y - a.y - 7);
    const double diameter = std::min(a.width, available);
    if (large && diameter >= 62) {
        m.gauge({a.x + (a.width - diameter) * .5, a.y, diameter, diameter}, double(d.remaining) / std::max(1, total),
                L"", L"", true, total / 60., true);
        const double ww = diameter * .66, hh = std::max(20., diameter * .24);
        const cybercanvas::Rect window{w * .5 - ww * .5, a.y + diameter * .60, ww, hh};
        m.window(window);
        m.text(time, window, std::clamp(diameter * .22, 16., 28.));
    } else {
        const cybercanvas::Rect window{a.x + 2, a.y + 1, a.width - 4, std::max(10., available - 2)};
        m.window(window);
        m.text(time, window, std::clamp(available * .65, 14., 30.));
    }
    for (const auto& hit : hits) {
        const auto& r = hit.rect;
        m.button({r.x, r.y, r.width, r.height}, controls.visual(uint32_t(hit.action)).pressed);
        m.text(hit.action == 2 ? (en ? L"Reset" : L"复位")
               : d.running     ? (en ? L"Pause" : L"暂停")
                               : (en ? L"Start" : L"开始"),
               {r.x + 3, r.y, r.width - 6, r.height}, 11, 0x172021FF);
    }
}
