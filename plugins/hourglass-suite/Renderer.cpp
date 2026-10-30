#include <algorithm>
#include <array>
#include <cmath>

#include "plugins/hourglass-suite/Hourglass.h"
#include "plugins/hourglass-suite/PhysicalSand.h"
#include "sdk/Canvas.h"

namespace hourglassplugin {
namespace {
std::wstring remainingText(int seconds) {
    const int minutes = seconds / 60, remainder = seconds % 60;
    return (minutes < 10 ? L"0" : L"") + std::to_wstring(minutes) + L":" + (remainder < 10 ? L"0" : L"") +
           std::to_wstring(remainder);
}
}  // namespace

struct Renderer::Impl {
    cdsdk::Canvas canvas;

    void rect(double x, double y, double w, double h, uint32_t fill, float opacity = 1) {
        canvas.rectangle({x, y, w, h}, fill, opacity);
    }

    void label(const std::wstring& text, float size, CdRect area, uint32_t fill) {
        cybercanvas::TextStyle style;
        style.family = L"Microsoft YaHei";
        style.locale = L"zh-CN";
        style.size = size;
        // Keep the original natural-height baseline while sharing the engine cache.
        style.vertical = cybercanvas::VerticalAlign::Top;
        const auto metrics = canvas.measureText(text, style, {1000, 1000});
        canvas.text(text, style, {area.x, area.y + (area.height - metrics.height) * .5, 1000, 1000}, fill);
    }

    void dot(double x, double y, double side, uint32_t fill, double glow) {
        if (glow > 0) {
            const double spread = std::min(side * .45, 1.2);
            rect(x - spread, y - spread, side + spread * 2, side + spread * 2, fill, float(glow * .10));
        }
        rect(x, y, side, side, fill);
    }

    void digits(const std::wstring& text, CdRect area, uint32_t ink, uint32_t dim, double glow) {
        // Use the same 3x7 digit alphabet as the built-in pixel electronic clock.
        constexpr unsigned glyphs[10][7]{{7, 5, 5, 5, 5, 5, 7}, {2, 6, 2, 2, 2, 2, 7}, {7, 1, 1, 7, 4, 4, 7},
                                         {7, 1, 1, 7, 1, 1, 7}, {5, 5, 5, 7, 1, 1, 1}, {7, 4, 4, 7, 1, 1, 7},
                                         {7, 4, 4, 7, 5, 5, 7}, {7, 1, 1, 2, 2, 2, 2}, {7, 5, 5, 7, 5, 5, 7},
                                         {7, 5, 5, 7, 1, 1, 7}};
        const double pitch = std::max(.1, std::min(area.width / 18, area.height / 7));
        const double side = pitch * .76;
        double x = area.x;
        const double y = area.y + (area.height - pitch * 7) * .5;
        for (wchar_t character : text) {
            const bool colon = character == L':';
            for (int row = 0; row < 7; ++row) {
                for (int column = 0; column < (colon ? 1 : 3); ++column) {
                    const bool lit =
                        colon ? row == 2 || row == 4
                              : (glyphs[std::clamp(int(character - L'0'), 0, 9)][row] & (1u << (2 - column))) != 0;
                    dot(x + column * pitch, y + row * pitch, side, lit ? ink : dim, lit ? glow : 0);
                }
            }
            x += pitch * (colon ? 2 : 4);
        }
    }

