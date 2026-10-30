// Included inside Renderer::Impl. Digits are code-native shapes, with cached
// seven-segment paths and layered, bounded emission on the software surface.
std::array<ComPtr<ID2D1PathGeometry>, 7> segments;
ComPtr<ID2D1Factory> segmentFactory;

void panel(Rect r, CdColor color, double radius = 2) {
    brush->SetColor(paint(color));
    target->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(float(r.x), float(r.y), float(r.x + r.width), float(r.y + r.height)),
                          float(radius), float(radius)),
        brush.Get());
}

void led(Rect r, CdColor color, const Options& options, double radius = 0) {
    const double glow = std::clamp(options.glowPercent / 100., 0., 1.);
    for (int layer = 6; layer >= 1 && glow > 0; --layer) {
        const double spread = layer * std::min(1.0, std::min(r.width, r.height) * .16) * glow;
        auto halo = options.accent;
        halo.a = uint8_t(255 * glow * .045 * (7 - layer));
        panel({r.x - spread, r.y - spread, r.width + 2 * spread, r.height + 2 * spread}, halo, radius + spread);
    }
    panel(r, color, radius);
}

void segmentShapes() {
    if (segmentFactory != factory) {
        for (auto& segment : segments) {
            segment.Reset();
        }
        segmentFactory = factory;
    }
    if (segments[0]) {
        return;
    }
    const std::array<std::array<D2D1_POINT_2F, 6>, 7> points{
        {{{{.14f, .07f}, {.24f, 0}, {.77f, 0}, {.87f, .07f}, {.75f, .16f}, {.25f, .16f}}},
         {{{.88f, .10f}, {.96f, .18f}, {.94f, .86f}, {.86f, .96f}, {.78f, .85f}, {.80f, .22f}}},
         {{{.86f, 1.04f}, {.94f, 1.13f}, {.92f, 1.83f}, {.82f, 1.91f}, {.75f, 1.80f}, {.77f, 1.16f}}},
         {{{.12f, 1.94f}, {.22f, 1.85f}, {.74f, 1.85f}, {.84f, 1.94f}, {.73f, 2}, {.23f, 2}}},
         {{{.03f, 1.12f}, {.12f, 1.04f}, {.20f, 1.15f}, {.18f, 1.80f}, {.10f, 1.91f}, {.02f, 1.83f}}},
         {{{.05f, .18f}, {.15f, .10f}, {.22f, .22f}, {.20f, .85f}, {.12f, .96f}, {.04f, .86f}}},
         {{{.13f, 1}, {.24f, .92f}, {.74f, .92f}, {.85f, 1}, {.74f, 1.08f}, {.24f, 1.08f}}}}};
    for (size_t i = 0; i < segments.size(); ++i) {
        auto path = canvas.path();
        path.moveTo({points[i][0].x, points[i][0].y});
        for (size_t vertex = 1; vertex < points[i].size(); ++vertex) {
            path.lineTo({points[i][vertex].x, points[i][vertex].y});
        }
        path.close();
        segments[i] = path.geometry(factory.Get());
    }
}

void lcdDigit(int digit, Rect r, const Options& options) {
    segmentShapes();
    constexpr unsigned bits[]{0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f};
    D2D1_MATRIX_3X2_F previous;
    target->GetTransform(&previous);
    target->SetTransform(D2D1::Matrix3x2F::Scale(float(r.width), float(r.height / 2)) *
                         D2D1::Matrix3x2F::Translation(float(r.x), float(r.y)) * previous);
    const double glow = std::clamp(options.glowPercent / 100., 0., 1.);
    for (size_t i = 0; i < 7; ++i) {
        const bool lit = (bits[std::clamp(digit, 0, 9)] & (1u << i)) != 0;
        if (lit && glow > 0)
            for (int layer = 5; layer >= 1; --layer) {
                brush->SetColor(paint(options.accent, glow * .05 * (6 - layer)));
                target->DrawGeometry(segments[i].Get(), brush.Get(), float(layer * .028 * glow));
            }
        brush->SetColor(paint(options.primary, lit ? 1 : .055));
        target->FillGeometry(segments[i].Get(), brush.Get());
    }
    target->SetTransform(previous);
}

void lcdText(const std::wstring& value, Rect box, const Options& options) {
    double units = 0;
    for (auto c : value)
        units += c == L':' ? .35 : 1.14;
    units -= .14;
    const double digitWidth = std::min(box.width / std::max(1., units), box.height * .48) * options.sizePercent / 100.,
                 digitHeight = digitWidth * 2;
    double x = box.x + (box.width - units * digitWidth) * .5;
    const double y = box.y + (box.height - digitHeight) * .5;
    for (auto c : value) {
        if (c == L':') {
            const double side = std::max(1., digitWidth * .16);
            for (const double position : {.32, .68})
                led({x + digitWidth * .03, y + digitHeight * position - side * .5, side, side}, options.primary,
                    options, side * .1);
            x += digitWidth * .35;
        } else {
            lcdDigit(int(c - L'0'), {x, y, digitWidth, digitHeight}, options);
            x += digitWidth * 1.14;
        }
    }
}

