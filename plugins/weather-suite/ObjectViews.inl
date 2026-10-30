void objectWeather(const Snapshot& data, bool loading, double width, double height, const std::wstring& city) {
    using namespace mechanicalart;
    const auto stage = arrange(Kind::Weather, width, height);
    stage.draw(canvas, [&] {
        artwork.body(canvas, Kind::Weather, stage);
        const auto ticks = [&](Point center, double radius) {
            for (int i = 0; i < 40; ++i) {
                const auto a = polar(center, radius, 140 + i * 260. / 39);
                const auto b = polar(center, radius * (i % 5 ? .96 : .90), 140 + i * 260. / 39);
                canvas.line(a.x, a.y, b.x, b.y, 0xcbbd92a0, i % 5 ? .8 : 1.8);
            }
        };
        ticks({265, 295}, 151);
        ticks({577, 295}, 94);
        ticks({837, 295}, 94);
        // Fixed single-line width uses DirectWrite's character ellipsis, including
        // CJK and supplementary Unicode names, instead of drawing over the bezel.
        mechanicalart::text(canvas, city, {165, 194, 200, 43}, 31);
        weatherIcon(data.valid ? data.code : -1, data.day, D2D1::RectF(203, 244, 327, 350));
        mechanicalart::text(canvas, temperature(data.temperature, data.valid), {150, 348, 230, 88}, 80);
        if (!data.valid) {
            mechanicalart::text(canvas, summary(data, loading), {160, 392, 210, 35}, 24, 0xbabeb0ff, false);
        }
        const double humidity = data.needles[0] >= 0 ? data.needles[0] : data.humidity / 100;
        const double wind = data.needles[1] >= 0 ? data.needles[1] : data.wind / 25;
        needle(canvas, {577, 295}, 43, 140 + 260 * std::clamp(humidity, 0., 1.), data.valid);
        needle(canvas, {837, 295}, 43, 140 + 260 * std::clamp(wind, 0., 1.), data.valid);
        mechanicalart::text(canvas, data.valid ? rounded(data.humidity) + L"%" : L"—", {521, 323, 112, 44}, 36);
        mechanicalart::text(canvas, data.valid ? decimal(data.wind * (imperial ? 2.236936 : 1)) : L"—",
                            {785, 316, 104, 43}, 36);
        mechanicalart::text(canvas, imperial ? L"mph" : L"m/s", {803, 354, 68, 27}, 20, kIvory, false);
        for (unsigned i = 0; i < 3; ++i) {
            const auto day = i < data.days.size() ? &data.days[i] : nullptr;
            const double x = 388 + i * 172.;
            weatherIcon(day ? day->code : -1, true, D2D1::RectF(float(x + 56), 437, float(x + 112), 474), false,
                        "forecast:" + std::to_string(i));
            mechanicalart::text(canvas, day ? range(*day) : L"— / —", {x + 6, 473, 158, 41}, 29, kIvory, false);
        }
    });
    artwork.icon(canvas, stage, {561, 218, 32, 32}, Icon::Humidity, 0xc3d5d9ff);
    artwork.icon(canvas, stage, {821, 218, 32, 32}, Icon::Wind, 0xd7dfd6ff);
}