    void content(const Model& model, const Options& options, double w, double h) {
        const uint32_t ink = options.color == "mint"   ? (options.dark ? 0x9AE7BCFF : 0x2F8967FF)
                             : options.color == "cyan" ? (options.dark ? 0x83DAF2FF : 0x247FA0FF)
                                                       : (options.dark ? 0xF0C57AFF : 0xAA6C2FFF);
        const uint32_t bright = options.color == "mint"   ? 0xDCFCE6FF
                                : options.color == "cyan" ? 0xD7F8FFFF
                                                          : 0xFFF2CCFF;
        const uint32_t frame = options.dark ? 0x739495FF : 0x708582FF;
        const uint32_t ghost = options.dark ? 0x26383BFF : 0xD4DCD4FF;
        const uint32_t muted = options.dark ? 0x8EA9A8FF : 0x607774FF;
        const auto glass = glassArea(w, h, options.showTimer);
        const double pixel = glass.height / kGridHeight;
        const double side = pixel * .83;
        canvas.saved([&] {
            const cybercanvas::Point center{glass.x + glass.width * .5, glass.y + glass.height * .5};
            const double radians = model.angle() * 3.14159265358979323846 / 180;
            const double rotatedWidth =
                glass.width * std::abs(std::cos(radians)) + glass.height * std::abs(std::sin(radians));
            const double rotatedHeight =
                glass.height * std::abs(std::cos(radians)) + glass.width * std::abs(std::sin(radians));
            const float fit = float(std::min({1.0, std::max(1.0, (options.showTimer ? w * .38 : w) - 8) / rotatedWidth,
                                              std::max(1.0, h - 12) / rotatedHeight}));
            canvas.rotate(model.angle(), center);
            canvas.scale(fit, fit, center);
            for (int y = 2; y <= 26; ++y) {
                const int half = chamberHalfWidth(y);
                for (int x = 9 - half; x <= 9 + half; ++x) {
                    rect(glass.x + x * pixel, glass.y + y * pixel, side, side, ghost, .26f);
                }
                for (int sign : {-1, 1}) {
                    const int x = 9 + sign * (half + 1);
                    dot(glass.x + x * pixel, glass.y + y * pixel, side, frame, 0);
                }
            }
            for (int y : {0, 1, 27, 28}) {
                for (int x = y == 0 || y == 28 ? 0 : 2; x <= (y == 0 || y == 28 ? 18 : 16); ++x) {
                    dot(glass.x + x * pixel, glass.y + y * pixel, side, y == 0 || y == 27 ? frame : ghost, 0);
                }
            }
            const auto sand = model.sample();
            const auto grains = [&](const std::vector<Cell>& cells, bool falling) {
                for (const auto cell : cells) {
                    const bool sparkle = (cell.x * 17 + cell.y * 11) % 9 == 0;
                    dot(glass.x + cell.x * pixel, glass.y + cell.y * pixel, side,
                        falling || options.dark && sparkle ? bright : ink, options.glow);
                }
            };
            if (options.physicalSand) {
                size_t index = 0;
                for (const auto point : options.physicalSand->sample()) {
                    const double grainSide = pixel * .78;
                    dot(glass.x + point.x * pixel - grainSide * .5, glass.y + point.y * pixel - grainSide * .5,
                        grainSide, index++ % 9 == 0 ? bright : ink, options.glow);
                }
            } else {
                grains(sand.upper, false);
                grains(sand.lower, false);
                grains(sand.falling, true);
            }
        });

        if (!options.showTimer) {
            return;
        }
        const double x = w * .42, right = w - 10, areaWidth = std::max(1.0, right - x);
        const double textSize = std::clamp(h * .11, 6.0, 11.0);
        label(L"像素沙漏", float(textSize), {x, h * .14, areaWidth, h * .16}, muted);
        digits(remainingText(model.remaining()), {x, h * .34, areaWidth, h * .30}, ink, ghost, options.glow);
        const double barY = h * .72, barWidth = areaWidth;
        const int segments = std::max(4, int(barWidth / 5));
        const double pitch = barWidth / segments;
        for (int i = 0; i < segments; ++i) {
            const bool lit = double(i + 1) / segments <= model.progress() + .000001;
            rect(x + i * pitch, barY, pitch * .70, std::max(1.0, h * .026), lit ? ink : ghost);
        }
        const std::wstring status = !options.running                   ? L"已暂停"
                                    : model.phase() == Phase::Turning  ? L"翻转中"
                                    : model.phase() == Phase::Finished ? L"已结束"
                                                                       : L"流动中";
        label(status, float(std::clamp(h * .09, 6.0, 9.5)), {x, h * .80, areaWidth, h * .12}, muted);
        const double mark = std::max(1.0, pixel * .5);
        if (options.running && model.phase() != Phase::Finished) {
            for (int i = 0; i < 3; ++i) {
                rect(right - mark * 5, h * .83 + mark * i, mark, mark * .8, muted);
                rect(right - mark * 2, h * .83 + mark * i, mark, mark * .8, muted);
            }
        } else {
            for (int i = 0; i < 3; ++i) {
                rect(right - mark * 5 + i * mark, h * .83 + mark * i * .5, mark, mark * (3 - i), muted);
            }
        }
    }
};

Renderer::Renderer() : impl_(std::make_unique<Impl>()) {}

Renderer::~Renderer() = default;

void Renderer::bindOutput(const CdCreateContext& context) {
    impl_->canvas.bindGpu(context);
}

bool Renderer::render(const Model& model, const Options& options, const CdFrame& frame, CdRect safe) {
    try {
        auto& p = *impl_;

        const double w = frame.width / frame.device_pixel_ratio, h = frame.height / frame.device_pixel_ratio;
        if (safe.width <= 0 || safe.height <= 0) {
            safe = {0, 0, w, h};
        }
        safe.x = std::clamp(safe.x, 0.0, w);
        safe.y = std::clamp(safe.y, 0.0, h);
        safe.width = std::clamp(safe.width, 0.0, w - safe.x);
        safe.height = std::clamp(safe.height, 0.0, h - safe.y);
        auto painting = p.canvas.frame(frame, safe);
        if (safe.width > 24 && safe.height > 24) {
            if (options.background) {
                const float radius =
                    std::clamp(options.radius, 0.f, float(std::min(safe.width - 4, safe.height - 5) * .5));
                const cybercanvas::Rect face{1, 1, safe.width - 4, safe.height - 5};
                p.canvas.box(face.x, face.y, face.width, face.height, radius, options.dark ? 0x10292DFF : 0xEDF1E6FF);
                p.canvas.strokeBox(face, radius, options.dark ? 0x698A8952 : 0x8FA49677, {.width = .65f});
            }
            p.content(model, options, safe.width, safe.height);
        }
        painting.finish();
        return true;
    } catch (...) {
        return false;
    }
}

bool Renderer::savePng(const std::wstring& filename, const CdFrame& frame) {
    return impl_->canvas.save(filename, frame);
}
}  // namespace hourglassplugin
