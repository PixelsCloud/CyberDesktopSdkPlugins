#include <algorithm>
#include <cmath>
#include <numbers>

#include "plugins/precision-clocks/Clock.h"

namespace precisionclock {
namespace {
using cybercanvas::Canvas;
using cybercanvas::Point;
using cybercanvas::Rect;
constexpr double kTau = 2 * std::numbers::pi;

struct Gear {
    Point center;
    double radius;
    unsigned teeth;
    bool reverse;
};

// Pitch radii share a common tooth size. Adjacent wheels alternate direction;
// their fixed pivots are supported by the stationary bridge layer.
constexpr std::array<Gear, 6> kGears{{{{458, 339}, 90, 45, false},
                                      {{367, 449}, 59, 30, true},
                                      {{391, 588}, 88, 44, false},
                                      {{566, 592}, 94, 47, true},
                                      {{668, 490}, 56, 28, false},
                                      {{612, 373}, 79, 40, true}}};

void circle(cybercanvas::Path& path, Point center, double radius) {
    path.moveTo({center.x + radius, center.y})
        .arcTo({center.x - radius, center.y}, {radius, radius})
        .arcTo({center.x + radius, center.y}, {radius, radius})
        .close();
}

void screw(Canvas& c, Point p, double radius, bool gold = true) {
    c.oval(p.x + 1, p.y + 2, radius + 2, radius + 2, 0x030507c0);
    c.oval(p.x, p.y, radius, radius, gold ? 0xc6a568ff : 0xb7bfc0ff);
    c.oval(p.x - .6, p.y - .8, radius - 1.5, radius - 1.5, gold ? 0xf7df9f90 : 0xffffff90, 1.3);
    c.line(p.x - radius * .55, p.y + radius * .33, p.x + radius * .55, p.y - radius * .33, 0x181e20ff, 1.8);
}

void bridges(Canvas& c) {
    const auto support = [&](Point a, Point b, double width) {
        const double angle = std::atan2(b.y - a.y, b.x - a.x);
        const Point side{-std::sin(angle) * width, std::cos(angle) * width};
        auto path = c.path();
        path.moveTo({a.x + side.x, a.y + side.y})
            .cubicTo({a.x + (b.x - a.x) * .35 + side.x * .5, a.y + (b.y - a.y) * .35 + side.y * .5},
                     {a.x + (b.x - a.x) * .65 + side.x * .5, a.y + (b.y - a.y) * .65 + side.y * .5},
                     {b.x + side.x, b.y + side.y})
            .quadraticTo({b.x + std::cos(angle) * width * 1.4, b.y + std::sin(angle) * width * 1.4},
                         {b.x - side.x, b.y - side.y})
            .cubicTo({a.x + (b.x - a.x) * .65 - side.x * .5, a.y + (b.y - a.y) * .65 - side.y * .5},
                     {a.x + (b.x - a.x) * .35 - side.x * .5, a.y + (b.y - a.y) * .35 - side.y * .5},
                     {a.x - side.x, a.y - side.y})
            .quadraticTo({a.x - std::cos(angle) * width * 1.4, a.y - std::sin(angle) * width * 1.4},
                         {a.x + side.x, a.y + side.y})
            .close();
        c.saved([&] {
            c.translate(2, 4);
            c.fill(path, 0x000000b0);
        });
        c.masked(path, [&] {
            c.gradientBox({260, 250, 490, 500}, 0, 0xd3d6d3ff, 0x535b5dff);
            for (double y = 250; y < 750; y += 4) {
                c.line(260, y, 750, y, 0xffffff18, .6);
            }
        });
        c.stroke(path, 0x171c1fff, {.width = 2.4});
        c.line(a.x + side.x * .7, a.y + side.y * .7, b.x + side.x * .7, b.y + side.y * .7, 0xf0f1e6b0, 1.5);
    };
    support({300, 345}, kGears[0].center, 20);
    support(kGears[0].center, {548, 281}, 18);
    support({282, 510}, kGears[1].center, 20);
    support(kGears[1].center, kGears[2].center, 17);
    support(kGears[2].center, {352, 689}, 18);
    support({693, 369}, kGears[5].center, 19);
    support(kGears[5].center, kGears[4].center, 16);
    support(kGears[4].center, kGears[3].center, 18);
    support(kGears[3].center, {678, 651}, 18);
    for (const auto& gear : kGears) {
        c.oval(gear.center.x, gear.center.y, 13, 13, 0x252c2dff);
        c.oval(gear.center.x, gear.center.y, 10, 10, 0xb6bcb9ff);
        c.oval(gear.center.x, gear.center.y, 6, 6, 0x503254ff);
        c.oval(gear.center.x - 1.5, gear.center.y - 1.5, 2, 2, 0xf0cbe8ff);
    }
    support({416, 734}, {560, 641}, 15);
    support({568, 738}, {560, 641}, 15);
    support({560, 641}, {493, 677}, 10);
    screw(c, {416, 734}, 7, false);
    screw(c, {568, 738}, 7, false);
}

void cog(Canvas& c, const Gear& gear, size_t index, const std::shared_ptr<const cybercanvas::ImageData>& brass) {
    const double r = gear.radius;
    auto outline = c.path();
    for (unsigned i = 0; i < gear.teeth * 4; ++i) {
        const double angle = (double(i / 4) + (i % 4 == 0   ? -.48
                                               : i % 4 == 1 ? -.26
                                               : i % 4 == 2 ? .26
                                                            : .48)) *
                             kTau / gear.teeth;
        const double radius = r * (i % 4 == 0 || i % 4 == 3 ? .92 : 1);
        const Point point{radius * std::cos(angle), radius * std::sin(angle)};
        if (i) {
            outline.lineTo(point);
        } else {
            outline.moveTo(point);
        }
    }
    outline.close();
    if (index) {
        for (unsigned hole = 0; hole < 6; ++hole) {
            const double angle = hole * kTau / 6;
            circle(outline, {std::cos(angle) * r * .53, std::sin(angle) * r * .53}, r * .235);
        }
    }
    c.masked(outline, [&] {
        c.gradientBox({-r, -r, r * 2, r * 2}, 0, 0xefcf85ff, 0x7d592aff);
        if (brass) {
            c.withOpacity(.32, [&] {
                c.image(*brass, {-r, -r, r * 2, r * 2}, {.fit = cybercanvas::ImageFit::Stretch, .clip = false});
            });
        }
        for (int ray = 0; ray < 90; ++ray) {
            const double angle = ray * kTau / 90;
            c.line(std::cos(angle) * r * .2, std::sin(angle) * r * .2, std::cos(angle) * r, std::sin(angle) * r,
                   0xffefd528, .6);
        }
    });
    c.stroke(outline, 0xf8dfa4d0, {.width = 1.3});
    c.oval(0, 0, r * .86, r * .86, 0x6e4c22b0, 1.8);
    if (!index) {
        c.oval(0, 0, r * .79, r * .79, 0x5e6668ff);
        for (int ring = 0; ring < 28; ++ring) {
            const double radius = r * (.25 + ring * .019);
            c.oval(0, 0, radius, radius, ring % 2 ? 0xced1c76a : 0x161c1eb0, 1);
        }
        c.line(-r * .45, -r * .42, r * .34, r * .53, 0xdfe2d580, 3);
    }
    c.oval(0, 0, r * .19, r * .19, 0x2c2821ff);
    c.oval(0, 0, r * .145, r * .145, 0xd8bc7cff);
    screw(c, {}, std::clamp(r * .065, 3., 7.));
}

void star(Canvas& c, Point center, double radius) {
    auto path = c.path();
    path.moveTo({center.x, center.y - radius})
        .lineTo({center.x + radius * .23, center.y - radius * .23})
        .lineTo({center.x + radius, center.y})
        .lineTo({center.x + radius * .23, center.y + radius * .23})
        .lineTo({center.x, center.y + radius})
        .lineTo({center.x - radius * .23, center.y + radius * .23})
        .lineTo({center.x - radius, center.y})
        .lineTo({center.x - radius * .23, center.y - radius * .23})
        .close();
    c.fill(path, 0xe5c58bff);
}
}  // namespace

Renderer::Renderer(Kind kind, const std::filesystem::path& assets) : spec_(specification(kind)), assets_(assets) {
    if (!assets_.empty()) {
        edge_ = 512;
        images_.set("face", assets_ / "face.png", 0, edge_);
        if (kind == Kind::Skeleton) {
            images_.set("brass", assets_ / "brass-material.png", 0, 64);
        }
    }
}

bool Renderer::poll() {
    const auto previous = revision_;
    const bool changed = images_.poll();
    const auto accept = [&](const char* key, auto& image) {
        const auto next = images_.get(key).image;
        if (next && image != next) {
            image = next;
            ++revision_;
        }
    };
    accept("face", face_);
    accept("brass", brass_);
    return changed || previous != revision_;
}

bool Renderer::pending() const {
    return images_.pending();
}

void Renderer::pause(bool paused) {
    images_.pause(paused);
}

cdsdk::ImageSlotStats Renderer::assets() const {
    return images_.stats();
}

void Renderer::base(Canvas& c, const Options&) {
    if (face_) {
        c.image(*face_, {0, 0, kExtent, kExtent}, {.fit = cybercanvas::ImageFit::Stretch, .clip = false});
        return;
    }
    const bool dress = spec_.kind == Kind::Dress, green = spec_.kind == Kind::Green;
    const auto p = spec_.center;
    c.oval(p.x, p.y, 410, 410, 0x090d11ff);
    c.oval(p.x, p.y, 405, 405, dress ? 0xe7b990ff : green ? 0x195e45ff : 0x6f787bff);
    c.oval(p.x, p.y, 350, 350, dress ? 0xeee7d8ff : green ? 0x074331ff : 0x171d22ff);
    for (int i = 0; i < 60; ++i) {
        const double a = i * kTau / 60;
        const double r = i % 5 ? 330 : 307;
        c.line(p.x + std::sin(a) * r, p.y - std::cos(a) * r, p.x + std::sin(a) * 340, p.y - std::cos(a) * 340,
               dress ? 0x956d50ff : 0xe7e0caff, i % 5 ? 2 : 8);
    }
    if (green) {
        c.gradientBox({686, 480, 58, 46}, 3, 0xfdfdf8ff, 0xd5d9d3ff);
    }
}

void Renderer::prepare(Canvas& c, const Options& value, double scale) {
    const double dpi = c.devicePixelRatio();
    const auto pixels = uint32_t(std::clamp(std::ceil(kExtent * scale * dpi - 1e-6), 1., 8192.));
    if (!assets_.empty()) {
        // Let WIC prefilter the original directly to the displayed pixel size.
        // A larger decode bucket followed by bilinear shrinking softens tiny dial
        // indices and engraved case details for no benefit at the resting size.
        const uint32_t edge = std::clamp(pixels, 64u, 1536u);
        if (edge != edge_) {
            // Keep the previous decoded face alive until the new DPI level is ready.
            images_.set("face", assets_ / "face.png", 0, edge);
            edge_ = edge;
        }
    }
    poll();
    const bool cache = uint64_t(pixels) * pixels <= 2 * 1024 * 1024;
    if (owner_ == c.target.Get() && pixels_ == pixels && dpi_ == dpi && cachedRevision_ == revision_ &&
        background_ == value.background && (!cache || backdrop_.bitmap())) {
        return;
    }
    if (cache) {
        c.renderLayer(backdrop_, {pixels, pixels, dpi}, [&](auto& layer) {
            layer.scale(scale, scale);
            base(layer, value);
        });
    }
    if (spec_.kind == Kind::Skeleton) {
        for (size_t i = 0; i < kGears.size(); ++i) {
            const double span = kGears[i].radius * 2 + 8;
            const auto edge = uint32_t(std::clamp(std::ceil(span * scale * dpi), 8., 1024.));
            c.renderLayer(wheels_[i], {edge, edge, dpi}, [&](auto& layer) {
                layer.scale(double(edge) / dpi / span, double(edge) / dpi / span);
                layer.translate(span / 2, span / 2);
                cog(layer, kGears[i], i, brass_);
            });
        }
    }
    owner_ = c.target.Get();
    pixels_ = pixels;
    dpi_ = dpi;
    cachedRevision_ = revision_;
    background_ = value.background;
}

void Renderer::mechanism(Canvas& c, const State& state) {
    for (size_t i = 0; i < kGears.size(); ++i) {
        const auto& gear = kGears[i];
        const double half = gear.radius + 4;
        c.saved([&] {
            c.translate(gear.center.x + 2, gear.center.y + 3);
            c.oval(0, 0, gear.radius, gear.radius, 0x00000070);
            c.translate(-2, -3);
            c.rotate(gearAngle(state.mechanismSeconds, gear.teeth, gear.reverse) + (i % 2 ? 180. / gear.teeth : 0));
            c.target->DrawBitmap(wheels_[i].bitmap(), D2D1::RectF(float(-half), float(-half), float(half), float(half)),
                                 1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        });
    }
    // Keep the balance assembly on its fixed arbor while the wheel and inner
    // spring oscillate. Its drawing coordinates are independent of the dial.
    c.saved([&] {
        c.translate(510, 670);
        c.scale(.7, .7);
        c.translate(-500, -680);
        c.saved([&] {
            c.translate(500, 680);
            c.rotate(state.balance);
            c.oval(0, 0, 118, 118, 0x573d21ff, 15);
            c.oval(-1, -1, 118, 118, 0xe0bd74ff, 6);
            c.oval(0, 0, 104, 104, 0xfbe3a490, 1.8);
            for (int i = 0; i < 4; ++i) {
                const double a = i * kTau / 4;
                c.line(std::cos(a) * 24, std::sin(a) * 24, std::cos(a) * 112, std::sin(a) * 112, 0x9e793cff, 18);
                c.line(std::cos(a) * 25, std::sin(a) * 25 - 1, std::cos(a) * 111, std::sin(a) * 111 - 1, 0xf8d995ff, 4);
                screw(c, {std::cos(a) * 103, std::sin(a) * 103}, 4);
            }
        });
        auto spring = c.path();
        for (int i = 0; i <= 210; ++i) {
            const double winding = state.balance * kTau / 360 * (1 - double(i) / 210);
            const double angle = i * kTau / 30 + winding, radius = 15 + i * .34;
            const Point point{500 + std::cos(angle) * radius, 680 + std::sin(angle) * radius};
            if (i) {
                spring.lineTo(point);
            } else {
                spring.moveTo(point, false);
            }
        }
        c.stroke(spring, 0xbac0c5e0, {.width = 1.6});
        c.line(586.4, 680, 615, 680, 0xbac0c5e0, 1.8);
        screw(c, {615, 680}, 5, false);
        c.oval(500, 680, 24, 24, 0xe1b57fff);
        c.oval(500, 680, 16, 16, 0x4a122eff);
        c.oval(496, 676, 6, 6, 0xf498bba0);
        c.saved([&] {
            c.translate(626, 638);
            c.rotate(state.balance * .55);
            c.line(-7, -18, -22, 14, 0x989c94ff, 9);
            c.line(-22, 14, -12, 26, 0xd7d9ccff, 8);
            c.line(-22, 14, -40, 17, 0xd7d9ccff, 8);
            c.oval(0, 0, 6, 6, 0x3f0b22ff);
        });
    });
}

void Renderer::lunar(Canvas& c, const State& state) {
    auto aperture = c.path();
    aperture.moveTo({379, 704})
        .cubicTo({380, 533}, {612, 533}, {613, 704})
        .cubicTo({600, 642}, {553, 637}, {523, 676})
        .quadraticTo({497, 661}, {471, 676})
        .cubicTo({442, 638}, {395, 641}, {379, 704})
        .close();
    c.masked(aperture, [&] {
        if (!face_) {
            c.gradientBox({375, 560, 245, 150}, 0, 0x061d38ff, 0x020c1eff);
        }
        for (auto point : {Point{424, 636}, Point{447, 609}, Point{462, 641}, Point{549, 612}, Point{574, 643}}) {
            star(c, point, point.x == 447 ? 4 : 2.8);
        }
        constexpr Point center{496, 624};
        constexpr double radius = 35;
        c.oval(center.x, center.y, radius, radius, 0x07192fff);
        const double phase = state.lunarPhase * kTau, cosine = std::cos(phase);
        const bool waxing = state.lunarPhase < .5;
        auto lit = c.path();
        for (int i = 0; i <= 60; ++i) {
            const double y = -radius + 2 * radius * i / 60, x = std::sqrt(std::max(0., radius * radius - y * y));
            const Point point{center.x + (waxing ? x : -x), center.y + y};
            if (i) {
                lit.lineTo(point);
            } else {
                lit.moveTo(point);
            }
        }
        for (int i = 60; i >= 0; --i) {
            const double y = -radius + 2 * radius * i / 60, x = std::sqrt(std::max(0., radius * radius - y * y));
            lit.lineTo({center.x + (waxing ? cosine * x : -cosine * x), center.y + y});
        }
        lit.close();
        c.masked(lit, [&] {
            c.gradientBox({center.x - radius, center.y - radius, radius * 2, radius * 2}, 0, 0xf9d99bff, 0xc18b4cff);
            for (const auto point : {Point{488, 609}, Point{509, 635}, Point{478, 636}, Point{510, 609}}) {
                c.oval(point.x, point.y, 4, 3.5, 0x875a3140, 1.2);
                c.oval(point.x + 1, point.y + 1, 3, 2.5, 0xffedbd60, .8);
            }
        });
    });
}

void Renderer::hands(Canvas& c, const State& state, const Options& value) {
    const bool dress = spec_.kind == Kind::Dress, green = spec_.kind == Kind::Green;
    const auto metalTop = dress ? 0xf5d4b0ff : green ? 0xf1f4efff : 0xf4d89cff;
    const auto metalBottom = dress ? 0xa06843ff : green ? 0x6e7b7eff : 0x8c602dff;
    const auto outline = dress ? 0x8e6044ff : 0x121b20ff;
    const auto blade = [&](double angle, double length, double width) {
        c.saved([&] {
            c.translate(spec_.center.x, spec_.center.y);
            c.rotate(angle);
            auto path = c.path();
            if (dress) {
                path.moveTo({0, -length})
                    .cubicTo({width, -length * .7}, {width * 1.7, -length * .5}, {width * .55, -length * .27})
                    .lineTo({width * .45, 25})
                    .lineTo({-width * .45, 25})
                    .lineTo({-width * .55, -length * .27})
                    .cubicTo({-width * 1.7, -length * .5}, {-width, -length * .7}, {0, -length})
                    .close();
            } else {
                path.moveTo({0, -length})
                    .lineTo({width, -length + 29})
                    .lineTo({width * .55, 34})
                    .lineTo({-width * .55, 34})
                    .lineTo({-width, -length + 29})
                    .close();
            }
            c.saved([&] {
                c.translate(3, 5);
                c.fill(path, 0x00000065);
            });
            c.masked(path, [&] {
                c.gradientBox({-width * 2, -length, width * 4, length + 35}, 0, metalTop, metalBottom);
                c.box(0, -length, width * 2, length + 35, 0, 0x4c35192d);
            });
            c.stroke(path, outline, {.width = 1.9});
            c.line(-width * .15, -length + 15, -width * .15, 16, dress ? 0xffead3c0 : 0xffffffb0, 1.4);
            if (!dress) {
                c.box(-width * .32, -length + 43, width * .64, length - 75, width * .18, 0xece8d0ff);
            }
        });
    };
    blade(state.hour, dress ? 220 : 205, dress ? 13 : 20);
    blade(state.minute, green ? 270 : 300, dress ? 9 : 13);
    if (value.seconds) {
        c.saved([&] {
            c.translate(spec_.center.x, spec_.center.y);
            c.rotate(state.second);
            const double length = green ? 280 : 315;
            c.line(2, 55, 2, -length, 0x00000060, 3.5);
            c.line(0, 55, 0, -length, dress ? 0xa8734fff : green ? 0xe4e6dfff : 0xc7985cff, 2.7);
            c.line(-.8, 53, -.8, -length + 2, dress ? 0xffdfbcff : 0xfff3d9ff, 1);
            if (green) {
                c.oval(0, -223, 11, 11, 0xc0c8c2ff);
                c.oval(0, -223, 8, 8, 0xf0ecd8ff);
            }
            c.oval(0, 46, 7, 9, dress ? 0xe5b58aff : 0xc7c6b4ff);
        });
    }
    const auto p = spec_.center;
    c.oval(p.x + 1, p.y + 3, 24, 24, 0x00000080);
    c.oval(p.x, p.y, 22, 22, metalBottom);
    c.oval(p.x - 1, p.y - 1, 18, 18, metalTop);
    c.oval(p.x, p.y, 10, 10, metalBottom);
    c.oval(p.x - 2, p.y - 2, 6, 6, metalTop);
}

void Renderer::draw(Canvas& c, const State& state, const Options& value, double scale) {
    prepare(c, value, scale);
    const auto paint = [&] {
        const auto front = [&] {
            if (uint64_t(pixels_) * pixels_ <= 2 * 1024 * 1024) {
                c.target->DrawBitmap(backdrop_.bitmap(), D2D1::RectF(0, 0, float(kExtent), float(kExtent)), 1,
                                     D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            } else {
                base(c, value);
            }
        };
        if (spec_.kind == Kind::Skeleton) {
            if (!face_) {
                front();
            }
            auto opening = c.path();
            circle(opening, spec_.center, 290);
            c.masked(opening, [&] {
                c.gradientBox({200, 200, 600, 600}, 0, 0x252b2dff, 0x070c10ff);
                for (double radius : {125., 185., 245., 278.}) {
                    c.oval(spec_.center.x, spec_.center.y, radius, radius, 0x87918b20, 1);
                }
                if (value.detail) {
                    mechanism(c, state);
                }
            });
            if (face_) {
                front();
            } else {
                bridges(c);
            }
        } else {
            front();
            if (spec_.kind == Kind::Dress && value.detail) {
                lunar(c, state);
            } else if (spec_.kind == Kind::Green && value.detail) {
                cybercanvas::TextStyle text;
                text.family = L"Bahnschrift";
                text.size = 42;
                text.bold = true;
                text.align = cybercanvas::TextAlign::Center;
                text.vertical = cybercanvas::VerticalAlign::Center;
                c.text(std::to_wstring(state.day), text, {686, 480, 58, 46}, 0x17201eff);
            }
        }
        hands(c, state, value);
    };
    if (value.background) {
        paint();
    } else {
        auto clip = c.path();
        circle(clip, spec_.center, spec_.radius);
        c.masked(clip, paint);
    }
}
}  // namespace precisionclock
