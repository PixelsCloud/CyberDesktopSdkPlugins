#include <CyberCanvas/Effects.h>
#include <CyberCanvas/Noise.h>
#include <CyberCanvas/Text.h>
#include <Windows.h>
#include <d2d1.h>
#include <dwrite_1.h>
#include <sdk/Canvas.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "plugins/koi-suite/FishArtwork.h"
#include "plugins/koi-suite/Pond.h"
#include "sdk/WindowsRendering.h"

namespace koiplugin {
using Microsoft::WRL::ComPtr;

namespace {
void require(HRESULT result) {
    if (FAILED(result)) {
        throw std::runtime_error("Pond drawing failed");
    }
}

D2D1_POINT_2F point(Vec value) {
    return D2D1::Point2F(float(value.x), float(value.y));
}

cybercanvas::Point pathPoint(Vec value) {
    return {value.x, value.y};
}

D2D1_COLOR_F color(uint32_t value, float opacity = 1) {
    return D2D1::ColorF(float(value >> 24) / 255, float((value >> 16) & 255) / 255, float((value >> 8) & 255) / 255,
                        float(value & 255) / 255 * opacity);
}

D2D1_COLOR_F blend(uint32_t day, uint32_t night, double amount, float opacity = 1) {
    const auto a = color(day, opacity), b = color(night, opacity);
    const float t = float(amount);
    return D2D1::ColorF(std::lerp(a.r, b.r, t), std::lerp(a.g, b.g, t), std::lerp(a.b, b.b, t), std::lerp(a.a, b.a, t));
}

double motionTime(const Scene& scene, const Appearance& appearance) {
    return appearance.motionTime >= 0 ? appearance.motionTime : scene.time() * kDefaultMotionSpeed;
}
}  // namespace

struct Renderer::Impl {
    cdsdk::Canvas canvas;
    cybercanvas::NoiseField waterNoise{{.seed = 741, .frequency = .025, .octaves = 2}};
    cybercanvas::NoiseField plantNoise{{.seed = 1907, .frequency = .02, .octaves = 2}};
    uint32_t width = 0, height = 0;
    double dpr = 0;
    ComPtr<ID2D1Factory>& factory = canvas.factory;
    cybercanvas::OffscreenSurface pondSurface;
    cybercanvas::EffectSurface blessingEffects;
    cybercanvas::EffectResult blessingImage;
    cybercanvas::Point blessingOrigin;
    D2D1_SIZE_F blessingBitmapSize{};
    double blessingNight = -1;
    std::wstring_view blessingLabel;
    float blessingSize = 0;
    cybercanvas::Size blessingArea;
    const wchar_t* blessingFamily = L"Microsoft YaHei";
    ComPtr<ID2D1RenderTarget> target;
    ComPtr<ID2D1SolidColorBrush> brush;

    void surface(const CdFrame& frame) {
        if (target == canvas.target && width == frame.width && height == frame.height &&
            dpr == frame.device_pixel_ratio) {
            return;
        }
        blessingImage = {};
        pondSurface.reset();
        // Local aliases may select the independent pond cache. The Canvas retains
        // ownership of its primary target throughout this renderer's frame.
        target = canvas.target;
        brush = canvas.brush;
        width = frame.width;
        height = frame.height;
        dpr = frame.device_pixel_ratio;
    }

    void ink(D2D1_COLOR_F value) {
        brush->SetColor(value);
    }

    void ellipse(Vec center, double rx, double ry, D2D1_COLOR_F value) {
        ink(value);
        target->FillEllipse(D2D1::Ellipse(point(center), float(rx), float(ry)), brush.Get());
    }

    void line(Vec a, Vec b, D2D1_COLOR_F value, double stroke = .5) {
        ink(value);
        target->DrawLine(point(a), point(b), brush.Get(), float(stroke));
    }

