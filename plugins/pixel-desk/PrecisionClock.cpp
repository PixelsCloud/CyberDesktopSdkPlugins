#include "plugins/pixel-desk/PrecisionClock.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <tuple>

#include "plugins/mechanical-pixels/Pixels.h"
#include "plugins/pixel-desk/PixelImages.h"
#include "plugins/pixel-desk/PixelLayout.h"
#include "plugins/pixel-desk/PixelParts.h"

namespace pixeldesk {
using cybercanvas::Canvas;
using cybercanvas::Point;
using Microsoft::WRL::ComPtr;

const PrecisionSpec& precisionSpec(std::string_view id) {
    for (const auto& spec : kPrecisionCatalog) {
        if (id == spec.id) {
            return spec;
        }
    }
    throw std::invalid_argument("Unknown Pixel Desk precision clock");
}

cdsdk::Settings precisionDefaults(precisionclock::Kind kind) {
    return {{{"motion", CD_SETTING_CHOICE, 0, 0, "beat", 0, 0, {"sweep", "beat", "tick"}},
             {"seconds", CD_SETTING_TOGGLE, 1},
             {"background", CD_SETTING_TOGGLE, 1},
             {"detail", CD_SETTING_TOGGLE, 1},
             {"animation", CD_SETTING_TOGGLE, 1},
             {"reduced_motion", CD_SETTING_TOGGLE, 0},
             {"hand_color", CD_SETTING_COLOR, kCream},
             {"seconds_color", CD_SETTING_COLOR, kind == precisionclock::Kind::Skeleton ? kMint : kClay}}};
}

PrecisionView precisionView(CdRect safe, const PrecisionSpec& spec) {
    const auto bounds = fitBody(safe.width, safe.height, spec.visible.width, spec.visible.height);
    const double scale = bounds.width / spec.visible.width;
    return {{safe.x + bounds.x - spec.visible.x * scale, safe.y + bounds.y - spec.visible.y * scale}, scale};
}

namespace {
struct Gear {
    Point center;
    double radius;
    unsigned teeth;
    bool reverse;
};

// Keep the existing precision-clock train, pitches and arbor positions. The
// pixel skin changes only its artwork; gearAngle remains the original drive.
constexpr std::array<Gear, 6> kGears{{{{458, 339}, 90, 45, false},
                                      {{367, 449}, 59, 30, true},
                                      {{391, 588}, 88, 44, false},
                                      {{566, 592}, 94, 47, true},
                                      {{668, 490}, 56, 28, false},
                                      {{612, 373}, 79, 40, true}}};
constexpr double kMechanismScale = 1.2;

void cog(Canvas& canvas, const Gear& gear, unsigned index) {
    const double radius = gear.radius;
    auto outline = canvas.path();
    for (unsigned vertex = 0; vertex < gear.teeth * 4; ++vertex) {
        const double angle = (double(vertex) / 4 - .48) * 2 * std::numbers::pi / gear.teeth,
                     r = radius * (vertex % 4 == 0 || vertex % 4 == 3 ? .88 : 1.);
        const Point p{std::round(std::cos(angle) * r / 3) * 3, std::round(std::sin(angle) * r / 3) * 3};
        if (vertex) {
            outline.lineTo(p);
        } else {
            outline.moveTo(p);
        }
    }
    outline.close();
    canvas.fill(outline, index % 2 ? kAmber : kCream);
    canvas.stroke(outline, 0x80593fff, {.width = 3});
    if (index) {
        for (unsigned hole = 0; hole < 6; ++hole) {
            const double angle = hole * std::numbers::pi / 3;
            pixelDisc(canvas, std::cos(angle) * radius * .53, std::sin(angle) * radius * .53, radius * .21, kInk);
        }
    } else {
        pixelDisc(canvas, 0, 0, radius * .74, 0x354a56ff);
        for (double r : {.28, .45, .62}) {
            canvas.oval(0, 0, radius * r, radius * r, kMint, 2);
        }
        canvas.line(-radius * .45, -radius * .4, radius * .36, radius * .48, kCream, 5);
    }
    pixelDisc(canvas, 0, 0, radius * .18, kInk);
    pixelDisc(canvas, -2, -2, radius * .12, kAmber);
}

void supports(Canvas& canvas) {
    const auto bar = [&](Point a, Point b, double width) {
        canvas.line(a.x + 2, a.y + 4, b.x + 2, b.y + 4, 0x080f18dd, width + 5);
        canvas.line(a.x, a.y, b.x, b.y, 0x58727aff, width);
        canvas.line(a.x - 2, a.y - 2, b.x - 2, b.y - 2, 0xc9d5c5ff, width * .3);
    };
    bar({310, 339}, kGears[0].center, 18);
    bar(kGears[0].center, {548, 281}, 18);
    bar({282, 510}, kGears[1].center, 20);
    bar(kGears[1].center, kGears[2].center, 18);
    bar(kGears[2].center, {352, 689}, 18);
    bar({693, 369}, kGears[5].center, 20);
    bar(kGears[5].center, kGears[4].center, 18);
    bar(kGears[4].center, kGears[3].center, 18);
    bar(kGears[3].center, {678, 651}, 18);
    bar({416, 734}, {560, 641}, 17);
    bar({568, 738}, {560, 641}, 17);
    for (const auto& gear : kGears) {
        pixelBolt(canvas, gear.center.x, gear.center.y, 10);
    }
    for (Point point : {Point{310, 339}, Point{548, 281}, Point{282, 510}, Point{352, 689}, Point{693, 369},
                        Point{678, 651}, Point{416, 734}, Point{568, 738}}) {
        pixelBolt(canvas, point.x, point.y, 7);
    }
}

void blade(Canvas& canvas, double length, double width, uint32_t color, bool leaf, bool previous = false) {
    auto path = canvas.path();
    if (!leaf) {
        path.moveTo({0, -length})
            .lineTo({width, -length + 28})
            .lineTo({width * .55, 30})
            .lineTo({-width * .55, 30})
            .lineTo({-width, -length + 28})
            .close();
        canvas.fill(path, color);
        if (previous) {
            canvas.stroke(path, kInk, {.width = 4});
            canvas.line(-width * .12, -length + 20, -width * .12, 15, 0xfff9e0ff, 3);
            return;
        }
        canvas.stroke(path, kInk, {.width = 10});
        auto stem = canvas.path();
        stem.moveTo({-width * .55, 30})
            .lineTo({-width * .48, -length * .32})
            .lineTo({width * .48, -length * .32})
            .lineTo({width * .55, 30})
            .close();
        canvas.fill(stem, kInk);
        canvas.line(-width * .12, -length + 20, -width * .12, -length * .4, 0xfff9e0ff, 3);
        return;
    }
    path.moveTo({0, -length})
        .lineTo({width, -length * .72})
        .lineTo({width * 1.5, -length * .50})
        .lineTo({width * .65, -length * .20})
        .lineTo({width * .45, 30})
        .lineTo({-width * .45, 30})
        .lineTo({-width * .65, -length * .20})
        .lineTo({-width * 1.5, -length * .50})
        .lineTo({-width, -length * .72})
        .close();
    canvas.fill(path, color);
    canvas.stroke(path, kInk, {.width = 4});
    canvas.line(-width * .25, -length + 18, -width * .25, 10, 0xfff4dabb, 2);
}

void pixelNumber(Canvas& canvas, const std::string& value, cybercanvas::Rect area, uint32_t color) {
    const unsigned columns = unsigned(value.size()) * 6 - 1;
    const double unit = std::min(area.width / columns, area.height / 7),
                 left = area.x + (area.width - unit * columns) / 2, top = area.y + (area.height - unit * 7) / 2;
    cybercanvas::Path glyph(canvas.factory.Get(), cybercanvas::FillRule::Winding);
    for (unsigned row = 0; row < 7; ++row) {
        for (unsigned column = 0; column < columns; ++column) {
            if (!mechanicalpixels::clockCell(value, column + 1, row + 1)) {
                continue;
            }
            const double x = left + column * unit, y = top + row * unit;
            glyph.moveTo({x, y}).lineTo({x + unit, y}).lineTo({x + unit, y + unit}).lineTo({x, y + unit}).close();
        }
    }
    canvas.fill(glyph, color);
}

void greenDetails(Canvas& canvas, const PrecisionSpec& spec, bool bezel) {
    if (bezel) {
        const std::array<double, 5> rotation{45, -45, 0, 45, -45};
        for (unsigned i = 0; i < 5; ++i) {
            const double angle = (i + 1) * std::numbers::pi / 3;
            canvas.saved([&] {
                canvas.translate(spec.center.x + spec.bezelRadius * std::sin(angle),
                                 spec.center.y - spec.bezelRadius * std::cos(angle));
                canvas.rotate(rotation[i]);
                pixelNumber(canvas, std::to_string((i + 1) * 10), {-56, -40, 112, 80}, kCream);
            });
        }
    }
    canvas.box(spec.date.x - 6, spec.date.y - 6, spec.date.width + 12, spec.date.height + 12, 0, kCream);
}

void moon(Canvas& canvas, const PrecisionSpec& spec, double phase) {
    auto aperture = canvas.path();
    const auto center = spec.moonCenter;
    const double width = spec.moonRadius * 3.4, baseline = center.y + spec.moonRadius * 1.2;
    aperture.moveTo({center.x - width, baseline})
        .arcTo({center.x + width, baseline}, {width, spec.moonRadius * 2.7})
        .close();
    canvas.masked(aperture, [&] {
        for (Point point : {Point{520, 775}, Point{560, 719}, Point{690, 730}, Point{740, 797}, Point{514, 825}}) {
            canvas.box(point.x - 3, point.y - 7, 6, 14, 0, kAmber);
            canvas.box(point.x - 7, point.y - 3, 14, 6, 0, kCream);
        }
        const double radius = spec.moonRadius, cosine = std::cos(phase * std::numbers::pi * 2);
        const bool waxing = phase < .5;
        pixelDisc(canvas, center.x, center.y, radius, 0x314454ff);
        // Only the rendered sprite is quantized into pixel clusters. Its phase is
        // the unmodified sxtwl ephemeris from the existing precision-clock model.
        constexpr unsigned rows = 25;
        const double unit = radius * 2 / rows;
        struct Span {
            double left, right;
            unsigned row;
        };
        std::vector<Span> spans;
        for (unsigned row = 0; row < rows; ++row) {
            const double y = -radius + (row + .5) * unit, x = std::sqrt(std::max(0., radius * radius - y * y));
            double left = waxing ? cosine * x : -x, right = waxing ? x : -cosine * x;
            left = std::round(left / unit) * unit;
            right = std::round(right / unit) * unit;
            if (right > left) {
                spans.push_back({center.x + left, center.x + right, row});
            }
        }
        // One silhouette per connected run avoids seams between antialiased rows.
        auto lit = canvas.path();
        for (size_t start = 0; start < spans.size();) {
            size_t end = start;
            while (end + 1 < spans.size() && spans[end + 1].row == spans[end].row + 1) {
                ++end;
            }
            const double top = center.y - radius + spans[start].row * unit;
            lit.moveTo({spans[start].left, top}).lineTo({spans[start].right, top});
            for (size_t index = start; index <= end; ++index) {
                const double bottom = center.y - radius + (spans[index].row + 1) * unit;
                lit.lineTo({spans[index].right, bottom});
                if (index < end) {
                    lit.lineTo({spans[index + 1].right, bottom});
                }
            }
            lit.lineTo({spans[end].left, center.y - radius + (spans[end].row + 1) * unit});
            for (size_t index = end + 1; index-- > start;) {
                const double y = center.y - radius + spans[index].row * unit;
                lit.lineTo({spans[index].left, y});
                if (index > start) {
                    lit.lineTo({spans[index - 1].left, y});
                }
            }
            lit.close();
            start = end + 1;
        }
        if (!spans.empty()) {
            canvas.fill(lit, kAmber);
            canvas.masked(lit, [&] {
                for (Point offset : {Point{-19, -22}, Point{23, 14}, Point{-26, 24}}) {
                    pixelDisc(canvas, center.x + offset.x, center.y + offset.y, 9, 0xba8d4c88);
                    canvas.box(center.x + offset.x - 4, center.y + offset.y - 3, 6, 4, 0, 0xffe7b690);
                }
            });
        }
    });
}
}  // namespace

struct PrecisionRenderer::Impl {
    using Key = std::tuple<double, bool, const cybercanvas::ImageData*>;
    const PrecisionSpec& spec;
    cdsdk::Images images;
    std::filesystem::path path;
    std::shared_ptr<const cybercanvas::ImageData> body;
    std::optional<Key> key;
    std::optional<std::tuple<double, uint32_t, uint32_t>> handKey;
    ComPtr<ID2D1RenderTarget> owner;
    cybercanvas::SurfaceImage face;
    std::array<cybercanvas::SurfaceImage, 3> hands;
    std::array<cybercanvas::SurfaceImage, kGears.size()> gears;
    cybercanvas::SurfaceImage bridges, balance;
    std::optional<double> gearDensity;
    uint64_t builds = 0;

