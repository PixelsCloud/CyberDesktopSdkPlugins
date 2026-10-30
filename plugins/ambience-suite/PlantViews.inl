void leaf(double x, double y, double size, double angle, uint32_t fill, uint32_t edge, bool succulent = false) {
    draw.saved([&] {
        draw.rotate(angle, {x, y});
        auto path = draw.path();
        const auto p = [&](double xx, double yy) -> cybercanvas::Point {
            return {x + xx * size, y + yy * size};
        };
        path.moveTo(p(0, succulent ? -.85 : -.65))
            .cubicTo(p(-1.0, -.9), p(-1.1, .20), p(.03, 1.1))
            .cubicTo(p(.78, .17), p(.93, -.72), p(0, succulent ? -.85 : -.65))
            .close();
        draw.fill(path, fill);
        draw.stroke(path, edge, {.width = std::max(.65, size * .065)});
        if (!succulent) {
            draw.oval(x - size * .22, y - size * .15, size * .27, size * .48, 0xD7EC7A20);
            draw.line(x, y - size * .48, x + size * .02, y + size * .86, 0xCAE88088, std::max(.45, size * .055));
        } else {
            draw.oval(x, y - size * .67, size * .18, size * .13, 0xEAA4A0AA);
        }
    });
}

void paintPlant(double x, double bottom, double height, int type, const Model& m, double growth, bool dark,
                bool watering) {
    const double age = m.interaction.seconds();
    const double sway = m.sway(type * .7) * 2.8 + m.reaction() * 6;
    height *= .48 + growth * .52;
    const double potW = height * .43, potH = height * .25, soil = bottom - potH + height * .025;
    const uint32_t outline = dark ? 0x173E32FF : 0x31543BFF;
    potShadows.ellipse(draw, {x - potW * .61, bottom - height * .025, potW * 1.22, height * .08},
                       std::max(.6, height * .012), {}, dark ? 0x00000038 : 0x7C6A4B28);
    const uint32_t pot = type == 0   ? (dark ? 0xBDB79CFF : 0xF2DDADFF)
                         : type == 1 ? (dark ? 0xB48268FF : 0xE7AA7BFF)
                                     : (dark ? 0x73927FFF : 0xA8BE8DFF);
    const double top = bottom - potH;
    auto potShape = draw.path();
    potShape.moveTo({x - potW * .53, top})
        .lineTo({x + potW * .53, top})
        .cubicTo({x + potW * .49, bottom - potH * .2}, {x + potW * .34, bottom}, {x + potW * .20, bottom})
        .lineTo({x - potW * .20, bottom})
        .cubicTo({x - potW * .34, bottom}, {x - potW * .49, bottom - potH * .2}, {x - potW * .53, top})
        .close();
    draw.fill(potShape, pot);
    draw.stroke(potShape, dark ? 0x3E5249FF : 0x775C42FF, {.width = std::max(.8, height * .009)});
    draw.oval(x, top + potH * .06, potW * .53, potH * .13, dark ? 0x938872FF : 0xEED5A5FF);
    draw.oval(x, soil, potW * .44, potH * .10, watering && age < 3 ? 0x4B4339FF : 0x75624AFF);
    draw.line(x - potW * .37, top + potH * .23, x - potW * .30, bottom - potH * .23, 0xFFF7DA44,
              std::max(.8, potW * .08));
    if (type == 0) {
        const int leaves = int(4 + growth * 11);
        constexpr double positions[15][2]{{-.22, -.82}, {.03, -.91}, {.25, -.79},  {-.36, -.64}, {-.10, -.67},
                                          {.18, -.62},  {.37, -.57}, {-.29, -.48}, {-.01, -.48}, {.26, -.42},
                                          {-.37, -.28}, {.35, -.26}, {-.15, -.35}, {.12, -.31},  {0, -.72}};
        for (int i = 0; i < leaves; ++i) {
            const double lx = x + positions[i][0] * height + m.sway(i * .6) * height * .012,
                         ly = bottom + positions[i][1] * height;
            draw.line(x, soil, lx, ly - height * .02, dark ? 0x426B49FF : 0x608846FF, std::max(.8, height * .012));
        }
        for (int i = 0; i < leaves; ++i) {
            const double a = i * 2.399963;
            const double lx = x + positions[i][0] * height + m.sway(i * .6) * height * .012,
                         ly = bottom + positions[i][1] * height;
            const uint32_t green = dark ? (i % 3 ? 0x3F8164FF : 0x62935AFF) : (i % 3 ? 0x559F36FF : 0x8ABA3EFF);
            leaf(lx, ly, height * (.12 + .018 * std::sin(i * 2)), std::sin(a) * -32 + sway, green, outline);
        }
    } else if (type == 1) {
        const double cy = soil - height * .16;
        for (int layer = 0; layer < 3; ++layer) {
            for (int i = 0; i < 7; ++i) {
                const double a = i * 6.283185 / 7 + layer * .4;
                const double r = height * (.17 - layer * .048), lx = x + std::sin(a) * r,
                             ly = cy + std::cos(a) * r * .55;
                leaf(lx, ly, height * (.13 - layer * .018), -a * 180 / 3.141593 + sway * .3,
                     dark ? (layer == 0 ? 0x447C6AFF : 0x78A383FF) : (layer == 0 ? 0x72AA85FF : 0xA0C99AFF), outline,
                     true);
            }
        }
    } else {
        for (int i = 0; i < 8; ++i) {
            const double a = i * 2.399963;
            leaf(x + std::sin(a) * height * .16, soil - height * (.09 + .15 * (i % 3) / 2.), height * .09,
                 std::sin(a) * 70 + sway, dark ? 0x49865BFF : 0x67A542FF, outline);
        }
        for (int i = 0; i < 5; ++i) {
            const double fx = x + std::sin(i * 2.4) * height * .22 + sway * height * .008,
                         fy = bottom - height * (.61 + .22 * (i % 3) / 2.);
            draw.line(x, soil, fx, fy, dark ? 0x4A7F4EFF : 0x6C9847FF, std::max(.8, height * .013));
            leaf((x + fx) * .5 + (i % 2 ? -1 : 1) * height * .045, (soil + fy) * .5, height * .065, i % 2 ? 65 : -65,
                 dark ? 0x4F865CFF : 0x81AC49FF, outline);
            const double blossom = height * .065 * std::clamp((growth - .30) * 2.2, .15, 1.);
            for (int petal = 0; petal < 8; ++petal) {
                const double a = petal * 6.283185 / 8 + m.sway() * .02;
                draw.oval(fx + std::cos(a) * blossom, fy + std::sin(a) * blossom, blossom * .74, blossom * .74,
                          dark ? 0xC8D7D0FF : 0xFFFEF1FF);
                draw.oval(fx + std::cos(a) * blossom, fy + std::sin(a) * blossom, blossom * .74, blossom * .74,
                          dark ? 0x799C8AFF : 0x8BA783FF, std::max(.35, height * .004));
            }
            draw.oval(fx, fy, blossom * .65, blossom * .65, dark ? 0xD6B65FFF : 0xF4C74CFF);
        }
    }
    if (watering && age < 2.4) {
        draw.withOpacity(m.waterOpacity(), [&] {
            static const cybercanvas::Tween<double> fall(
                0, 1, {.duration = 1 / .85, .repeat = cybercanvas::Repeat::Loop, .cycles = 0});
            for (int i = 0; i < 7; ++i) {
                const double progress = fall.sample(age + i * .16 / .85).value;
                const double dx = x + height * (.23 - progress * .18 + std::sin(i * 2.) * .10),
                             dy = bottom - height * .92 + progress * height * .72;
                draw.oval(dx, dy, height * .017, height * .026, 0x6EC2E3CC);
            }
        });
    }
}