    ComPtr<ID2D1LinearGradientBrush> gradient(Vec a, Vec b, D2D1_COLOR_F start, D2D1_COLOR_F end,
                                              ID2D1RenderTarget* destination = nullptr) {
        destination = destination ? destination : target.Get();
        const D2D1_GRADIENT_STOP colors[]{{0, start}, {1, end}};
        return canvas.linearGradient(pathPoint(a), pathPoint(b), colors, destination);
    }

    void glow(Vec center, double rx, double ry, D2D1_COLOR_F tint) {
        const D2D1_GRADIENT_STOP colors[]{{0, tint}, {1, D2D1::ColorF(tint.r, tint.g, tint.b, 0)}};
        const auto fill = canvas.radialGradient(pathPoint(center), {rx, ry}, colors, {}, target.Get());
        target->FillEllipse(D2D1::Ellipse(point(center), float(rx), float(ry)), fill.Get());
    }

    // Processing curveVertex uses a Catmull-Rom spline. Convert the same points
    // to cubic Beziers rather than replacing the upstream fish with rigid sprites.
    ComPtr<ID2D1PathGeometry> curve(const std::vector<Vec>& points, bool closed = true) {
        if (points.size() < (closed ? 3u : 2u)) {
            throw std::invalid_argument("A pond curve needs enough points");
        }
        auto path = canvas.path();
        path.moveTo(pathPoint(points[0]), closed);
        const size_t count = points.size();
        for (size_t i = 0; i < (closed ? count : count - 1); ++i) {
            const auto p0 = points[i ? i - 1 : (closed ? count - 1 : 0)];
            const auto p1 = points[i];
            const auto p2 = points[(i + 1) % count];
            const auto p3 = points[(i + 2) < count ? i + 2 : (closed ? (i + 2) % count : count - 1)];
            path.cubicTo(pathPoint(p1 + (p2 - p0) * (1.0 / 6)), pathPoint(p2 - (p3 - p1) * (1.0 / 6)), pathPoint(p2));
        }
        if (closed) {
            path.close();
        }
        return ComPtr<ID2D1PathGeometry>(path.geometry(factory.Get()));
    }

    void water(double time, double night, float w, float h) {
        const auto bed =
            gradient({0, 0}, {w, h}, blend(0x326A61FF, 0x091C30FF, night), blend(0x76B6A0FF, 0x214555FF, night));
        target->FillRectangle(D2D1::RectF(0, 0, w, h), bed.Get());
        // Submerged river stones and soft botanical shadows give the water depth.
        std::mt19937 random(71);
        for (int i = 0; i < 85; ++i) {
            const auto sample = [&] {
                return std::uniform_real_distribution<double>(0, 1)(random);
            };
            const Vec position{w * sample(), h * sample()};
            const double size = (2 + 5 * sample()) * std::sqrt(w * h) / 260;
            ellipse(position + Vec{1, 2}, size * 1.3, size, blend(0x123F3823, 0x07162639, night));
            ellipse(position, size, size * .7, blend(0xB8C7A81C, 0x7A9CA913, night));
            ellipse(position + Vec{-size * .2, -size * .2}, size * .65, size * .35,
                    blend(0xDBDEC311, 0x9EB8C00E, night));
        }
        // Soft travelling light replaces the hard diagonal bands on the river bed.
        glow({w * (.30 + .13 * std::sin(time * .48)), h * (.27 + .13 * std::cos(time * .41))}, w * .66, h * .82,
             blend(0xE0F6C740, 0x96CFE035, night));
        glow({w * (.79 + .08 * std::cos(time * .39)), h * (.78 + .11 * std::sin(time * .55))}, w * .45, h * .65,
             blend(0xBDE8CD2A, 0x72ABCD24, night));
        const auto beam =
            gradient({w * .05, h * .15}, {w * .8, h}, blend(0xE5F3D71B, 0xA4D2E417, night), color(0xDBF5DA00));
        for (int i = 0; i < 3; ++i) {
            const double x = w * (.01 + .30 * i + .07 * std::sin(time * .45 + i));
            const auto path = curve({{x, -30},
                                     {x + w * .11, h * .25},
                                     {x + w * .23, h * .67},
                                     {x + w * .40, h + 30},
                                     {x + w * .32, h + 30},
                                     {x + w * .17, h * .67},
                                     {x + w * .04, h * .25}});
            target->FillGeometry(path.Get(), beam.Get());
        }
    }

