// Included inside Canvas::Impl. Cache the static grid and small light textures;
// live frames only stamp changed spectrum levels into the instance's surface.
using MatrixKey = std::tuple<Kind, float, float, double, int, bool, uint32_t, uint32_t, uint32_t, float, double>;

struct MatrixCache {
    std::optional<MatrixKey> key;
    ComPtr<ID2D1RenderTarget> owner;
    MatrixGeometry grid;
    float halo = 0, spriteSize = 0;
    ComPtr<ID2D1RoundedRectangleGeometry> mask;
    cybercanvas::SurfaceImage background;
    std::vector<cybercanvas::SurfaceImage> lights;
};

MatrixCache matrixCache;
cybercanvas::SpritePalette matrixPalette;

template <typename Draw>
cybercanvas::SurfaceImage matrixBitmap(float w, float h, Draw draw) {
    cybercanvas::OffscreenSurface surface;
    surface.render(
        target.Get(), {uint32_t(std::ceil(w * dpr)), uint32_t(std::ceil(h * dpr)), dpr},
        [&](auto* destination) {
            ComPtr<ID2D1SolidColorBrush> pen;
            require(destination->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0), pen.GetAddressOf()));
            draw(surface, destination, pen.Get());
        },
        canvas.resourceAccount());
    // Retain the finished bitmap, not a separate render target for each lamp row.
    return surface.snapshot();
}

static void matrixDot(ID2D1RenderTarget* surface, ID2D1SolidColorBrush* pen, bool round, float x, float y,
                      float diameter) {
    if (round) {
        surface->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(x + diameter * .5f, y + diameter * .5f), diameter * .5f, diameter * .5f), pen);
    } else {
        surface->FillRectangle(D2D1::RectF(x, y, x + diameter, y + diameter), pen);
    }
}

void prepareMatrix(Spec spec, const cdsdk::Settings& settings, float w, float h) {
    const int rows = int(settings.at("matrix_rows").integer);
    const bool background = settings.at("show_background").integer != 0, round = spec.kind == Kind::RoundMatrix;
    const auto plate = uint32_t(settings.at("background").integer), base = uint32_t(settings.at("color").integer),
               top = uint32_t(settings.at("color_top").integer);
    const float radius =
        std::clamp(float(settings.at("background_corner_radius").integer), 0.f, std::min(w - 4, h - 5) * .5f);
    const double glow = settings.at("glow_strength").number / 100;
    const MatrixKey key{spec.kind, w, h, dpr, rows, background, plate, base, top, radius, glow};
    if (matrixCache.owner == target && matrixCache.key && *matrixCache.key == key) {
        return;
    }

    MatrixCache cache;
    cache.owner = target;
    cache.grid = arrangeMatrix(w, h, rows);
    const auto& grid = cache.grid;
    cache.halo = std::max(1.f, grid.diameter * .38f * float(glow));
    cache.spriteSize = float(std::ceil((grid.diameter + 2 * cache.halo) * dpr) / dpr);
    require(factory->CreateRoundedRectangleGeometry(D2D1::RoundedRect(D2D1::RectF(1, 1, w - 3, h - 4), radius, radius),
                                                    cache.mask.GetAddressOf()));

    cache.background = matrixBitmap(w, h, [&](auto& offscreen, ID2D1RenderTarget* surface, ID2D1SolidColorBrush* pen) {
        if (background) {
            pen->SetColor(paint(0x00000015));
            surface->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(3, 5, w - 1, h - 1), radius, radius), pen);
            pen->SetColor(paint(0x0000000C));
            surface->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(1, 3, w - 1, h - 1), radius, radius), pen);
        }
        offscreen.masked(cache.mask.Get(), [&] {
            if (background) {
                pen->SetColor(paint(plate));
                surface->FillRectangle(D2D1::RectF(1, 1, w - 3, h - 4), pen);
            }

            pen->SetColor(paint(0x302732FF));
            for (int row = 0; row < grid.rows; ++row) {
                for (int column = 0; column < grid.columns; ++column) {
                    matrixDot(surface, pen, round, grid.left + column * grid.pitchX, grid.top + row * grid.pitchY,
                              grid.diameter);
                }
            }
        });
    });

    const auto lowerColor = paint(base), upperColor = paint(top);
    for (int row = 0; row < grid.rows; ++row) {
        const float blend = float(grid.rows - 1 - row) / (grid.rows - 1);
        const auto color =
            D2D1::ColorF(std::lerp(lowerColor.r, upperColor.r, blend), std::lerp(lowerColor.g, upperColor.g, blend),
                         std::lerp(lowerColor.b, upperColor.b, blend), std::lerp(lowerColor.a, upperColor.a, blend));
        cache.lights.push_back(matrixBitmap(
            cache.spriteSize, cache.spriteSize, [&](auto&, ID2D1RenderTarget* surface, ID2D1SolidColorBrush* pen) {
                for (int layer = 3; layer >= 1 && glow > 0; --layer) {
                    auto halo = color;
                    halo.a *= float(glow) * .065f * (4 - layer);
                    pen->SetColor(halo);
                    const float spread = cache.halo * layer / 3;
                    matrixDot(surface, pen, round, cache.halo - spread, cache.halo - spread,
                              grid.diameter + spread * 2);
                }

                pen->SetColor(color);
                matrixDot(surface, pen, round, cache.halo, cache.halo, grid.diameter);
                const float shine = float(glow) * .28f, inset = grid.diameter * .22f;
                pen->SetColor(D2D1::ColorF(std::lerp(color.r, 1.f, shine), std::lerp(color.g, 1.f, shine),
                                           std::lerp(color.b, 1.f, shine), color.a));
                matrixDot(surface, pen, round, cache.halo + inset, cache.halo + inset, grid.diameter - inset * 2);
            }));
    }
    cache.key = key;
    matrixPalette.build(canvas, cache.lights);
    matrixCache = std::move(cache);
}

