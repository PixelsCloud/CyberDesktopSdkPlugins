#include "plugins/koi-suite/FishArtwork.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace koiplugin {
using Microsoft::WRL::ComPtr;

namespace {
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

struct FishPainter {
    cybercanvas::Canvas& canvas;
    ComPtr<ID2D1Factory> factory;
    ComPtr<ID2D1RenderTarget> target;
    ComPtr<ID2D1SolidColorBrush> brush;
    cybercanvas::OffscreenSurface* surface;

    void masked(ID2D1Geometry* shape, const std::function<void()>& paint) {
        if (surface) {
            surface->masked(shape, paint);
        } else {
            canvas.masked(shape, paint);
        }
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

    Vec skinPoint(const Fish& fish, double joint, double side) {
        const double position = std::clamp(joint, 0.0, 8.999);
        const int index = int(position);
        const double fraction = position - index;
        const Vec center = fish.joints[index] * (1 - fraction) + fish.joints[index + 1] * fraction;
        const double angle =
            fish.angles[index] + angleDifference(fish.angles[index + 1], fish.angles[index]) * fraction;
        const double width = std::lerp(fish.widths[index], fish.widths[index + 1], fraction) * fish.link / 64;
        return center + direction(angle + kPi / 2) * (side * width) -
               direction(angle) * ((joint - position) * fish.link);
    }

    void marking(const Fish& fish, double center, double length, double width, double side, double variation,
                 ID2D1Brush* fill) {
        std::vector<Vec> points;
        points.reserve(14);
        for (int i = 0; i < 14; ++i) {
            const double angle = 2 * kPi * i / 14;
            const double radius = 1 + .12 * std::sin(angle * 3 + variation) + .07 * std::cos(angle * 5 - variation);
            points.push_back(
                skinPoint(fish, center + std::cos(angle) * length * radius, side + std::sin(angle) * width * radius));
        }
        const auto patch = curve(points);
        target->FillGeometry(patch.Get(), fill);
    }

    void fish(const Fish& fish, double night, uint32_t koiColor) {
        const double scale = fish.link / 64;
        const double mid = angleDifference(fish.angles[0], fish.angles[6]);
        const bool white = fish.pattern == 4;
        const float visible = float(1 - night * .14 - fish.depth * .10);
        const auto finColor = blend(white ? 0xF5F7F9AE : 0xFFF0DCAE, white ? 0xC2D6DEA0 : 0xB8D4DBA0, night, visible);
        const auto outline = blend(0x153B354A, 0x101C365F, night);
        D2D1_MATRIX_3X2_F original;
        target->GetTransform(&original);

        for (int joint : {3, 7}) {
            for (int sign : {-1, 1}) {
                const auto pose = finPose(fish, joint, sign);
                target->SetTransform(D2D1::Matrix3x2F::Rotation(float(pose.angle * 180 / kPi)) *
                                     D2D1::Matrix3x2F::Translation(float(pose.center.x), float(pose.center.y)) *
                                     original);
                ellipse({}, pose.radiusX, pose.radiusY, finColor);
                ink(outline);
                target->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(0, 0), float(pose.radiusX), float(pose.radiusY)),
                                    brush.Get(), .5f);
            }
        }
        target->SetTransform(original);

        const auto tail = tailOutline(fish);
        const auto caudal = curve(std::vector<Vec>(tail.begin(), tail.end()));
        ink(blend(white ? 0xF7FAFCF2 : 0xFFF9ECF2, 0xD6EEF2E6, night, visible));
        target->FillGeometry(caudal.Get(), brush.Get());
        ink(outline);
        target->DrawGeometry(caudal.Get(), brush.Get(), .5f);

        const auto body = curve(bodyOutline(fish));
        target->SetTransform(D2D1::Matrix3x2F::Translation(3, 5) * original);
        ink(blend(0x103E383D, 0x03112460, night));
        target->FillGeometry(body.Get(), brush.Get());
        target->SetTransform(original);

        const bool gold = fish.pattern == 2;
        const auto base = gradient(fish.edge(3, -kPi / 2), fish.edge(3, kPi / 2),
                                   blend(gold    ? 0xFFF0BAFF
                                         : white ? 0xFCFCFCFF
                                                 : 0xFFFFF7FF,
                                         gold    ? 0xC1B58CFF
                                         : white ? 0xDFE7EBFF
                                                 : 0xD4E1E0FF,
                                         night, visible),
                                   blend(gold    ? 0xC58B38FF
                                         : white ? 0xBAC4CAFF
                                                 : 0xCAD8D3FF,
                                         gold    ? 0x8C703DFF
                                         : white ? 0x869BA5FF
                                                 : 0x8DAEB2FF,
                                         night, visible));
        target->FillGeometry(body.Get(), base.Get());

