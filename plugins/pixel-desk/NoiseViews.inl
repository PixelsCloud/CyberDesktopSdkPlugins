std::array<cdsdk::Scene::Node, 4> noiseRows{}, noiseButtons{}, noiseGroups{}, noiseNames{}, noiseSliders{};
cdsdk::Scene::Node primary = 0, play = 0, timer = 0, noiseSpacer = 0, master = 0;
bool noiseBuilt = false;
int preparedPage = -1;
Canvas* activeNoiseCanvas = nullptr;

void buildNoise(bool compact) {
    using namespace cybercanvas;
    const auto add = [&](Scene::Node parent, ViewKind kind, uint32_t key = 0) {
        View v;
        v.kind = kind;
        v.key = key;
        v.inheritGap = false;
        v.layout.shrink = 0;
        v.theme.padding = 0;
        v.theme.gap = 0;
        v.iconSize = 32;
        if (kind == ViewKind::Slider) {
            v.maximum = 100;
            v.step = 1;
            v.hitSlop = 1.5;
        }
        return scene.add(parent, v);
    };
    if (compact) {
        noiseRows[0] = add(0, ViewKind::Row);
        primary = add(noiseRows[0], ViewKind::Button, 2);
        play = add(noiseRows[0], ViewKind::Button, 1);
        timer = add(0, ViewKind::Button, 3);
    } else {
        const unsigned keys[]{1, 2, 3, 4};
        for (unsigned i = 0; i < 4; ++i) {
            noiseRows[i] = add(0, ViewKind::Row);
            noiseButtons[i] = add(noiseRows[i], ViewKind::Button, keys[i]);
            noiseGroups[i] = add(noiseRows[i], ViewKind::Column);
            noiseNames[i] = add(noiseGroups[i], ViewKind::Text);
            // Key changes with the page; the controller still receives its original
            // six track IDs rather than reducing the mixer to four sound sources.
        }
    }
    noiseSpacer = add(0, ViewKind::Decoration);
    master = add(0, ViewKind::Slider, 10);
    noiseBuilt = true;
}