void matrix(Spec spec, const cdsdk::Settings& settings, const Spectrum& spectrum, float w, float h) {
    if (w <= 16 || h <= 16) {
        return;
    }
    prepareMatrix(spec, settings, w, h);
    const auto& grid = matrixCache.grid;
    target->DrawBitmap(matrixCache.background.Get(), D2D1::RectF(0, 0, w, h));
    matrixPalette.begin(canvas);
    canvas.masked(matrixCache.mask.Get(), [&] {
        const auto light = [&](int column, int row, float opacity) {
            const float x = grid.left + column * grid.pitchX - matrixCache.halo,
                        y = grid.top + row * grid.pitchY - matrixCache.halo;
            matrixPalette.add(unsigned(row), {x, y, matrixCache.spriteSize, matrixCache.spriteSize}, opacity);
        };
        const double step = grid.columns > 1 ? double(Spectrum::count - 1) / (grid.columns - 1) : 0;
        const double half = grid.columns == 1 ? double(Spectrum::count - 1) * .5
                                              : (grid.columns < int(Spectrum::count) ? step * .5 : 0);
        for (int column = 0; column < grid.columns; ++column) {
            const double position = grid.columns == 1 ? half : column * step;
            const float level = std::clamp(spectrum.range(position - half, position + half), 0.f, 1.f),
                        peak = std::clamp(spectrum.range(position - half, position + half, true), 0.f, 1.f);
            for (int row = 0; row < grid.rows; ++row) {
                const float opacity = std::clamp(level * grid.rows - (grid.rows - 1 - row), 0.f, 1.f);
                if (opacity > 0) {
                    light(column, row, opacity);
                }
            }
            if (settings.at("peaks").integer && peak > level + .5f / grid.rows) {
                const int row = grid.rows - std::clamp(int(std::ceil(peak * grid.rows)), 1, grid.rows);
                light(column, row, .8f);
            }
        }
        matrixPalette.flush(canvas);
    });
}