        // Skin and markings share one coordinate system throughout the masked
        // layer, including nonzero host padding. Patches follow the moving spine.
        masked(body.Get(), [&] {
            if (!gold && !white) {
                auto red = color(koiColor, visible);
                red.r *= float(1 - night * .13);
                red.g = std::min(1.f, red.g + float(night * .10));
                red.b = std::min(1.f, red.b + float(night * .10));
                const auto redShade = D2D1::ColorF(red.r * .79f, red.g * .77f, red.b * .75f, red.a);
                const auto vermilion = gradient(fish.edge(3, -kPi / 2), fish.edge(3, kPi / 2), red, redShade);
                const auto sumi = gradient(fish.edge(3, -kPi / 2), fish.edge(3, kPi / 2),
                                           blend(0x35423FFF, 0x314B58FF, night, visible),
                                           blend(0x182522FF, 0x122A38FF, night, visible));
                const double variation = fish.phase + fish.pattern * 1.7;
                const double shift = .15 * std::sin(fish.phase);
                if (fish.pattern == 3) {
                    marking(fish, 1.8, 1.45, .98, .37, variation, sumi.Get());
                    marking(fish, 5.2, 1.25, .88, -.50, variation + 1, sumi.Get());
                    marking(fish, 7.9, .85, .95, .30, variation + 2, sumi.Get());
                }
                marking(fish, 1.10 + shift, 1.30, .94, -.10, variation, vermilion.Get());
                marking(fish, 4.45 + shift, 1.20, .95, .20, variation + 2, vermilion.Get());
                marking(fish, 7.35 - shift, .94, .88, -.18, variation + 4, vermilion.Get());
                if (fish.pattern == 1) {
                    marking(fish, 3.25, .52, .52, -.66, variation + 3, sumi.Get());
                    marking(fish, 5.85, .48, .62, .60, variation + 5, sumi.Get());
                    marking(fish, 8.35, .32, .53, -.40, variation + 7, sumi.Get());
                }
            }
            std::vector<Vec> sheen;
            for (int i = 1; i < 9; ++i) {
                sheen.push_back(skinPoint(fish, i, -.28 - .11 * std::sin(kPi * i / 9)));
            }
            for (int i = 8; i > 0; --i) {
                sheen.push_back(skinPoint(fish, i, -.28 + .11 * std::sin(kPi * i / 9)));
            }
            const auto highlight = curve(sheen);
            ink(blend(gold ? 0xFFF8CE58 : 0xFFFFFF24, gold ? 0xE6E3CA35 : 0xEAF9FF19, night));
            target->FillGeometry(highlight.Get(), brush.Get());
        });
        ink(outline);
        target->DrawGeometry(body.Get(), brush.Get(), .5f);

        auto dorsal = canvas.path();
        dorsal.moveTo(pathPoint(fish.joints[4]))
            .cubicTo(pathPoint(fish.joints[5]), pathPoint(fish.joints[6]), pathPoint(fish.joints[7]));
        const double mid2 = angleDifference(fish.angles[0], fish.angles[7]);
        dorsal
            .cubicTo(pathPoint(fish.joints[6] + direction(fish.angles[6] + kPi / 2) * (mid2 * 16 * scale)),
                     pathPoint(fish.joints[5] + direction(fish.angles[5] + kPi / 2) * (mid * 16 * scale)),
                     pathPoint(fish.joints[4]))
            .close();
        ink(blend(white ? 0xF0F6FA77 : 0xF5E7C477, 0xC7DCE961, night));
        target->FillGeometry(dorsal.geometry(factory.Get()), brush.Get());
        line(fish.joints[4], fish.joints[7],
             blend(white ? 0xF2F5F898 : 0xFFF6DA98, white ? 0xDCE7EF6F : 0xDEF3F56F, night), .6);
        for (int side : {-1, 1}) {
            const Vec eye = fish.edge(0, side * kPi / 2, -18);
            ellipse(eye, 8 * scale, 8 * scale, color(0x253B35FF));
            ellipse(eye + Vec{-.35, -.35}, 2.6 * scale, 2.6 * scale, color(0xFFFCEDDD));
        }
    }
};
}  // namespace

void drawFish(cybercanvas::Canvas& canvas, const Fish& fish, double night, uint32_t koiColor, ID2D1RenderTarget* target,
              ID2D1SolidColorBrush* brush, cybercanvas::OffscreenSurface* surface) {
    FishPainter painter{canvas, canvas.factory, target ? target : canvas.target.Get(),
                        brush ? brush : canvas.brush.Get(), surface};
    painter.fish(fish, night, koiColor);
}

void drawPlanFish(cybercanvas::Canvas& canvas, const Fish& item, uint32_t accent) {
    const auto polygon = [&](const auto& points) {
        auto path = canvas.path();
        if (!points.empty()) {
            path.moveTo({points[0].x, points[0].y});
            for (size_t i = 1; i < points.size(); ++i) {
                path.lineTo({points[i].x, points[i].y});
            }
        }
        path.close();
        return path;
    };
    auto body = polygon(bodyOutline(item));
    const double length = item.link * 10;
    canvas.saved([&] {
        canvas.translate(length * .025, length * .035);
        canvas.fill(body, 0x00161966);
    });
    for (int side : {-1, 1}) {
        const auto pose = finPose(item, 3, side);
        canvas.saved([&] {
            canvas.translate(pose.center.x, pose.center.y);
            canvas.rotate(pose.angle * 180 / kPi);
            canvas.oval(0, 0, pose.radiusX, pose.radiusY, 0xeee8d6aa);
        });
    }
    auto tail = polygon(tailOutline(item));
    canvas.fill(tail, 0xe9ddc8cc);
    canvas.fill(body, item.pattern == 2 ? 0x413d38ff : 0xf3eee0ff);
    canvas.masked(body, [&] {
        for (unsigned i : {2u, 5u, 7u}) {
            const auto p = item.joints[i];
            canvas.saved([&] {
                canvas.translate(p.x, p.y);
                canvas.rotate(item.angles[i] * 180 / kPi);
                canvas.oval(0, 0, length * (i == 2 ? .14 : .12), length * .065,
                            item.pattern == 1 ? 0xecc15eff : accent);
            });
        }
        const auto a = item.joints[1], b = item.joints[8];
        canvas.line(a.x, a.y, b.x, b.y, 0xffffff40, length * .015);
    });
    for (int side : {-1, 1}) {
        const auto eye = item.edge(0, side * kPi / 2, .02 * length);
        canvas.oval(eye.x, eye.y, length * .014, length * .014, 0x162429ff);
    }
}
}  // namespace koiplugin