    void fish(const Fish& value, double night, uint32_t koiColor) {
        drawFish(canvas, value, night, koiColor, target.Get(), brush.Get(), &pondSurface);
    }

    void cacheBlessing(double night, const DWRITE_TEXT_METRICS& metrics, const cybercanvas::TextStyle& style) {
        if (blessingImage.image && blessingNight == night) {
            return;
        }
        const auto rgba = [night](uint32_t day, uint32_t dark) {
            const auto value = blend(day, dark, night);
            return uint32_t(std::lround(value.r * 255)) << 24 | uint32_t(std::lround(value.g * 255)) << 16 |
                   uint32_t(std::lround(value.b * 255)) << 8 | uint32_t(std::lround(value.a * 255));
        };
        cybercanvas::TextSpan span;
        span.text = blessingLabel;
        span.style = style;
        span.ink = {rgba(0xFFF4CDFF, 0xFFF1BDFF), rgba(0xE8C884FF, 0xDBB96EFF),
                    std::clamp(blessingSize * .036f, .7f, 1.2f), rgba(0x21483DE8, 0x071B2CF5)};
        cybercanvas::StyledText text(canvas, std::span(&span, 1),
                                     {.available = {std::ceil(metrics.width) + 2, metrics.height}, .paragraph = style});
        const auto source = text.snapshot(dpr, 4);
        const cybercanvas::EffectStyle shadow{
            cybercanvas::EffectKind::Shadow, 1.5, {1.3, 2.1}, rgba(0x10362F90, 0x020F1BB0)};
        blessingImage = blessingEffects.render(*source, std::span(&shadow, 1), dpr);
        blessingOrigin = {blessingImage.offset.x - 4, blessingImage.offset.y - 4};
        blessingBitmapSize =
            D2D1::SizeF(float(blessingImage.image->width() / dpr), float(blessingImage.image->height() / dpr));
        blessingNight = night;
    }