    Impl(const CdCreateContext& context, const PrecisionSpec& value) : spec(value) {
        images.bind(context);
        path = std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(context.plugin_directory))) /
               "assets" / "body.png";
    }

    void prepare(Canvas& canvas, const cdsdk::Settings& settings, double density) {
        const auto edge = uint32_t(
            std::clamp(std::ceil(std::max(spec.source.width, spec.source.height) * density / 64) * 64, 64., 4096.));
        pixeldesk::requestPixelImage(images, "body", path, 1, edge, canvas.devicePixelRatio());
        images.poll();
        body = images.get("body").image;
        if (owner != canvas.target) {
            handKey.reset();
            gearDensity.reset();
        }
        const Key next{density, settings.at("background").integer != 0, body.get()};
        if (owner != canvas.target || key != next) {
            cybercanvas::OffscreenSurface surface;
            canvas.renderLayer(surface,
                               {uint32_t(std::ceil(spec.source.width * density)),
                                uint32_t(std::ceil(spec.source.height * density)), 1},
                               [&](Canvas& layer) {
                                   layer.scale(density, density);
                                   const auto paint = [&] {
                                       if (body) {
                                           layer.image(*body, {0, 0, spec.source.width, spec.source.height},
                                                       {.fit = cybercanvas::ImageFit::Stretch,
                                                        .clip = false,
                                                        .sampling = cybercanvas::ImageSampling::Nearest});
                                       } else {
                                           pixelDisc(layer, spec.center.x, spec.center.y, spec.radius, kCream);
                                       }
                                       if (spec.kind == precisionclock::Kind::Green && spec.bezelRadius > 0) {
                                           greenDetails(layer, spec, settings.at("background").integer != 0);
                                       }
                                   };
                                   if (settings.at("background").integer) {
                                       paint();
                                   } else {
                                       auto mask = layer.path();
                                       const auto center = spec.center;
                                       mask.moveTo({center.x + spec.radius, center.y})
                                           .arcTo({center.x - spec.radius, center.y}, {spec.radius, spec.radius})
                                           .arcTo({center.x + spec.radius, center.y}, {spec.radius, spec.radius})
                                           .close();
                                       layer.masked(mask, paint);
                                   }
                               });
            face = surface.snapshot();
            owner = canvas.target;
            key = next;
            ++builds;
        }
        if (spec.kind == precisionclock::Kind::Skeleton && gearDensity != density) {
            const double pixels = density * kMechanismScale;
            for (unsigned i = 0; i < gears.size(); ++i) {
                const double span = (kGears[i].radius + 5) * 2;
                cybercanvas::OffscreenSurface surface;
                canvas.renderLayer(surface, {uint32_t(std::ceil(span * pixels)), uint32_t(std::ceil(span * pixels)), 1},
                                   [&](Canvas& layer) {
                                       layer.scale(pixels, pixels);
                                       layer.translate(span / 2, span / 2);
                                       cog(layer, kGears[i], i);
                                   });
                gears[i] = surface.snapshot();
            }
            cybercanvas::OffscreenSurface overlay, wheel;
            canvas.renderLayer(overlay, {uint32_t(std::ceil(1024 * pixels)), uint32_t(std::ceil(1024 * pixels)), 1},
                               [&](Canvas& layer) {
                                   layer.scale(pixels, pixels);
                                   supports(layer);
                               });
            bridges = overlay.snapshot();
            canvas.renderLayer(wheel, {uint32_t(std::ceil(180 * pixels)), uint32_t(std::ceil(180 * pixels)), 1},
                               [&](Canvas& layer) {
                                   layer.scale(pixels, pixels);
                                   layer.translate(90, 90);
                                   pixelDisc(layer, 0, 0, 82, kAmber);
                                   pixelDisc(layer, 0, 0, 73, kInk);
                                   for (unsigned i = 0; i < 4; ++i) {
                                       layer.saved([&] {
                                           layer.rotate(i * 90.);
                                           layer.box(-5, -78, 10, 68, 0, kMint);
                                           pixelBolt(layer, 0, -76, 4);
                                       });
                                   }
                               });
            balance = wheel.snapshot();
            gearDensity = density;
        }
        const auto color = uint32_t(settings.at("hand_color").integer),
                   seconds = uint32_t(settings.at("seconds_color").integer);
        const auto nextHands = std::make_tuple(density, color, seconds);
        if (handKey == nextHands) {
            return;
        }
        for (unsigned i = 0; i < hands.size(); ++i) {
            const double length = spec.radius * (spec.kind == precisionclock::Kind::Skeleton ? (i == 0   ? .52
                                                                                                : i == 1 ? .76
                                                                                                         : .82)
                                                                                             : (i == 0   ? .66
                                                                                                : i == 1 ? .96
                                                                                                         : .97)),
                         width = i == 0   ? (spec.kind == precisionclock::Kind::Skeleton ? 50 : 72)
                                 : i == 1 ? (spec.kind == precisionclock::Kind::Skeleton ? 40 : 60)
                                 : spec.kind == precisionclock::Kind::Green ? 28
                                                                            : 18;
            cybercanvas::OffscreenSurface hand;
            canvas.renderLayer(
                hand, {uint32_t(std::ceil(width * density)), uint32_t(std::ceil((length + 45) * density)), 1},
                [&](Canvas& layer) {
                    layer.scale(density, density);
                    layer.translate(width / 2, length + 4);
                    if (i == 2) {
                        layer.line(0, 34, 0, -length, seconds, spec.kind == precisionclock::Kind::Skeleton ? 4 : 7);
                        pixelDisc(layer, 0, 26, 7, seconds);
                        if (spec.kind == precisionclock::Kind::Green) {
                            pixelDisc(layer, 0, -length * .8, 11, seconds);
                            pixelDisc(layer, 0, -length * .8, 6, kCream);
                        }
                    } else {
                        const bool previous = spec.kind == precisionclock::Kind::Skeleton;
                        blade(layer, length, previous ? (i == 0 ? 14 : 10) : (i == 0 ? 22 : 16), color, false,
                              previous);
                    }
                });
            hands[i] = hand.snapshot();
        }
        handKey = nextHands;
    }

    void mechanism(Canvas& canvas, const precisionclock::State& state) {
        auto opening = canvas.path();
        constexpr double radius = 350;
        opening.moveTo({spec.center.x + radius, spec.center.y})
            .arcTo({spec.center.x - radius, spec.center.y}, {radius, radius})
            .arcTo({spec.center.x + radius, spec.center.y}, {radius, radius})
            .close();
        canvas.masked(opening, [&] {
            canvas.saved([&] {
                canvas.translate(spec.center.x, spec.center.y);
                canvas.scale(kMechanismScale, kMechanismScale);
                canvas.translate(-497, -502);
                for (unsigned i = 0; i < gears.size(); ++i) {
                    const auto& gear = kGears[i];
                    const float half = float(gear.radius + 5);
                    canvas.saved([&] {
                        canvas.translate(gear.center.x, gear.center.y);
                        canvas.rotate(precisionclock::gearAngle(state.mechanismSeconds, gear.teeth, gear.reverse) +
                                      (i % 2 ? 180. / gear.teeth : 0));
                        canvas.target->DrawBitmap(gears[i].Get(), D2D1::RectF(-half, -half, half, half), 1,
                                                  D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
                    });
                }
                canvas.saved([&] {
                    canvas.translate(510, 670);
                    canvas.rotate(state.balance);
                    canvas.target->DrawBitmap(balance.Get(), D2D1::RectF(-90, -90, 90, 90), 1,
                                              D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
                });
                auto spring = canvas.path();
                for (unsigned i = 0; i <= 150; ++i) {
                    const double angle = i * std::numbers::pi / 15 +
                                         state.balance * std::numbers::pi / 180 * (1 - i / 150.),
                                 r = 12 + i * .36;
                    const Point p{510 + std::round(std::cos(angle) * r / 2) * 2,
                                  670 + std::round(std::sin(angle) * r / 2) * 2};
                    if (i) {
                        spring.lineTo(p);
                    } else {
                        spring.moveTo(p);
                    }
                }
                canvas.stroke(spring, 0xa6c2b8ff, {.width = 2});
                canvas.line(576, 670, 590, 670, kCream, 3);
                pixelBolt(canvas, 510, 670, 10);
                pixelBolt(canvas, 590, 670, 5);
                canvas.target->DrawBitmap(bridges.Get(), D2D1::RectF(0, 0, 1024, 1024), 1,
                                          D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
            });
        });
    }
};

PrecisionRenderer::PrecisionRenderer(const CdCreateContext& context, const PrecisionSpec& spec)
    : impl_(std::make_unique<Impl>(context, spec)) {}

PrecisionRenderer::~PrecisionRenderer() = default;

void PrecisionRenderer::onChange(std::function<void()> callback) {
    impl_->images.onChange(std::move(callback));
}

void PrecisionRenderer::pause(bool paused) {
    impl_->images.pause(paused);
}

bool PrecisionRenderer::ready() const {
    return impl_->body && !impl_->images.pending() && impl_->images.get("body").image == impl_->body;
}

bool PrecisionRenderer::pending() const {
    return impl_->images.pending();
}

uint64_t PrecisionRenderer::cacheBuilds() const {
    return impl_->builds;
}

cdsdk::ImageSlotStats PrecisionRenderer::imageStats() const {
    return impl_->images.stats();
}

void PrecisionRenderer::draw(Canvas& canvas, const precisionclock::State& state, const cdsdk::Settings& settings,
                             double scale, double dpi) {
    impl_->prepare(canvas, settings, scale * dpi);
    const auto& spec = impl_->spec;
    canvas.target->DrawBitmap(impl_->face.Get(), D2D1::RectF(0, 0, float(spec.source.width), float(spec.source.height)),
                              1, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    if (spec.kind == precisionclock::Kind::Skeleton && settings.at("detail").integer) {
        impl_->mechanism(canvas, state);
    }
    if (spec.kind == precisionclock::Kind::Dress && settings.at("detail").integer) {
        moon(canvas, spec, state.lunarPhase);
    }
    if (spec.kind == precisionclock::Kind::Green && settings.at("detail").integer) {
        pixelNumber(canvas, std::to_string(state.day), spec.date, kInk);
    }
    const std::array<double, 3> angles{state.hour, state.minute, state.second};
    for (unsigned i = 0; i < angles.size(); ++i) {
        if (i == 2 && !settings.at("seconds").integer) {
            continue;
        }
        const double length = spec.radius * (spec.kind == precisionclock::Kind::Skeleton ? (i == 0   ? .52
                                                                                            : i == 1 ? .76
                                                                                                     : .82)
                                                                                         : (i == 0   ? .66
                                                                                            : i == 1 ? .96
                                                                                                     : .97)),
                     width = i == 0   ? (spec.kind == precisionclock::Kind::Skeleton ? 50 : 72)
                             : i == 1 ? (spec.kind == precisionclock::Kind::Skeleton ? 40 : 60)
                             : spec.kind == precisionclock::Kind::Green ? 28
                                                                        : 18;
        canvas.saved([&] {
            canvas.translate(spec.center.x, spec.center.y);
            canvas.rotate(angles[i]);
            canvas.target->DrawBitmap(impl_->hands[i].Get(),
                                      D2D1::RectF(float(-width / 2), float(-length - 4), float(width / 2), 41), 1,
                                      D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
        });
    }
    const bool previous = spec.kind == precisionclock::Kind::Skeleton;
    pixelDisc(canvas, spec.center.x, spec.center.y, previous ? 22 : 30, kInk);
    pixelDisc(canvas, spec.center.x, spec.center.y, previous ? 16 : 23, kAmber);
    pixelDisc(canvas, spec.center.x - 3, spec.center.y - 4, previous ? 7 : 10, kCream);
}

PrecisionInstance::PrecisionInstance(const CdCreateContext& context)
    : settings(precisionDefaults(precisionSpec(context.plugin_id).kind)),
      spec_(precisionSpec(context.plugin_id)),
      model_(spec_.kind),
      renderer_(context, spec_) {
    canvas_.bindGpu(context);
    environment_.bind(context);
    timer_.bind(context);
    redraw_.bind(context);
    renderer_.onChange([this] {
        invalidate();
    });
    model_.synchronize(environment_.snapshot(), precisionclock::options(settings));
    if (schedule(settings) != CD_OK) {
        throw std::runtime_error("Cannot schedule Pixel Desk precision clock");
    }
}

PrecisionInstance::~PrecisionInstance() {
    timer_.stop();
    renderer_.onChange({});
}

CdResult PrecisionInstance::schedule(const cdsdk::Settings& next) {
    if (hidden_) {
        timer_.stop();
        return CD_OK;
    }
    return timer_.replace(precisionclock::cadence(spec_.kind, precisionclock::options(next),
                                                  environment_.snapshot().unix_ms, renderer_.pending()),
                          wake, this);
}

void CD_CALL PrecisionInstance::wake(void* value, uint64_t) noexcept {
    auto& self = *static_cast<PrecisionInstance*>(value);
    try {
        if (self.model_.synchronize(self.environment_.snapshot(), precisionclock::options(self.settings))) {
            self.invalidate();
        }
        if (self.schedule(self.settings) != CD_OK) {
            self.timer_.stop();
        }
    } catch (...) {
        self.timer_.stop();
    }
}

CdResult PrecisionInstance::render(const CdFrame& frame) {
    if (!canvas_.valid(frame)) {
        return CD_ERROR_ARGUMENT;
    }
    redraw_.beginFrame();
    canvas_.render(frame, safe_, [&](auto& canvas, CdRect safe) {
        const auto view = precisionView(safe, spec_);
        canvas.saved([&] {
            canvas.translate(view.origin.x - safe.x, view.origin.y - safe.y);
            canvas.scale(view.scale, view.scale);
            renderer_.draw(canvas, model_.state(), settings, view.scale, frame.device_pixel_ratio);
        });
    });
    return schedule(settings);
}

CdResult PrecisionInstance::applySettings(const CdSettingValue* values, uint32_t count) {
    auto next = settings;
    const auto result = next.apply(values, count);
    if (result != CD_OK) {
        return result;
    }
    const auto scheduled = schedule(next);
    if (scheduled != CD_OK) {
        return scheduled;
    }
    settings = std::move(next);
    model_.synchronize(environment_.snapshot(), precisionclock::options(settings));
    invalidate();
    return CD_OK;
}

void PrecisionInstance::layoutChanged(const CdLayout& layout) {
    if (!std::isfinite(layout.logical_width) || !std::isfinite(layout.logical_height) || layout.logical_width <= 0 ||
        layout.logical_height <= 0) {
        return;
    }
    const auto safe = layout.safe_content;
    safe_ = std::isfinite(safe.x) && std::isfinite(safe.y) && std::isfinite(safe.width) && std::isfinite(safe.height) &&
                    safe.x >= 0 && safe.y >= 0 && safe.width > 0 && safe.height > 0 &&
                    safe.x + safe.width <= layout.logical_width && safe.y + safe.height <= layout.logical_height
                ? safe
                : CdRect{0, 0, layout.logical_width, layout.logical_height};
}

void PrecisionInstance::setPaused(bool paused) {
    hidden_ = paused;
    renderer_.pause(paused);
    redraw_.pause(paused);
    if (paused) {
        timer_.stop();
    } else {
        model_.synchronize(environment_.snapshot(), precisionclock::options(settings));
        schedule(settings);
        invalidate();
    }
}

void PrecisionInstance::invalidate() {
    redraw_.invalidate();
}

bool PrecisionInstance::save(const std::filesystem::path& path, const CdFrame& frame) {
    return canvas_.save(path.wstring(), frame);
}

bool PrecisionInstance::ready() const {
    return renderer_.ready();
}

const precisionclock::State& PrecisionInstance::state() const {
    return model_.state();
}
}  // namespace pixeldesk