void plant(double x, double bottom, double height, int type, const Model& model, double growth, bool dark,
           bool watering) {
    auto& group = plantGroups.at(size_t(type));
    if (!group) {
        cybercanvas::View view;
        view.kind = cybercanvas::ViewKind::Column;
        view.paintLocal = [this](cybercanvas::Canvas&, cybercanvas::Rect, const cybercanvas::WidgetTheme&) {
            paintPlantGroup();
        };
        group = std::make_unique<cybercanvas::Scene>(std::move(view));
    }
    const cybercanvas::Point base{height * .7, height * 1.12};
    group->arrange(draw, {x - base.x, bottom - base.y, height * 1.4, height * 1.2}, draw.devicePixelRatio());
    group->transform(0, {.pivot = base, .rotation = model.sway(type * .7) * .65});
    paintPlantGroup = [&] {
        paintPlant(base.x, base.y, height, type, model, growth, dark, watering);
    };
    group->draw(draw);
    paintPlantGroup = {};
}

void plants(Spec spec, const cdsdk::Settings& settings, const Model& model, double w, double h, bool dark) {
    const bool garden = spec.columns >= 4 && settings.at("garden").integer;
    const bool info = spec.rows > 1 && settings.at("show_info").integer;
    const uint32_t fg = dark ? 0xE3EDDBFF : 0x294B34FF, muted = dark ? 0x8CAAA0FF : 0x748371FF;
    const double growth = model.growth(wallSeconds(), settings.at("mode").text == "grow");
    const auto species = settings.at("species").text;
    const bool en = settings.at("language").text == "en";
    const int type = species == "succulent" ? 1 : species == "daisy" ? 2 : 0;
    if (info && h > 110) {
        draw.text(garden      ? (en ? L"Little garden" : L"我的小花园")
                  : type == 0 ? (en ? L"Pothos" : L"绿萝")
                  : type == 1 ? (en ? L"Succulent" : L"多肉")
                              : (en ? L"Daisy" : L"小雏菊"),
                  std::clamp(w * .07, 10., 16.), {10, 5, w - 20, 23}, fg, false, true);
    }
    const double bottom = h - (info ? 25 : 7);
    if (garden) {
        const double height = std::min(h - (info && h > 110 ? 55 : 24), w * .35);
        for (int i = 0; i < 3; ++i) {
            Model pose = model;
            pose.motion.seek(pose.motion.seconds() + i * .8);
            plant(w * (.18 + i * .32), bottom, height, i, pose, growth, dark, model.waterFeedback);
        }
    } else {
        const double height = std::min(h - (info && h > 110 ? 55 : info ? 34 : 13), w);
        plant(w * .5, bottom, std::max(12., height), type, model, growth, dark, model.waterFeedback);
    }
    if (info) {
        const auto status = model.interaction.seconds() < 2 && model.waterFeedback ? (en ? L"Watered" : L"已浇水")
                            : settings.at("mode").text == "grow"
                                ? (growth >= .99 ? (en ? L"Mature" : L"已长成") : (en ? L"Growing" : L"生长中"))
                                : (en ? L"Ornamental" : L"观赏");
        draw.text(status, 9, {9, h - 23, w - 45, 18}, muted, false);
        auto theme = dark ? cybercanvas::WidgetTheme::dark() : cybercanvas::WidgetTheme::light();
        theme.surface = dark ? 0x456F73FF : 0xD6E8E1FF;
        theme.foreground = dark ? 0x9ED2DFFF : 0x4D9DB6FF;
        theme.accent = theme.surface;
        theme.onAccent = theme.foreground;
        cybercanvas::IconButton water(1, {w - 28, h - 25, 20, 20}, "droplet", L"", cybercanvas::ButtonStyle::Filled);
        water.round = true;
        water.iconSize = 12;
        water.draw(draw, theme, icons.painter(), plantControls.visual(1));
    }
}