void pixelText(const std::wstring& value, Rect box, const Options& options, bool round = false) {
    constexpr unsigned digits[10][7]{{7, 5, 5, 5, 5, 5, 7}, {2, 6, 2, 2, 2, 2, 7}, {7, 1, 1, 7, 4, 4, 7},
                                     {7, 1, 1, 7, 1, 1, 7}, {5, 5, 5, 7, 1, 1, 1}, {7, 4, 4, 7, 1, 1, 7},
                                     {7, 4, 4, 7, 5, 5, 7}, {7, 1, 1, 2, 2, 2, 2}, {7, 5, 5, 7, 5, 5, 7},
                                     {7, 5, 5, 7, 1, 1, 7}};
    int columns = -1;
    for (auto c : value)
        columns += c == L':' ? 2 : 4;
    const double cell = std::min(box.width / (columns + 2), box.height / 9) * options.sizePercent / 100.;
    const int gridColumns = std::max(columns + 2, int(std::floor(box.width / cell))),
              gridRows = std::max(9, int(std::floor(box.height / cell)));
    // Anchor the dot envelope to all four box edges. Distribute the remaining
    // space between dots instead of leaving different residual outer margins.
    const double side = cell * .80, pitchX = (box.width - side) / (gridColumns - 1),
                 pitchY = (box.height - side) / (gridRows - 1);
    const double radius = round ? cell * .40 : .15;
    const auto inactive = CdColor{48, 39, 50, 255};
    for (int row = 0; row < gridRows; ++row)
        for (int col = 0; col < gridColumns; ++col)
            panel({box.x + col * pitchX, box.y + row * pitchY, side, side}, inactive, radius);
    double x = box.x + ((gridColumns - columns) / 2) * pitchX;
    const double y = box.y + ((gridRows - 7) / 2) * pitchY;
    for (auto c : value) {
        const bool colon = c == L':';
        const int width = colon ? 1 : 3;
        for (int row = 0; row < 7; ++row)
            for (int col = 0; col < width; ++col) {
                const bool lit = colon ? (row == 2 || row == 4)
                                       : (digits[std::clamp(int(c - L'0'), 0, 9)][row] & (1u << (2 - col))) != 0;
                if (lit)
                    led({x + col * pitchX, y + row * pitchY, side, side}, options.primary, options, radius);
            }
        x += (colon ? 2 : 4) * pitchX;
    }
}

void electronic(Spec spec, const Options& options, Time time, double w, double h) {
    const bool pixel = isPixel(spec.display), round = spec.display == Display::PixelRound,
               footer = options.date || options.weekday || options.seconds && spec.columns == 2;
    const double pad = std::clamp(std::min(w, h) * .055, 3., 7.), bottom = footer ? std::clamp(h * .18, 12., 22.) : 0;
    // Match the spectrum plate: face edges (1,1,w-3,h-4), with room
    // below/right for the relief instead of an extra outer margin.
    const double radius = std::clamp(options.cornerRadius, 0., std::min(w - 4, h - 5) * .5);
    panel({3, 5, w - 4, h - 6}, CdColor{0, 0, 0, 21}, radius);
    panel({1, 3, w - 2, h - 4}, CdColor{0, 0, 0, 12}, radius);
    ComPtr<ID2D1RoundedRectangleGeometry> mask;
    require(factory->CreateRoundedRectangleGeometry(
        D2D1::RoundedRect(D2D1::RectF(1, 1, float(w - 3), float(h - 4)), float(radius), float(radius)),
        mask.GetAddressOf()));
    canvas.masked(mask.Get(), [&] {
        panel({1, 1, w - 4, h - 5}, pixel ? options.cardColor : CdColor{240, 240, 224, 255}, radius);
        const double innerRadius = std::max(0., radius - pad + 1);
        if (!pixel) {
            panel({pad, pad, w - pad * 2, h - pad * 2}, options.cardColor, innerRadius);
        }
        if (!pixel && options.glowPercent > 0) {
            const double g = options.glowPercent / 100.;
            for (int i = 5; i >= 1; --i) {
                brush->SetColor(paint(options.accent, g * .035 * (6 - i)));
                target->DrawRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(float(pad + i * .45), float(pad + i * .45), float(w - pad - i * .45),
                                                  float(h - pad - i * .45)),
                                      float(innerRadius), float(innerRadius)),
                    brush.Get(), float(i * .7));
            }
        }
        auto value = timeText(time, options);
        if (options.seconds && spec.columns == 4) {
            value += L":" + two(time.second);
        }
        const double contentPad = pixel ? 4 : pad + 2;
        const Rect main =
            pixel ? Rect{1 + contentPad, 1 + contentPad, w - 4 - contentPad * 2,
                         std::max(1., h - 5 - contentPad * 2 - bottom)}
                  : Rect{contentPad, contentPad, w - contentPad * 2, std::max(1., h - contentPad * 2 - bottom)};
        if (pixel) {
            pixelText(value, main, options, round);
        } else {
            lcdText(value, main, options);
        }
        if (footer) {
            const double y = h - pad - bottom;
            const auto color = pixel ? options.primary : options.secondary;
            const auto family = std::wstring(fonts->family());
            std::wstring caption;
            if (options.date) {
                caption = std::to_wstring(time.month) + L"/" + std::to_wstring(time.day);
            }
            if (options.weekday) {
                if (!caption.empty()) {
                    caption += L"  ";
                }
                caption += weekdayText(time.weekday, options.language == "en");
            }
            const double right = options.seconds && spec.columns == 2 ? std::min(37., w * .23) : 0;
            label(caption, {pad + 2, y, w - pad * 2 - right - 4, bottom}, color, bottom * .77, family,
                  DWRITE_FONT_WEIGHT_NORMAL, false);
            if (right) {
                if (pixel) {
                    pixelText(two(time.second), {w - pad - right, y - 1, right, bottom + 2}, options, round);
                } else {
                    lcdText(two(time.second), {w - pad - right, y, right, bottom}, options);
                }
            }
        }
    });
}