cdsdk::Scene& prepareNoise(Spec spec, const cdsdk::Settings& settings, const Model& model, double width, double height,
                           double dpi) {
    using namespace cybercanvas;
    const bool compact = spec.rows == 1;
    if (!noiseBuilt) {
        buildNoise(compact);
    }
    const auto& plate = pixeldesk::ambiencePlate(spec.id);
    const auto fit = pixeldesk::fitBody(width, height, plate.visible.width, plate.visible.height);
    const double s = fit.width / plate.visible.width, x = fit.x - plate.visible.x * s, y = fit.y - plate.visible.y * s;
    auto theme = WidgetTheme::dark();
    theme.foreground = pixeldesk::kCream;
    theme.muted = pixeldesk::kMint;
    theme.accent = pixeldesk::kMint;
    theme.track = 0x344b4fff;
    theme.surface = 0;
    theme.border = 0;
    theme.padding = theme.gap = theme.radius = 0;
    theme.font.family = L"Microsoft YaHei";
    theme.font.bold = false;
    theme.font.ellipsis = true;
    theme.font.size = (compact ? 38 : 25) * s;
    theme.thumbRadius = 10 * s;
    scene.theme(theme);
    const auto fixed = [&](Scene::Node node, double w, double h) {
        auto style = scene.view(node).layout;
        style.width = w > 0 ? Length::points(w * s) : Length{};
        style.height = Length::points(h * s);
        style.shrink = 0;
        style.gap = 0;
        return style;
    };
    const double left = compact ? 356 : 486, top = compact ? 193 : 192, span = compact ? 310 : 515,
                 total = compact ? 212 : 385;
    auto root = scene.view(0).layout;
    root.padding = {x + left * s, y + top * s, width - x - (left + span) * s, height - y - (top + total) * s};
    root.gap = 0;
    scene.style(0, root);
    const bool en = settings.at("language").text == "en";
    static constexpr const wchar_t* zh[]{L"雨声", L"海浪", L"溪流", L"鸟鸣", L"炉火", L"微风"};
    static constexpr const wchar_t* english[]{L"Rain", L"Ocean", L"Stream", L"Birds", L"Fire", L"Wind"};
    const auto* names = en ? english : zh;
    const bool playing = model.audioStatus == CD_AMBIENT_PLAYING;
    const bool error = model.audioError != 0;
    if (compact) {
        auto row = fixed(noiseRows[0], 0, 80);
        row.margin.left = 20 * s;
        row.align = FlexAlign::Center;
        scene.style(noiseRows[0], row);
        auto title = fixed(primary, 0, 76);
        title.grow = 1;
        title.basis = Length::points(0);
        scene.style(primary, title);
        scene.style(play, fixed(play, 76, 76));
        scene.text(primary, names[settings.at("primary").integer]);
        scene.accessibleName(primary, en ? L"Change sound" : L"切换声音");
        scene.icon(play, playing ? "pause" : "play");
        scene.accessibleName(play, playing ? (en ? L"Pause" : L"暂停") : (en ? L"Play" : L"播放"));
        scene.theme(play, {.foreground = pixeldesk::kCream, .fontSize = 40 * s});
        auto timed = fixed(timer, 0, 44);
        timed.margin.left = 20 * s;
        scene.style(timer, timed);
        const auto minutes = settings.at("timer_minutes").integer;
        scene.text(timer, error     ? (en ? L"Audio unavailable" : L"音频不可用")
                          : minutes ? std::to_wstring(minutes) + (en ? L" min" : L" 分钟")
                                    : (en ? L"No timer" : L"持续播放"));
        scene.theme(timer, {.foreground = error ? pixeldesk::kClay : pixeldesk::kMint, .fontSize = 26 * s});
        scene.accessibleName(timer, en ? L"Change sleep timer" : L"切换定时停止");
        scene.style(noiseSpacer, fixed(noiseSpacer, 0, 60));
    } else {
        const unsigned normal[]{0, 1, 3, 4}, more[]{2, 5, 0, 4};
        const auto* order = model.noisePage ? more : normal;
        if (preparedPage != model.noisePage) {
            scene.cancel();
            for (auto& node : noiseSliders) {
                if (node) {
                    scene.remove(node);
                }
                node = 0;
            }
            preparedPage = model.noisePage;
        }
        const char* icons[]{playing ? "pause" : "play", "volume-2", "timer", "ellipsis"};
        const wchar_t* labels[]{playing ? (en ? L"Pause" : L"暂停") : (en ? L"Play" : L"播放"),
                                en ? L"Change primary sound" : L"切换主声源",
                                en ? L"Change sleep timer" : L"切换定时停止", en ? L"More sounds" : L"更多声源"};
        for (unsigned i = 0; i < 4; ++i) {
            auto row = fixed(noiseRows[i], 0, 80);
            row.gap = 20 * s;
            row.align = FlexAlign::Start;
            scene.style(noiseRows[i], row);
            auto button = fixed(noiseButtons[i], 44, 44);
            button.margin = {11 * s, 10 * s, 11 * s, 0};
            scene.style(noiseButtons[i], button);
            scene.icon(noiseButtons[i], icons[i]);
            scene.accessibleName(noiseButtons[i], labels[i]);
            scene.theme(noiseButtons[i], {.foreground = pixeldesk::kInk, .fontSize = 38 * s});
            auto group = fixed(noiseGroups[i], 0, 64);
            group.grow = 1;
            group.basis = Length::points(0);
            scene.style(noiseGroups[i], group);
            scene.style(noiseNames[i], fixed(noiseNames[i], 0, 32));
            std::wstring caption = names[order[i]];
            caption += L"  " + std::to_wstring(int(std::lround(model.levels[order[i]] * 100))) + L"%";
            if (i == 0 && error) {
                caption = en ? L"Audio unavailable" : L"音频不可用";
            }
            scene.text(noiseNames[i], caption);
            // Rebind only the changed track IDs on page transitions. Native Scene
            // keeps the slider capture, keyboard values and painting geometry shared.
            if (!noiseSliders[i]) {
                const auto parent = noiseGroups[i];
                View slider;
                slider.kind = ViewKind::Slider;
                slider.key = 20 + order[i];
                slider.inheritGap = false;
                slider.layout = fixed(noiseNames[i], 0, 18);
                slider.maximum = 100;
                slider.step = 1;
                slider.hitSlop = 2 * s;
                noiseSliders[i] = scene.add(parent, slider);
            }
            scene.style(noiseSliders[i], fixed(noiseSliders[i], 0, 18));
            scene.accessibleName(noiseSliders[i], names[order[i]]);
            scene.value(noiseSliders[i], std::round(model.levels[order[i]] * 100));
        }
        scene.style(noiseSpacer, fixed(noiseSpacer, 0, 41));
    }
    auto masterStyle = fixed(master, 0, compact ? 28 : 24);
    masterStyle.margin.left = (compact ? 10 : 20) * s;
    masterStyle.margin.right = (compact ? 10 : 35) * s;
    scene.style(master, masterStyle);
    scene.value(master, std::round(model.master * 100));
    scene.accessibleName(master, en ? L"Master volume" : L"主音量");
    scene.arrange(activeNoiseCanvas ? *activeNoiseCanvas : noiseCanvas, {0, 0, width, height}, dpi);
    return scene;
}

