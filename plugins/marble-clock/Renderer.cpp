#include <array>
#include <cmath>
#include <numbers>

#include "plugins/marble-clock/Clock.h"
#include "plugins/mechanical-instruments/PhysicsMaterials.h"

namespace marbleclock {
namespace {
using cybercanvas::Canvas;
using cybercanvas::ImageData;
using cybercanvas::Rect;
constexpr double kDegrees = 180 / std::numbers::pi;

void capsule(Canvas& c, Point a, Point b, double radius, uint32_t fill) {
    c.line(a.x, a.y, b.x, b.y, fill, radius * 2);
    c.oval(a.x, a.y, radius, radius, fill);
    c.oval(b.x, b.y, radius, radius, fill);
}

void label(Canvas& c, const std::wstring& value, Rect area, double size, uint32_t ink,
           cybercanvas::TextAlign align = cybercanvas::TextAlign::Start, bool bold = false) {
    cybercanvas::TextStyle style;
    style.family = L"Microsoft YaHei";
    style.size = size;
    style.bold = bold;
    style.align = align;
    c.text(value, style, area, ink);
}

void metalBox(Canvas& c, Rect r, double radius, bool brass = false) {
    c.box(r.x + 1, r.y + 3, r.width, r.height, radius, 0x040505b0);
    c.gradientBox(r, radius, brass ? 0xd7b86fff : 0xd5dadbff, brass ? 0x66502dff : 0x4b5356ff);
    c.gradientBox({r.x + 2, r.y + 2, r.width - 4, r.height - 4}, std::max(0., radius - 2),
                  brass ? 0x766039ff : 0x41494cff, brass ? 0x342c21ff : 0x131719ff);
}

void bolt(Canvas& c, Point p, double radius = 3) {
    c.oval(p.x, p.y + .8, radius + 1, radius + 1, 0x000000a0);
    c.oval(p.x, p.y, radius, radius, 0xceb77aff);
    c.oval(p.x, p.y, radius * .68, radius * .68, 0x594d32ff);
    c.line(p.x - radius * .35, p.y - radius * .35, p.x + radius * .35, p.y + radius * .35, 0x0d1112ff, 1);
    c.oval(p.x - radius * .35, p.y - radius * .4, radius * .18, radius * .18, 0xf5e6beff);
}

void rail(Canvas& c, const Segment& s, bool brass = false) {
    capsule(c, {s.first.x, s.first.y + 3}, {s.second.x, s.second.y + 3}, s.radius + 1.5, 0x020405a0);
    capsule(c, s.first, s.second, s.radius, brass ? 0x7e673bff : 0x5b666bff);
    capsule(c, {s.first.x, s.first.y - .8}, {s.second.x, s.second.y - .8}, s.radius * .64,
            brass ? 0xcab17aff : 0xb9c3c5ff);
    capsule(c, {s.first.x, s.first.y - 2.2}, {s.second.x, s.second.y - 2.2}, .65, brass ? 0xf6e3b0ff : 0xf1f5f3ff);
    capsule(c, {s.first.x, s.first.y + 2.5}, {s.second.x, s.second.y + 2.5}, .6, 0x2a3033ff);
}

void sprite(Canvas& c, const std::shared_ptr<const ImageData>& image, Rect area) {
    if (image) {
        c.image(*image, area, {.fit = cybercanvas::ImageFit::Stretch, .clip = false});
    }
}

std::unique_ptr<cybercanvas::Path> gearOutline(Canvas& c) {
    auto path = std::make_unique<cybercanvas::Path>(c.factory.Get());
    constexpr int teeth = 16;
    constexpr double pitch = std::numbers::pi * 2 / teeth;
    const auto point = [](double angle, double radius) -> Point {
        return {std::cos(angle) * radius, std::sin(angle) * radius};
    };
    path->moveTo(point(-pitch * .5, 82));
    for (int i = 0; i < teeth; ++i) {
        const double a = i * pitch;
        path->lineTo(point(a - pitch * .32, 82))
            .lineTo(point(a - pitch * .20, 100))
            .lineTo(point(a + pitch * .20, 100))
            .lineTo(point(a + pitch * .32, 82))
            .lineTo(point(a + pitch * .5, 82));
    }
    path->close();
    for (int i = 0; i < 3; ++i) {
        const double angle = i * std::numbers::pi * 2 / 3;
        constexpr double half = std::numbers::pi * 35 / 180;
        path->moveTo(point(angle - half, 67))
            .arcTo(point(angle + half, 67), {67, 67})
            .lineTo(point(angle + half, 37))
            .arcTo(point(angle - half, 37), {37, 37}, 0, cybercanvas::ArcSweep::CounterClockwise)
            .close();
    }
    path->finish();
    return path;
}

D2D1_COLOR_F rgba(uint32_t color) {
    return D2D1::ColorF((color >> 24) / 255.f, ((color >> 16) & 255) / 255.f, ((color >> 8) & 255) / 255.f,
                        (color & 255) / 255.f);
}

void gear(Canvas& c, cybercanvas::Path& outline, const std::shared_ptr<const ImageData>& material, Point center,
          double radius, double angle, double physicalDiameter) {
    c.oval(center.x + .5, center.y + 2, radius, radius, 0x020303a0);
    if (material) {
        c.saved([&] {
            c.translate(center.x, center.y);
            c.rotate(angle * kDegrees);
            mechanicalart::physicsmaterials::anchored(c, *material, {1254, 1254}, {627, 621}, radius / 557);
        });
        return;
    }
    c.saved([&] {
        c.translate(center.x, center.y);
        c.rotate(angle * kDegrees);
        c.scale(radius / 100, radius / 100);
        const std::array<D2D1_GRADIENT_STOP, 5> stops{{{0, rgba(0xffe6a6ff)},
                                                       {.25f, rgba(0xc8a65cff)},
                                                       {.52f, rgba(0x9b793aff)},
                                                       {.72f, rgba(0xe3c278ff)},
                                                       {1, rgba(0x76582aff)}}};
        const auto brass = c.linearGradient({-95, -95}, {85, 95}, stops);
        c.target->FillGeometry(outline.geometry(c.factory.Get()), brass.Get());
        // The silhouette stays vector sharp. Fine surface grain needs enough physical pixels to be visible.
        if (material && physicalDiameter >= 56) {
            c.masked(outline, [&] {
                c.image(*material, {-100, -100, 200, 200},
                        {.fit = cybercanvas::ImageFit::Stretch, .opacity = .16, .clip = false});
            });
        }
        c.stroke(outline, 0xf7d996ff, {.width = 1.7});
        c.oval(0, 0, 79, 79, 0x775829ff, 2.5);
        c.oval(0, 0, 76, 76, 0xe2c482ff, 1.7);
        c.oval(0, 0, 30, 30, 0x5c472aff);
        c.oval(0, 0, 26, 26, 0xdadbcfff);
        c.oval(0, 0, 19, 19, 0x566064ff);
        c.oval(0, 0, 12, 12, 0x141b1eff);
        c.line(-6, -3, 6, 3, 0xa8b1adff, 2.8);
        if (physicalDiameter >= 56) {
            c.oval(0, 0, 33, 33, 0xf2d599ff, 1.4);
            for (int i = 0; i < 3; ++i) {
                const double a = i * std::numbers::pi * 2 / 3;
                c.oval(std::cos(a) * 51, std::sin(a) * 51, 3, 3, 0xf1d491ff);
            }
        }
    });
}

void ball(Canvas& c, const std::shared_ptr<const ImageData>& image, Point p, double angle) {
    c.oval(p.x + 1.3, p.y + 2.2, kRadius, kRadius, 0x02030385);
    if (image) {
        // The light remains fixed in world space; only the engraved detail follows body rotation.
        mechanicalart::physicsmaterials::registeredImage(c, *image,
                                                         mechanicalart::physicsmaterials::registration("ball"),
                                                         {p.x - kRadius, p.y - kRadius, kRadius * 2, kRadius * 2});
    } else {
        c.oval(p.x, p.y, kRadius, kRadius, 0x849398ff);
        c.oval(p.x, p.y + 1, kRadius * .8, kRadius * .7, 0x222c32ff);
        c.oval(p.x - 2.1, p.y - 2.4, 3.9, 3.5, 0xf3f7f4ff);
    }
    c.saved([&] {
        c.translate(p.x, p.y);
        c.rotate(angle * kDegrees);
        c.line(-2.1, 5.5, .6, 6.1, 0xd2d9d54a, .7);
    });
}

void clockWindow(Canvas& c, double width) {
    const double x = width - 365;
    metalBox(c, {x, 23, 328, 69}, 7, true);
    for (int pair = 0; pair < 3; ++pair) {
        const double left = x + 19 + pair * 99;
        for (int digit = 0; digit < 2; ++digit) {
            const double dx = left + digit * 38;
            c.gradientBox({dx, 32, 31, 51}, 3, 0x282b2cff, 0x07090aff);
            c.line(dx + 2, 31, dx + 32, 31, 0x555c5dff, .7);
        }
        if (pair < 2) {
            c.oval(left + 86, 47, 1.9, 1.9, 0xa8afaaff);
            c.oval(left + 86, 66, 1.9, 1.9, 0xa8afaaff);
        }
    }
    for (double bx : {x + 7, x + 321}) {
        bolt(c, {bx, 31}, 2.4);
        bolt(c, {bx, 84}, 2.4);
    }
}

void clockDigits(Canvas& c, const Model& model) {
    const auto value = model.timeText();
    const auto previous = model.previousTimeText();
    const double progress = 1 - std::pow(1 - model.digitProgress(), 3);
    const double x = model.shape().width - 365;
    cybercanvas::TextStyle style;
    style.family = L"Bahnschrift";
    style.size = 42;
    style.align = cybercanvas::TextAlign::Center;
    style.tabularFigures = true;
    for (size_t pair = 0; pair < 3; ++pair) {
        for (size_t digit = 0; digit < 2; ++digit) {
            constexpr std::array<double, 6> offsets{235, 485, 865, 1115, 1495, 1745};
            const double dx = x + (offsets[pair * 2 + digit] - 79) * 328 / 2014;
            const auto index = pair * 3 + digit;
            if (progress < 1 && value[index] != previous[index]) {
                c.clipped({dx, 32, 31, 51}, [&] {
                    c.text(previous.substr(index, 1), style, {dx, 32 - progress * 51, 31, 51}, 0xeee9d9ff);
                    c.text(value.substr(index, 1), style, {dx, 32 + (1 - progress) * 51, 31, 51}, 0xeee9d9ff);
                });
            } else {
                c.text(value.substr(index, 1), style, {dx, 32, 31, 51}, 0xeee9d9ff);
            }
            c.gradientBox({dx, 32, 31, 8}, 0, 0x00000085, 0x00000000);
            c.gradientBox({dx, 72, 31, 11}, 0, 0x00000000, 0x000000a0);
            c.line(dx + 1, 57, dx + 30, 57, 0x02030480, .8);
        }
    }
}
}  // namespace

DriveGeometry driveGeometry(const Geometry& shape, double angle) {
    DriveGeometry result;
    result.center = {shape.pivot + 52, kFloorY};
    result.lever = {shape.pivot + 20 * std::cos(angle), kFloorY + 20 * std::sin(angle)};
    const Point delta{result.lever.x - result.center.x, result.lever.y - result.center.y};
    const double distance = std::hypot(delta.x, delta.y);
    // Circle intersection produces an exact, constant-length connecting rod.
    result.angle = std::atan2(delta.y, delta.x) +
                   std::acos(std::clamp((distance * distance + 144 - 676) / (24 * distance), -1., 1.));
    result.crank = {result.center.x + 12 * std::cos(result.angle), result.center.y + 12 * std::sin(result.angle)};
    return result;
}

Renderer::Renderer(const std::filesystem::path& assets, bool compact) : assetRoot_(assets) {
    images_.onChange([this] {
        ++assetRevision_;
    });
    if (!assets.empty()) {
        images_.set("chassis", assets / (compact ? "chassis-4x2.png" : "chassis-6x2.png"), 1, 1280);
        images_.set("gear", assets / "precision-gear.png", 1, 384);
        images_.set("clock", assets / "clock-window.png", 1, 768);
        images_.set("ball", assets / "steel-marble.png", 1, 128);
    }
}

bool Renderer::pollAssets() {
    return images_.poll();
}

bool Renderer::pendingAssets() const {
    return images_.pending();
}

void Renderer::pauseAssets(bool paused) {
    images_.pause(paused);
}

cdsdk::ImageSlotStats Renderer::assetStats() const {
    return images_.stats();
}

void Renderer::stationary(Canvas& c, const Geometry& shape, Appearance appearance) {
    if (appearance.background) {
        if (mechanicalart::physicsmaterials::plate(c, images_, true, shape.width < 1000,
                                                   {0, 0, shape.width, kHeight})) {
            if (appearance.light) {
                c.box(28, 47, shape.width - 56, 275, 12, 0xd6e0df22);
            }
        } else {
            c.gradientBox({3, 4, shape.width - 6, 348}, 20, 0xbfc6c8ff, 0x515a5fff);
            c.gradientBox({13, 17, shape.width - 26, 323}, 14, appearance.light ? 0x515b60ff : 0x292e32ff,
                          appearance.light ? 0x353e43ff : 0x12191dff);
        }
    }
    if (appearance.background && shape.width < 1000) {
        auto webs = c.path();
        webs.moveTo({225, 43}).lineTo({225, 177}).lineTo({552, 177}).lineTo({552, 92});
        webs.moveTo({390, 43}).lineTo({390, 177});
        webs.moveTo({252, 178}).lineTo({232, 198}).lineTo({232, 218});
        c.saved([&] {
            c.translate(1, 2);
            c.stroke(webs, 0x040608b0, {.width = 14});
        });
        c.stroke(webs, 0x485052ff, {.width = 9});
        c.stroke(webs, 0x252c2eff, {.width = 6});
        for (Point p : {Point{225, 62}, Point{390, 62}, Point{552, 163}}) {
            bolt(c, p, 2.8);
        }
    }
    if (!mechanicalart::physicsmaterials::sprite(c, images_, "clock", {shape.width - 365, 23, 328, 69})) {
        clockWindow(c, shape.width);
    }

    const bool compact = shape.width < 1000;
    const double end = shape.track.front().second.x;
    for (double x : compact ? std::vector<double>{205, 408, 592} : std::vector<double>{218, 496, 780, 935}) {
        const double y = 122 + (x - 92) / (end - 92) * 31;
        metalBox(c, {x - 7, y + 3, 14, 22}, 3);
        bolt(c, {x, y + 16}, 2.7);
    }
    const double floor = floorHeight(shape, shape.left);
    c.box(shape.left - 8, 216, shape.pivot - shape.left + 16, floor - 216 + 11, 6, 0x05090bad);
    c.gradientBox({shape.left + 1, 221, shape.pivot - shape.left - 2, floor - 221 + 4}, 3, 0x101719ff, 0x333b3cff);
    // This cutaway is behind the balls; it does not add a physical lid.
    for (const auto& s : shape.track) {
        rail(c, s);
    }
    for (size_t i = 0; i < 2; ++i) {
        rail(c, shape.enclosure[i]);
    }

    metalBox(c, {40, 65, 66, 63}, 12);
    metalBox(c, {91, 104, 29, 20}, 4);
    c.box(94, 108, 18, 14, 3, 0x0a0f11ff);
    capsule(c, {shape.pivot + 52, 265}, {shape.pivot + 106, 295}, 8, 0x101718ff);
    capsule(c, {shape.pivot + 52, 263}, {shape.pivot + 106, 293}, 5, 0x778082ff);
    c.oval(174, 99, 24, 24, 0x080c0eff);
    c.oval(174, 99, 23, 23, 0xc2ab75ff, 2);
    c.oval(174, 99, 20, 20, 0x5d696aff, .8);
    for (unsigned tick = 0; tick < 30; ++tick) {
        const double angle = tick * std::numbers::pi / 15;
        const double inner = tick % 5 ? 20.5 : 19;
        c.line(174 + inner * std::cos(angle), 99 + inner * std::sin(angle), 174 + 22 * std::cos(angle),
               99 + 22 * std::sin(angle), 0xc2ab75ff, .65);
    }

    // A narrow exposed return leaves the refined lower case edge readable.
    for (const auto& s : shape.returning) {
        rail(c, s);
    }
    metalBox(c, {shape.width - 106, 324, 65, 30}, 5);
    c.box(shape.width - 101, 329, 51, 20, 4, 0x0c1113ff);
}

void Renderer::draw(Canvas& c, const Model& model, Appearance appearance, double scale) {
    const auto& shape = model.shape();
    const double dpi = c.devicePixelRatio();
    mechanicalart::physicsmaterials::requestPlate(images_, assetRoot_, shape.width < 1000, shape.width * scale * dpi);
    pollAssets();
    const auto width = uint32_t(std::clamp(std::ceil(shape.width * scale * dpi), 1., 4096.));
    const auto height = uint32_t(std::clamp(std::ceil(kHeight * scale * dpi), 1., 4096.));
    if (uint64_t(width) * height <= 2 * 1024 * 1024) {
        if (!backdrop_.bitmap() || owner_ != c.target.Get() || width != pixelWidth_ || height != pixelHeight_ ||
            dpi != dpi_ || scale != scale_ || appearance.light != appearance_.light ||
            appearance.background != appearance_.background || appearance.english != appearance_.english ||
            cachedAssetRevision_ != assetRevision_) {
            c.renderLayer(backdrop_, {width, height, dpi}, [&](auto& cached) {
                cached.scale(scale, scale);
                stationary(cached, shape, appearance);
            });
            owner_ = c.target.Get();
            pixelWidth_ = width;
            pixelHeight_ = height;
            dpi_ = dpi;
            scale_ = scale;
            appearance_ = appearance;
            cachedAssetRevision_ = assetRevision_;
        }
        c.target->DrawBitmap(backdrop_.bitmap(), D2D1::RectF(0, 0, float(shape.width), float(kHeight)), 1,
                             D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    } else {
        stationary(c, shape, appearance);
    }

    if (!gearOutline_ || gearFactory_ != c.factory.Get()) {
        gearOutline_ = gearOutline(c);
        gearFactory_ = c.factory.Get();
    }
    const auto material = images_.get("gear").image;
    const auto ballImage = images_.get("ball").image;
    gear(c, *gearOutline_, material, {73, 96}, 29, model.feederAngle(), 58 * scale * dpi);
    const auto drive = driveGeometry(shape, model.flapAngle());
    gear(c, *gearOutline_, material, drive.center, 31, drive.angle, 62 * scale * dpi);
    gear(c, *gearOutline_, material, {drive.center.x + 54, drive.center.y + 30}, 31, -drive.angle + .1,
         62 * scale * dpi);
    c.saved([&] {
        c.translate(shape.pivot, kFloorY);
        c.rotate(model.flapAngle() * kDegrees);
        rail(c, {{shape.left - shape.pivot, 0}, {20, 0}, 4.5});
    });
    capsule(c, {drive.lever.x, drive.lever.y + 2}, {drive.crank.x, drive.crank.y + 2}, 4.2, 0x03050690);
    capsule(c, drive.lever, drive.crank, 3.7, 0x788487ff);
    capsule(c, {drive.lever.x, drive.lever.y - 1}, {drive.crank.x, drive.crank.y - 1}, 1.5, 0xd2d8d4ff);
    bolt(c, drive.lever, 3.1);
    bolt(c, drive.crank, 3.1);

    for (double x : {45., shape.width - 70}) {
        c.oval(x, 351, 4.5, 4.5, 0xa7b0aeff);
        c.oval(x, 351, 2.8, 2.8, 0x3f494dff);
    }
    for (const auto& marble : model.marbles()) {
        const auto state = model.state(marble);
        ball(c, ballImage, {state.position.x * kPixelsPerMeter, state.position.y * kPixelsPerMeter}, state.angle);
    }
    c.oval(shape.pivot, kFloorY, 10, 10, 0x2d3538ff);
    c.oval(shape.pivot, kFloorY, 10, 10, 0xd3c79eff, 2);
    bolt(c, {shape.pivot, kFloorY}, 4.7);
    label(c, (model.second() < 10 ? L"0" : L"") + std::to_wstring(model.second()), {153, 79, 42, 27}, 19, 0xf0e6cbff,
          cybercanvas::TextAlign::Center, true);
    clockDigits(c, model);
    const bool opening = model.phase() == Phase::Opening || model.phase() == Phase::Draining;
    c.box(shape.width - 96, 333, 4, 11, 1.5, opening ? 0xf0c774ff : 0x71634bff);
}
}  // namespace marbleclock