    void blessing(const Scene& scene, const Appearance& appearance, float w, float h) {
        const auto& blessing = scene.blessing();
        const float opacity = float(blessing.opacity());
        if (!appearance.blessings || opacity <= .001f) {
            return;
        }
        const auto label = kBlessingWords[blessing.word];
        const float size = blessingFontSize(w, h, label.size());
        if (label != blessingLabel || size != blessingSize || blessingArea.width != w || blessingArea.height != h) {
            blessingImage = {};
            blessingLabel = label;
            blessingSize = size;
            blessingArea = {w, h};
        }
        cybercanvas::TextStyle style;
        style.family = blessingFamily;
        style.locale = L"zh-CN";
        style.size = size;
        style.bold = true;
        style.vertical = cybercanvas::VerticalAlign::Top;
        style.characterSpacing = .7f;
        // Borrow for this paint only; the shared layout cache owns font resources.
        const auto blessingText = canvas.nativeTextLayout(std::wstring(label), style, {w, h});
        DWRITE_TEXT_METRICS metrics{};
        require(blessingText->GetMetrics(&metrics));
        cacheBlessing(appearance.night, metrics, style);
        // Debug startup pre-ages the first word for readability; still animate its entrance.
        const double enter = std::clamp(std::min(blessing.age, scene.time()) / 1.2, 0.0, 1.0);
        const double settled = 1 - std::pow(1 - enter, 3);
        const double leave = std::clamp((blessing.age - kBlessingDuration + 1.3) / 1.3, 0.0, 1.0);
        const double y = blessing.center.y + (1 - settled) * 9 - blessing.age * 1.1 - leave * leave * 4;
        const Vec center{blessing.center.x + std::sin(blessing.age * .55) * 1.4,
                         std::clamp(y, metrics.height * .6 + 5, h - metrics.height * .6 - 5)};
        glow(center, std::max(double(size), metrics.width * .65), size * .85,
             blend(0xFFECC51B, 0xFFE8B325, appearance.night, opacity));
        D2D1_MATRIX_3X2_F original;
        target->GetTransform(&original);
        const float scale = float(.84 + settled * .16 + leave * leave * .025);
        target->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale, point(center)) * original);
        // Paired fine ripples carry the gold text, sharing its eased rise and fade.
        for (int ring = 0; ring < 2; ++ring) {
            const double progress = std::clamp(blessing.age * .22 + ring * .22, 0.0, 1.0);
            const float radius = float(std::max(double(size) * .78, metrics.width * .40) * (1 + progress * .90));
            ink(blend(0xFFF1BD75, 0xF6DC9D83, appearance.night, opacity * float((1 - progress) * .75)));
            target->DrawEllipse(D2D1::Ellipse(point(center + Vec{0, size * .61}), radius, radius * .17f), brush.Get(),
                                .65f);
        }
        const double left = center.x - metrics.width * .5 + blessingOrigin.x;
        const double top = center.y - metrics.height * .5 + blessingOrigin.y;
        canvas.image(*blessingImage.image, {left, top, blessingBitmapSize.width, blessingBitmapSize.height},
                     {.fit = cybercanvas::ImageFit::Stretch, .opacity = opacity});
        target->SetTransform(original);
    }

    void reflections(const Scene& scene, const Appearance& appearance, float w, float h) {
        const double t = motionTime(scene, appearance);
        const float strength = float(appearance.shimmer);
        for (int row = 0; row < 11; ++row) {
            std::vector<Vec> points;
            const double start = w * (.08 + .045 * std::sin(row * 17.0));
            const double end = w * (.62 + .24 * std::sin(row * 7.1));
            const double y = h * (row + .5) / 11 + std::sin(t * .65 + row * 1.7) * 3.3;
            for (int i = 0; i < 14; ++i) {
                const double x = std::lerp(start, end, i / 13.0);
                const double offset =
                    std::sin(x * .052 + t * 1.1 + row * .8) * 2.1 + waterNoise.animated({x, row * 29.}, t, .45) * 4.5;
                points.push_back({x, y + offset});
            }
            const auto path = curve(points, false);
            const double shimmer = .5 + .5 * waterNoise.animated({row * 43., 193}, t, .6);
            const float opacity = strength * float(.05 + .17 * shimmer * shimmer);
            ink(blend(0xE5F7CAFF, 0x95C9E2FF, appearance.night, opacity));
            target->DrawGeometry(path.Get(), brush.Get(), .65f);
        }
        // Fine sun/moon glints drift along the water independently of the fish.
        for (int i = 0; i < 24; ++i) {
            const double phase = i * 8.173;
            const Vec center{w * (.5 + .42 * std::sin(phase)) + std::sin(t * .85 + phase) * 7,
                             h * (.5 + .42 * std::cos(phase * 1.73)) + std::cos(t * .72 + phase) * 3};
            const float shine = float(std::pow(.5 + .5 * std::sin(t * 1.4 + phase), 4)) * strength;
            if (shine < .02f) {
                continue;
            }
            ellipse(center, 1.4 + shine * 4.5, .4 + shine * .35,
                    blend(0xFFFFDCC0, 0xC2E8FFD0, appearance.night, shine));
        }
        for (const auto& ripple : scene.ripples()) {
            for (int i = 0; i < 3; ++i) {
                const double age = ripple.age - i * .17;
                if (age <= 0) {
                    continue;
                }
                const double radius = 3 + age * 24;
                ink(blend(0xE4F8D6FF, 0xA2D8EFFF, appearance.night, float(std::max(0.0, .38 - age * .14))));
                target->DrawEllipse(D2D1::Ellipse(point(ripple.center), float(radius), float(radius * .7)), brush.Get(),
                                    .7f);
            }
        }
    }

    void plantRipple(Vec center, double radius, double time, double phase, double night) {
        for (int ring = 0; ring < 2; ++ring) {
            const double cycle = time * .30 + phase + ring * .5;
            const double progress = cycle - std::floor(cycle);
            const double wave = radius * 1.04 + (3 + radius * .34) * progress;
            const float opacity = float(std::pow(std::sin(kPi * progress), 2) * .60);
            ink(blend(0xD8F0C95A, 0xA1D0DF65, night, opacity));
            target->DrawEllipse(D2D1::Ellipse(point(center + Vec{0, .6}), float(wave), float(wave)), brush.Get(), .6f);
        }
    }

    void leaf(Vec center, double radius, double angle, double time, double night, bool notched) {
        const double phase = angle * 1.7 + radius * .19;
        const double drift = std::clamp(radius * .30, 3.0, 7.0);
        const double breeze = plantNoise.animated({center.x, center.y}, time, .38);
        center = center + Vec{(.65 * std::sin(time * 1.0 + phase) + .35 * breeze) * drift,
                              (.65 * std::cos(time * .85 + phase * 1.3) + .35 * breeze) * drift * .75};
        angle += .14 * std::sin(time * .92 + phase) + .10 * breeze;
        plantRipple(center, radius, time, phase, night);
        D2D1_MATRIX_3X2_F original;
        target->GetTransform(&original);
        target->SetTransform(D2D1::Matrix3x2F::Rotation(float(angle * 180 / kPi)) *
                             D2D1::Matrix3x2F::Translation(float(center.x), float(center.y)) * original);
        ComPtr<ID2D1Geometry> shape;
        if (notched) {
            auto path = canvas.path();
            path.moveTo({0, 0})
                .lineTo(pathPoint(direction(kPi / 2 - .20) * radius))
                .arcTo(pathPoint(direction(kPi / 2 + .20) * radius), {radius, radius}, 0,
                       cybercanvas::ArcSweep::CounterClockwise, cybercanvas::ArcSize::Large)
                .close();
            shape = path.geometry(factory.Get());
        } else {
            ComPtr<ID2D1EllipseGeometry> circle;
            require(factory->CreateEllipseGeometry(D2D1::Ellipse(D2D1::Point2F(0, 0), float(radius), float(radius)),
                                                   circle.GetAddressOf()));
            shape = circle;
        }
        target->SetTransform(D2D1::Matrix3x2F::Translation(1.5f, 3) *
                             D2D1::Matrix3x2F::Rotation(float(angle * 180 / kPi)) *
                             D2D1::Matrix3x2F::Translation(float(center.x), float(center.y)) * original);
        ink(blend(0x113F325A, 0x02152688, night));
        target->FillGeometry(shape.Get(), brush.Get());
        target->SetTransform(D2D1::Matrix3x2F::Rotation(float(angle * 180 / kPi)) *
                             D2D1::Matrix3x2F::Translation(float(center.x), float(center.y)) * original);
        const auto fill = gradient({-radius, -radius}, {radius, radius}, blend(0xAAC775FF, 0x527F78FF, night),
                                   blend(0x36724EFF, 0x214D50FF, night));
        target->FillGeometry(shape.Get(), fill.Get());
        ink(blend(0xCEE4A777, 0x7BA69C66, night));
        target->DrawGeometry(shape.Get(), brush.Get(), .65f);
        for (int i = 0; i < 11; ++i) {
            const double a = notched ? kPi * (.47 + i * 1.08 / 10) : 2 * kPi * i / 11;
            const Vec end{std::cos(a) * radius * .91, std::sin(a) * radius * .91};
            line({}, end, blend(0xD1DFA55A, 0x96BBB847, night), .45);
            const Vec middle = end * .53;
            line(middle, middle + direction(a + .5) * radius * .23, blend(0xC0D29530, 0x96BBB82A, night), .35);
        }
        ellipse({}, radius * .06, radius * .06, blend(0xD9E2A5CC, 0x83B5A4CC, night));
        ellipse({-radius * .34, -radius * .24}, radius * .09, radius * .065, color(0xE8FAE37A));
        ellipse({-radius * .37, -radius * .26}, radius * .035, radius * .025, color(0xFFFFEFCC));
        target->SetTransform(original);
    }

    void flower(Vec center, double radius, double time, double night) {
        const double drift = std::clamp(radius * .38, 2.5, 6.0);
        const double breeze = plantNoise.animated({center.x, center.y}, time, .38);
        const double lift = .7 * std::cos(time * .94) + .3 * breeze;
        center = center + Vec{(.7 * std::sin(time * 1.12) + .3 * breeze) * drift, lift * drift * .75};
        plantRipple(center, radius * .85, time, radius * .13, night);
        ellipse(center + Vec{1.5, 3 + lift * .7}, radius * .97, radius * .76, blend(0x123D3552, 0x01182A70, night));
        D2D1_MATRIX_3X2_F original;
        target->GetTransform(&original);
        for (int layer = 0; layer < 3; ++layer) {
            const int count = 9 - layer;
            for (int i = 0; i < count; ++i) {
                const double length =
                    radius * (1 - layer * .23) * (1 + .055 * std::sin(time * 1.2 + layer * .8 + i * .45));
                const double angle = 2 * kPi * (i + layer * .43) / count + .16 * std::sin(time * 1.02) +
                                     .018 * std::sin(time * 1.55 + layer);
                target->SetTransform(D2D1::Matrix3x2F::Rotation(float(angle * 180 / kPi)) *
                                     D2D1::Matrix3x2F::Translation(float(center.x), float(center.y)) * original);
                // Narrow pointed petals, layered from blush-pink tips to an ivory heart.
                const auto petal =
                    curve({{0, 2}, {-length * .30, -length * .34}, {0, -length}, {length * .30, -length * .34}});
                const auto fill = gradient({0, -length}, {0, 0}, blend(0xD87C95FF, 0xAC799AFF, night),
                                           blend(0xFFF1DFFF, 0xD6DCE6FF, night));
                target->FillGeometry(petal.Get(), fill.Get());
                ink(blend(0xFFE9DD88, 0xDFC9E376, night));
                target->DrawGeometry(petal.Get(), brush.Get(), .4f);
                line({0, -length * .15}, {0, -length * .75}, blend(0xFFEEDB6B, 0xF1DDEA4A, night), .4);
            }
        }
        target->SetTransform(original);
        ellipse(center, radius * .16, radius * .14, blend(0xE3BC60FF, 0xC0AA78FF, night));
        for (int i = 0; i < 8; ++i) {
            ellipse(center + direction(i * kPi / 4) * radius * .11, radius * .025, radius * .025, color(0xFFF1B3FF));
        }
    }

    void plants(const Scene& scene, const Appearance& appearance, float w, float h) {
        const double unit = std::min(std::sqrt(w * h) * .092, std::min(w, h) * .19);
        const double time = motionTime(scene, appearance), night = appearance.night;
        if (appearance.leaves) {
            for (const auto& pad : lilyPads(w, h)) {
                leaf(pad.center, pad.radius, pad.angle, time, night, pad.notched);
            }
        }
        if (appearance.flowers) {
            flower({w * .19, h * .23}, unit * .64, time, night);
            flower({w * .87, h * .72}, unit * .52, time + 2, night);
            if (w * h > 95000) {
                flower({w * .90, h * .92}, unit * .34, time + 4, night);
            }
        }
    }

    void paintPond(const Scene& scene, const Appearance& appearance, CdRect safe, ID2D1Geometry* bounds) {
        pondSurface.render(
            canvas.target.Get(), {width, height, dpr},
            [&](auto* destination) {
                // Restore the renderer's native alias even when a painter throws.
                const auto mainTarget = target;
                struct RestoreTarget {
                    ComPtr<ID2D1RenderTarget>& current;
                    ComPtr<ID2D1RenderTarget> previous;
                    ~RestoreTarget() {
                        current = previous;
                    }
                } restore{target, mainTarget};
                target = destination;
                target->SetTransform(D2D1::Matrix3x2F::Translation(float(safe.x), float(safe.y)));
                if (bounds) {
                    const float w = float(safe.width), h = float(safe.height);
                    pondSurface.masked(
                        bounds,
                        [&] {
                            if (appearance.background) {
                                water(motionTime(scene, appearance), appearance.night, w, h);
                            }
                            for (const auto& value : scene.fish()) {
                                fish(value, appearance.night, appearance.koiColor);
                            }
                            reflections(scene, appearance, w, h);
                            plants(scene, appearance, w, h);
                        },
                        D2D1::RectF(0, 0, w, h));
                    if (appearance.background) {
                        ink(blend(0xD5EBCD38, 0xA3D0DF35, appearance.night));
                        target->DrawGeometry(bounds, brush.Get(), .8f);
                    }
                }
            },
            canvas.resourceAccount());
    }
};