cdsdk::Canvas noiseCanvas;

void noiseThumbs(Canvas& canvas, Spec spec, double width, double height) {
    const auto& plate = pixeldesk::ambiencePlate(spec.id);
    const double scale =
        pixeldesk::fitBody(width, height, plate.visible.width, plate.visible.height).width / plate.visible.width;
    auto nodes = noiseSliders;
    for (unsigned i = 0; i < 5; ++i) {
        const auto node = i == 4 ? master : nodes[i];
        if (!node) {
            continue;
        }
        const auto r = scene.bounds(node);
        const double x = r.x + r.width * scene.view(node).value / 100, y = r.y + r.height * .5;
        pixeldesk::stepped(canvas, {x - 11 * scale, y - 11 * scale, 22 * scale, 22 * scale}, 3 * scale,
                           pixeldesk::kInk);
        pixeldesk::stepped(canvas, {x - 8 * scale, y - 8 * scale, 16 * scale, 16 * scale}, 2 * scale, pixeldesk::kMint);
        canvas.box(x - 5 * scale, y - 6 * scale, 7 * scale, 3 * scale, 0, pixeldesk::kCream);
    }
}

void noiseMotion(Canvas& canvas, Spec spec, const cdsdk::Settings& settings, const Model& model) {
    if (!settings.at("animation").integer || model.audioStatus != CD_AMBIENT_PLAYING || model.master <= 0 ||
        std::none_of(model.levels.begin(), model.levels.end(), [](float value) {
            return value > 0;
        })) {
        return;
    }
    const double time = model.motion.seconds();
    if (spec.rows == 1) {
        canvas.clipped(Rect{85, 205, 230, 155}, [&] {
            for (unsigned i = 0; i < 7; ++i) {
                const double y = 205 + std::fmod(time * 70 + i * 29., 155.);
                canvas.box(90 + i * 30, y, 5, 12, 0, 0x9bd4dd99);
            }
        });
    } else {
        canvas.clipped(Rect{215, 375, 145, 140}, [&] {
            for (unsigned i = 0; i < 6; ++i) {
                const double y = 375 + std::fmod(time * 65 + i * 31., 140.);
                canvas.box(230 + (i % 3) * 33, y, 26, 4, 0, 0xe3fff4aa);
            }
        });
    }
}
