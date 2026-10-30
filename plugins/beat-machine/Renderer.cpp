#include <algorithm>
#include <cmath>
#include <numbers>

#include "plugins/beat-machine/Machine.h"
#include "plugins/mechanical-instruments/PhysicsMaterials.h"

namespace beatmachine {
namespace {
using cybercanvas::Canvas;
using cybercanvas::Rect;

void capsule(Canvas& c, Point a, Point b, double radius, uint32_t color) {
    c.line(a.x, a.y, b.x, b.y, color, radius * 2);
    c.oval(a.x, a.y, radius, radius, color);
    c.oval(b.x, b.y, radius, radius, color);
}

void rail(Canvas& c, const Segment& segment) {
    const auto a = segment.first, b = segment.second;
    capsule(c, {a.x, a.y + 2}, {b.x, b.y + 2}, segment.radius + 2, 0x05080bd0);
    capsule(c, a, b, segment.radius, 0x677377ff);
    c.line(a.x, a.y - 1, b.x, b.y - 1, 0xcbd0caff, 1.4);
}

void metal(Canvas& c, Rect r, double radius, bool brass = false) {
    c.box(r.x + 1, r.y + 2, r.width, r.height, radius, 0x030609b0);
    c.gradientBox(r, radius, brass ? 0xe8ca82ff : 0xe0e4e2ff, brass ? 0x796036ff : 0x545f65ff);
    c.line(r.x + radius, r.y + 1.5, r.x + r.width - radius, r.y + 1.5, 0xfff8dfc0, 1);
}

void bolt(Canvas& c, Point p, double radius = 4) {
    c.oval(p.x, p.y, radius + 1, radius + 1, 0x0d1418ff);
    c.oval(p.x, p.y, radius, radius, 0xb7a46fff);
    c.oval(p.x, p.y, radius * .55, radius * .55, 0x202a2eff);
    c.line(p.x - radius * .4, p.y + radius * .25, p.x + radius * .4, p.y - radius * .25, 0xadb9b4ff, .9);
}

void label(Canvas& c, std::wstring_view text, Rect r, double size, uint32_t color, bool bold = false) {
    cybercanvas::TextStyle style;
    style.family = L"Bahnschrift";
    style.size = size;
    style.bold = bold;
    c.text(std::wstring(text), style, r, color);
}

void spring(Canvas& c, Point top, Point bottom, double radius, unsigned turns = 6) {
    const double dx = bottom.x - top.x, dy = bottom.y - top.y;
    const double length = std::hypot(dx, dy);
    if (length < 2) {
        return;
    }
    auto rear = c.path(), front = c.path();
    Point previous{};
    for (unsigned i = 0; i <= turns * 24; ++i) {
        const double t = double(i) / (turns * 24), a = t * turns * std::numbers::pi * 2;
        const double side = std::cos(a) * radius, depth = std::sin(a) * radius * .22;
        const Point p{top.x + dx * t + dy / length * side + dx / length * depth,
                      top.y + dy * t - dx / length * side + dy / length * depth};
        if (i) {
            auto& path = std::sin(a - .01) >= 0 ? front : rear;
            path.moveTo(previous).lineTo(p);
        }
        previous = p;
    }
    c.stroke(rear, 0x060d10ff, {.width = 4.2});
    c.stroke(rear, 0x61716dff, {.width = 2.7});
    c.stroke(front, 0x0b1417ff, {.width = 4.5});
    c.stroke(front, 0xbdc8bfff, {.width = 2.8});
    c.saved([&] {
        c.translate(-.35, -.5);
        c.stroke(front, 0xfff1d2b0, {.width = .8});
    });
}

using mechanicalart::physicsmaterials::sprite;
}  // namespace

void Renderer::bind(const CdCreateContext& context) {
    images_.bind(context);
}

Renderer::Renderer(const std::filesystem::path& assets, bool compact) : assetRoot_(assets) {
    if (!assets.empty()) {
        images_.set("ball", assets / "steel-marble.png", 1, 128);
        images_.set("chassis", assets / (compact ? "chassis-4x2.png" : "chassis-6x2.png"), 1, 1280);
        images_.set("cap", assets / "piston-cap.png", 1, 384);
        images_.set("base", assets / "piston-base.png", 1, 256);
        images_.set("paddle", assets / "paddle.png", 1, 512);
        images_.set("bumper", assets / "bumper.png", 1, 320);
        images_.set("launcher", assets / "launcher.png", 1, 512);
    }
}

bool Renderer::pollAssets() {
    const bool changed = images_.poll();
    assetRevision_ += changed;
    return changed;
}

bool Renderer::pendingAssets() const {
    return images_.pending();
}

cdsdk::ImageSlotStats Renderer::assetStats() const {
    return images_.stats();
}

void Renderer::pauseAssets(bool paused) {
    images_.pause(paused);
}

void Renderer::stationary(Canvas& c, const Model& model, Appearance appearance) {
    const double width = model.width();
    const bool compact = model.compact();
    const bool chassisReady = appearance.background && mechanicalart::physicsmaterials::plate(
                                                           c, images_, false, compact, {0, 0, width, kHeight});
    if (appearance.background && !chassisReady) {
        c.gradientBox({3, 4, width - 6, 348}, 20, 0xbfc6c8ff, 0x515a5fff);
        c.gradientBox({13, 17, width - 26, 323}, 14, 0x292e32ff, 0x12191dff);
    }
    if (appearance.background) {
        c.gradientBox({width - 183, 18, 132, 25}, 10, 0x596265ff, 0x222b2eff);
        c.box(width - 181, 20, 128, 21, 8, 0x10191dff);
    }
    for (size_t i = 0; i < 4; ++i) {
        const auto& piston = model.pistons()[i];
        const double floor = 330;
        const double half = compact ? 34 : 47;
        if (!sprite(c, images_, "base",
                    {piston.rest.x - half, floor - (compact ? 18 : 24), half * 2, compact ? 18. : 24.})) {
            c.box(piston.rest.x - half, 230, half * 2, floor - 227, 8, 0x050b0da0);
            metal(c, {piston.rest.x - half + 5, floor - 7, half * 2 - 10, 15}, 3, true);
            metal(c, {piston.rest.x - half + 9, floor - 16, half * 2 - 18, 21}, 5, true);
            c.box(piston.rest.x - half + 19, floor - 12, half * 2 - 38, 13, 3, 0x121b1eff);
        }
    }
    for (const auto& paddle : model.paddles()) {
        metal(c, {paddle.rest.x - 18, paddle.rest.y - 17, 37, 80}, 6, true);
        c.gradientBox({paddle.rest.x - 12, paddle.rest.y + 7, 25, 47}, 3, 0x483921ff, 0x92713aff);
        bolt(c, {paddle.rest.x - 11, paddle.rest.y + 57}, 3);
    }
    for (const auto& bumper : model.bumpers()) {
        const double r = compact ? 35 : 42;
        c.oval(bumper.x, bumper.y + 3, r - 3, r - 3, 0x02080bbb);
        if (!sprite(c, images_, "bumper", {bumper.x - r, bumper.y - r * 1.09, r * 2, r * 2})) {
            c.oval(bumper.x, bumper.y, r - 6, r - 6, 0x576565ff);
            c.oval(bumper.x, bumper.y, r - 9, r - 9, 0x0b1419ff);
        }
    }
    if (!sprite(c, images_, "launcher", {width - 91, 191, 42, 146})) {
        metal(c, {width - 94, 206, 48, 126}, 8, true);
        c.box(width - 87, 213, 34, 107, 5, 0x0a1317ff);
    }
    for (const auto& segment : model.rails()) {
        if (segment.first.y != 65 || segment.second.y != 65) {
            rail(c, segment);
        }
    }
}

void Renderer::draw(Canvas& c, const Model& model, const Signal& signal, Appearance appearance, double scale) {
    mechanicalart::physicsmaterials::requestPlate(images_, assetRoot_, model.compact(),
                                                  model.width() * scale * c.devicePixelRatio());
    pollAssets();
    const bool compact = model.compact();
    const auto dpi = c.devicePixelRatio();
    const auto width = uint32_t(std::clamp(std::ceil(model.width() * scale * dpi), 1., 8192.));
    const auto height = uint32_t(std::clamp(std::ceil(kHeight * scale * dpi), 1., 8192.));
    if (uint64_t(width) * height <= 2 * 1024 * 1024) {
        if (!backdrop_.bitmap() || owner_.Get() != c.target.Get() || width != width_ || height != height_ ||
            dpi != dpi_ || scale != scale_ || appearance.light != appearance_.light ||
            appearance.background != appearance_.background || assetRevision_ != cachedAssetRevision_) {
            scale_ = scale;
            c.renderLayer(backdrop_, {width, height, dpi}, [&](auto& cache) {
                cache.scale(scale, scale);
                stationary(cache, model, appearance);
            });
            owner_ = c.target.Get();
            width_ = width;
            height_ = height;
            dpi_ = dpi;
            appearance_ = appearance;
            cachedAssetRevision_ = assetRevision_;
        }
        c.target->DrawBitmap(backdrop_.bitmap(), D2D1::RectF(0, 0, float(model.width()), float(kHeight)), 1,
                             D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    } else {
        scale_ = scale;
        stationary(c, model, appearance);
    }
    const std::array<double, 3> energy{signal.bass, signal.middle, signal.high};
    for (size_t i = 0; i < energy.size(); ++i) {
        const double x = model.width() - 163 + i * 39.;
        c.oval(x, 31, 7, 7, 0x142126ff);
        const uint32_t color = i == 2 ? 0x80e8f0 : 0xf6c477;
        c.oval(x, 31, 4.5, 4.5, (color << 8) | uint32_t(50 + std::clamp(energy[i], 0., 1.) * 205));
    }
    for (size_t i = 0; i < model.pistons().size(); ++i) {
        const auto& piston = model.pistons()[i];
        const auto state = model.state(piston.body);
        const Point p{state.position.x * kUnits, state.position.y * kUnits};
        const double floor = i == 4 ? 334 : 330;
        if (i < 4 && p.y + 12 < floor) {
            spring(c, {p.x, p.y + 9}, {p.x, floor - 3}, i == 4 ? 9 : compact ? 11 : 16);
        }
        metal(c, {p.x - 6, p.y + 5, 12, std::max(2., floor - p.y - 8)}, 2);
        const double half = i == 4 ? 25 : compact ? 36 : 52;
        c.saved([&] {
            c.translate(p.x, p.y);
            if (i == 4) {
                c.rotate(-.55 * 180 / std::numbers::pi);
            }
            if (!sprite(c, images_, "cap", {-half, -8, half * 2, i == 4 ? 14. : compact ? 21. : 25.})) {
                metal(c, {-half, -7, half * 2, 14}, 5);
                c.line(-half + 7, 4, half - 7, 4, 0x333f44ff, 2);
            }
        });
        if (piston.until > model.time()) {
            c.line(p.x - half + 8, p.y - 8, p.x + half - 8, p.y - 8, 0xffc969ff, 2);
            if (i < 4) {
                c.box(piston.rest.x - 18, 321, 36, 4, 1, 0xe7ba69ff);
            }
        }
    }
    for (const auto& paddle : model.paddles()) {
        const auto state = model.state(paddle.body);
        const double length = compact ? 70 : 105;
        const Point end{paddle.rest.x + length * std::cos(state.angle), paddle.rest.y + length * std::sin(state.angle)};
        spring(c, {paddle.rest.x + 2, paddle.rest.y + 53},
               {paddle.rest.x + length * .45 * std::cos(state.angle),
                paddle.rest.y + length * .45 * std::sin(state.angle)},
               5, 5);
        if (images_.get("paddle").image) {
            c.saved([&] {
                c.translate(paddle.rest.x, paddle.rest.y);
                c.rotate(state.angle * 180 / std::numbers::pi);
                mechanicalart::physicsmaterials::anchored(c, *images_.get("paddle").image, {2172, 724}, {389, 362},
                                                          length / (1950 - 389));
            });
        } else {
            rail(c, {paddle.rest, end, compact ? 6.5 : 8});
            c.oval(paddle.rest.x, paddle.rest.y, compact ? 11 : 14, compact ? 11 : 14, 0xc5a25cff);
            bolt(c, paddle.rest, 6);
        }
    }
    for (size_t i = 0; i < model.bumpers().size(); ++i) {
        const auto p = model.bumpers()[i];
        const double flash = model.flashes()[i];
        const double radius = compact ? 24.2 : 29;
        c.oval(p.x, p.y, radius, radius, 0x3b777f40, 1);
        if (flash > 0) {
            c.oval(p.x, p.y, radius + 1 + flash * 2, radius + 1 + flash * 2, 0x79c8cd00 | uint32_t(flash * 90), 2);
            c.oval(p.x, p.y, radius, radius, 0xd8d2a700 | uint32_t(50 + flash * 120), 1);
        }
        if (!images_.get("bumper").image) {
            c.oval(p.x, p.y, 24, 24, 0x9d7a3dff);
            c.oval(p.x - 1, p.y - 2, 20, 19, 0xd8bd77ff);
            c.oval(p.x, p.y, 12, 12, 0x536265ff);
            bolt(c, p, 7);
        }
    }
    const auto image = images_.get("ball").image;
    for (const auto& ball : model.balls()) {
        const auto state = model.state(ball.body);
        const double x = state.position.x * kUnits, y = state.position.y * kUnits;
        c.oval(x + 1, y + 2, kRadius, kRadius, 0x010509a0);
        if (image) {
            mechanicalart::physicsmaterials::registeredImage(c, *image,
                                                             mechanicalart::physicsmaterials::registration("ball"),
                                                             {x - kRadius, y - kRadius, kRadius * 2, kRadius * 2});
        } else {
            c.oval(x, y, kRadius, kRadius, 0xb1bdbaff);
            c.oval(x, y + 1, 8, 7, 0x384950ff);
            c.oval(x - 3, y - 4, 4, 3, 0xf5fcf7ff);
        }
    }
    const bool unavailable = appearance.status == CD_DATA_UNAVAILABLE || appearance.status == CD_DATA_ERROR;
    const auto text = unavailable ? (appearance.english ? L"AUDIO UNAVAILABLE" : L"音频设备不可用")
                      : appearance.status == CD_DATA_LOADING ? (appearance.english ? L"CONNECTING" : L"连接音频")
                                                             : (appearance.english ? L"SYSTEM AUDIO" : L"系统音频");
    if (appearance.status != CD_DATA_READY) {
        label(c, text, {compact ? 250. : 310., 23, compact ? 170. : 250., 20}, 12,
              appearance.background ? 0x283b40ff : 0xc1cecbff);
    }
}
}  // namespace beatmachine
