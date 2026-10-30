// Foliage geometry reused from ambience-suite/PlantViews.inl. The approved whale pots stay stationary.
void leaf(Canvas& draw, double x, double y, double size, double angle, uint32_t fill, uint32_t edge,
          bool succulent = false) {
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

void foliage(Canvas& draw, double x, double soil, double height, int type, const Model& m, double growth, bool dark) {
    const double sway = m.sway(type * .7) * 2.8 + m.reaction() * 6;
    const uint32_t outline = dark ? 0x173e32ff : 0x31543bff;
    height *= .48 + growth * .52;
    const double bottom = soil + height * .22;
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
            leaf(draw, lx, ly, height * (.12 + .018 * std::sin(i * 2)), std::sin(a) * -32 + sway, green, outline);
        }
    } else if (type == 1) {
        const double cy = soil - height * .16;
        for (int layer = 0; layer < 3; ++layer) {
            for (int i = 0; i < 7; ++i) {
                const double a = i * 6.283185 / 7 + layer * .4;
                const double r = height * (.17 - layer * .048), lx = x + std::sin(a) * r,
                             ly = cy + std::cos(a) * r * .55;
                leaf(draw, lx, ly, height * (.13 - layer * .018), -a * 180 / 3.141593 + sway * .3,
                     dark ? (layer == 0 ? 0x447C6AFF : 0x78A383FF) : (layer == 0 ? 0x72AA85FF : 0xA0C99AFF), outline,
                     true);
            }
        }
    } else {
        for (int i = 0; i < 8; ++i) {
            const double a = i * 2.399963;
            leaf(draw, x + std::sin(a) * height * .16, soil - height * (.09 + .15 * (i % 3) / 2.), height * .09,
                 std::sin(a) * 70 + sway, dark ? 0x49865BFF : 0x67A542FF, outline);
        }
        for (int i = 0; i < 5; ++i) {
            const double fx = x + std::sin(i * 2.4) * height * .22 + sway * height * .008,
                         fy = bottom - height * (.61 + .22 * (i % 3) / 2.);
            draw.line(x, soil, fx, fy, dark ? 0x4A7F4EFF : 0x6C9847FF, std::max(.8, height * .013));
            leaf(draw, (x + fx) * .5 + (i % 2 ? -1 : 1) * height * .045, (soil + fy) * .5, height * .065,
                 i % 2 ? 65 : -65, dark ? 0x4F865CFF : 0x81AC49FF, outline);
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
}