Renderer::Renderer() : impl_(std::make_unique<Impl>()) {}

Renderer::~Renderer() = default;

void Renderer::bindOutput(const CdCreateContext& context) {
    impl_->canvas.bindGpu(context);
}

bool Renderer::render(const Scene& scene, const Appearance& appearance, const CdFrame& frame, CdRect safe) {
    try {
        auto& p = *impl_;
        if (!p.canvas.valid(frame)) {
            return false;
        }
        const double w = frame.width / frame.device_pixel_ratio, h = frame.height / frame.device_pixel_ratio;
        if (safe.width <= 0 || safe.height <= 0) {
            safe = {0, 0, w, h};
        }
        safe.x = std::clamp(safe.x, 0.0, w);
        safe.y = std::clamp(safe.y, 0.0, h);
        safe.width = std::clamp(safe.width, 0.0, w - safe.x);
        safe.height = std::clamp(safe.height, 0.0, h - safe.y);

        auto painting = p.canvas.frame(frame);
        p.surface(frame);
        ComPtr<ID2D1RoundedRectangleGeometry> bounds;
        if (safe.width > 12 && safe.height > 12) {
            const float width = float(safe.width), height = float(safe.height);
            const float radius = std::clamp(appearance.radius, 0.f, std::min(width - 4, height - 5) * .5f);
            require(p.factory->CreateRoundedRectangleGeometry(
                D2D1::RoundedRect(D2D1::RectF(1, 1, width - 3, height - 4), radius, radius), bounds.GetAddressOf()));
        }
        if (appearance.redrawPond || !p.pondSurface.bitmap()) {
            p.paintPond(scene, appearance, safe, bounds.Get());
        }
        p.target->DrawBitmap(p.pondSurface.bitmap(), D2D1::RectF(0, 0, float(w), float(h)));
        if (bounds && appearance.blessings && scene.blessing().opacity() > .001) {
            p.target->SetTransform(D2D1::Matrix3x2F::Translation(float(safe.x), float(safe.y)));
            p.canvas.masked(
                bounds.Get(),
                [&] {
                    p.blessing(scene, appearance, float(safe.width), float(safe.height));
                },
                D2D1::RectF(0, 0, float(safe.width), float(safe.height)));
        }
        painting.finish();
        return true;
    } catch (...) {
        return false;
    }
}

bool Renderer::savePng(const std::wstring& path, const CdFrame& frame) {
    return impl_->canvas.save(path, frame);
}
}  // namespace koiplugin
