#include <CyberCanvas/ReadableSurface.h>
#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <sdk/Canvas.h>
#include <sdk/Language.h>
#include <sdk/WindowsFonts.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

#include "plugins/clock-suite/FlipClock.h"

namespace clockplugin {
using Microsoft::WRL::ComPtr;

namespace {
void check(HRESULT value) {
    if (FAILED(value))
        throw std::runtime_error("Flip clock drawing failed: " + std::to_string(uint32_t(value)));
}

D2D1_COLOR_F color(CdColor c, double alpha = 1) {
    return D2D1::ColorF(c.r / 255.f, c.g / 255.f, c.b / 255.f, float(c.a / 255.0 * alpha));
}

std::wstring number(int value) {
    return (value < 10 ? L"0" : L"") + std::to_wstring(value);
}

D2D1_RECT_F rectangle(Rect r) {
    return D2D1::RectF(float(r.x), float(r.y), float(r.x + r.width), float(r.y + r.height));
}

CdColor ink(const Options& options) {
    if (!options.themeColors)
        return options.primary;
    const auto c = options.cardColor;
    return .2126 * c.r + .7152 * c.g + .0722 * c.b > 150 ? CdColor{37, 37, 37, 255} : CdColor{242, 242, 242, 255};
}

uint32_t packed(CdColor c) {
    return uint32_t(c.r) << 24 | uint32_t(c.g) << 16 | uint32_t(c.b) << 8 | c.a;
}
}  // namespace

struct FlipRenderer::Impl {
    cdsdk::Canvas canvas;
    double dpr = 0;
    uint32_t width = 0, height = 0;
    ComPtr<IWICImagingFactory> wic{canvas.nativeImageFactory()};
    ComPtr<ID2D1Factory>& factory = canvas.factory;
    ComPtr<IDWriteFactory> write{canvas.nativeTextFactory()};
    ComPtr<ID2D1RenderTarget>& target = canvas.target;
    ComPtr<ID2D1SolidColorBrush>& brush = canvas.brush;
    std::unique_ptr<cdsdk::WindowsFonts> fonts = std::make_unique<cdsdk::WindowsFonts>(write.Get());

    struct Face {
        cybercanvas::SurfaceImage bitmap;
        std::shared_ptr<const cybercanvas::ImageData> image;
        Rect digits;
    };

    std::map<std::wstring, std::shared_ptr<Face>> faces;
    cybercanvas::ReadableSurface faceSurface;
    ComPtr<ID2D1RenderTarget> faceOwner;

    void surface(const CdFrame& frame) {
        if (target && width == frame.width && height == frame.height && dpr == frame.device_pixel_ratio) {
            return;
        }
        // Faces are deliberately cached independently of the main shared surface:
        // their glyph halves are sampled by the perspective flip animation.
        faces.clear();
        faceSurface.reset();
        faceOwner.Reset();
        width = frame.width;
        height = frame.height;
        dpr = frame.device_pixel_ratio;
    }

    Rect label(ID2D1RenderTarget* canvas, ID2D1SolidColorBrush* paint, const std::wstring& source, Rect box,
               double size, CdColor c, const Options& options, bool centered = true) {
        const auto text = cdsdk::displayText(source);
        cybercanvas::TextStyle style;
        style.family = fonts->family();
        style.weight = cybercanvas::FontWeight(fonts->weight());
        style.size = size;
        style.locale = options.language == "en" ? L"en-US" : cdsdk::drawingLocale();
        style.align = centered ? cybercanvas::TextAlign::Center : cybercanvas::TextAlign::Start;
        std::vector<std::wstring> references;
        if (text.size() == 2 && text[0] >= L'0' && text[0] <= L'9' && text[1] >= L'0' && text[1] <= L'9') {
            for (wchar_t digit = L'0'; digit <= L'9'; ++digit) {
                references.emplace_back(2, digit);
            }
        }
        const auto fitted = this->canvas.fitTextInk(text, style, {box.x, box.y, box.width, box.height}, references);
        const float x = float(fitted.origin.x), y = float(fitted.origin.y);
        paint->SetColor(color({20, 20, 20, c.a}, options.reliefPercent * .0014));
        canvas->DrawTextLayout(D2D1::Point2F(x + .25f, y + .65f), fitted.layout.Get(), paint);
        paint->SetColor(color(c));
        canvas->DrawTextLayout(D2D1::Point2F(x, y), fitted.layout.Get(), paint);
        return {fitted.bounds.x, fitted.bounds.y, fitted.bounds.width, fitted.bounds.height};
    }

    std::shared_ptr<Face> face(int value, Rect area, const Options& options, CdColor foreground, Rect digits) {
        const auto key = number(value) + L"|" + std::to_wstring(area.width) + L"|" + std::to_wstring(area.height) +
                         L"|" + fonts->family() + L"|" + std::to_wstring(packed(options.cardColor)) + L"|" +
                         std::to_wstring(packed(foreground)) + L"|" + std::to_wstring(options.sizePercent) + L"|" +
                         std::to_wstring(options.reliefPercent) + L"|" + std::to_wstring(options.cornerRadius);
        const auto layoutKey = key + L"|" + std::to_wstring(digits.x) + L"|" + std::to_wstring(digits.y) + L"|" +
                               std::to_wstring(digits.width) + L"|" + std::to_wstring(digits.height);
        if (auto found = faces.find(layoutKey); found != faces.end()) {
            return found->second;
        }
        if (faces.size() > 32) {
            faces.clear();
        }
        auto result = std::make_shared<Face>();
        faceSurface.render(
            factory.Get(), wic.Get(),
            {uint32_t(std::ceil(area.width * dpr)), uint32_t(std::ceil(area.height * dpr)), dpr},
            [&](auto* destination) {
                ComPtr<ID2D1SolidColorBrush> paint;
                check(destination->CreateSolidColorBrush(color(options.cardColor), paint.GetAddressOf()));
                const float radius =
                    float(std::clamp(options.cornerRadius, 0.0, std::min(area.width, area.height) * .5));
                const auto shape =
                    D2D1::RoundedRect(D2D1::RectF(0, 0, float(area.width), float(area.height)), radius, radius);
                destination->FillRoundedRectangle(shape, paint.Get());
                faceSurface.clipped({0, 0, area.width, area.height * .5}, [&] {
                    paint->SetColor(color({0, 0, 0, 255}, .014));
                    destination->FillRoundedRectangle(shape, paint.Get());
                });
                result->digits = label(destination, paint.Get(), number(value), digits,
                                       area.height * options.sizePercent / 100, foreground, options);
            },
            canvas.resourceAccount());
        result->image = faceSurface.image();
        result->bitmap = faceSurface.bitmap(target.Get());
        faces.emplace(layoutKey, result);
        return result;
    }

    void shadow(Rect area, double strength, double cornerRadius) {
        const double radius = std::clamp(cornerRadius, 0.0, std::min(area.width, area.height) * .5);
        for (int i = 4; i > 0; --i) {
            const double spread = i * .5;
            brush->SetColor(color({35, 35, 35, 255}, strength * (i == 1 ? .10 : .022)));
            target->FillRoundedRectangle(
                D2D1::RoundedRect(
                    D2D1::RectF(float(area.x - spread), float(area.y - spread + 1.3),
                                float(area.x + area.width + spread), float(area.y + area.height + spread + 1.3)),
                    float(radius + spread), float(radius + spread)),
                brush.Get());
        }
    }

    void half(ID2D1Bitmap* texture, Rect area, bool lower) {
        const float split = float(area.height * .5);
        const auto source = D2D1::RectF(0, lower ? split : 0, float(area.width), lower ? float(area.height) : split);
        const auto destination =
            D2D1::RectF(float(area.x), float(area.y) + (lower ? split : 0), float(area.x + area.width),
                        float(area.y) + (lower ? float(area.height) : split));
        target->DrawBitmap(texture, destination, 1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, source);
    }

    void moving(const Face& texture, Rect area, bool lower, double angle) {
        // Inverse perspective sampling produces one continuous surface. Drawing
        // adjacent transformed bitmap strips introduces seams at fractional pixels
        // and high DPI.
        const double mid = area.height * .5, hinge = area.y + mid, cosine = std::cos(angle), sine = std::sin(angle),
                     distance = std::max(area.width * 2.4, area.height * 3.2);
        const double edgeScale = distance / (distance - mid * sine), extent = mid * cosine * edgeScale;
        if (extent * dpr < .1)
            return;
        const double center = area.x + area.width * .5,
                     left = std::floor((center - area.width * edgeScale * .5) * dpr) / dpr;
        const double top = std::floor((lower ? hinge : hinge - extent) * dpr) / dpr;
        const UINT pw = UINT(std::ceil((center + area.width * edgeScale * .5 - left) * dpr)),
                   ph = UINT(std::ceil((lower ? hinge + extent : hinge) - top) * dpr);
        std::vector<uint8_t> warped(size_t(pw) * ph * 4, 0);
        const auto sample = [&](int x, int y, int channel) {
            if (x < 0 || y < 0 || x >= int(texture.image->width()) || y >= int(texture.image->height()))
                return 0.0;
            return double(texture.image->pixels()[(size_t(y) * texture.image->width() + x) * 4 + channel]);
        };
        for (UINT y = 0; y < ph; ++y)
            for (UINT x = 0; x < pw; ++x) {
                const double dy = (top + (y + .5) / dpr - hinge) * (lower ? 1 : -1);
                if (dy < 0)
                    continue;
                const double offset = dy / (cosine + dy * sine / distance);
                if (offset >= mid)
                    continue;
                const double scale = distance / (distance - offset * sine);
                const double sx = (area.width * .5 + (left + (x + .5) / dpr - center) / scale) * dpr - .5;
                const double sy = (mid + (lower ? offset : -offset)) * dpr - .5;
                if (sx < -.5 || sx > area.width * dpr - .5)
                    continue;
                const int ix = int(std::floor(sx)), iy = int(std::floor(sy));
                const double fx = sx - ix, fy = sy - iy;
                for (int c = 0; c < 4; ++c) {
                    const double value = (sample(ix, iy, c) * (1 - fx) + sample(ix + 1, iy, c) * fx) * (1 - fy) +
                                         (sample(ix, iy + 1, c) * (1 - fx) + sample(ix + 1, iy + 1, c) * fx) * fy;
                    warped[(size_t(y) * pw + x) * 4 + c] =
                        uint8_t(std::clamp(std::lround(value * (c == 3 ? 1 : 1 - sine * .12)), 0l, 255l));
                }
            }
        cybercanvas::SurfaceImage projected;
        check(target->CreateBitmap(
            D2D1::SizeU(pw, ph), warped.data(), pw * 4,
            D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                                   float(96 * dpr), float(96 * dpr)),
            projected.GetAddressOf()));
        target->DrawBitmap(projected.Get(),
                           D2D1::RectF(float(left), float(top), float(left + pw / dpr), float(top + ph / dpr)));
    }

    Rect card(Rect area, FlipValue state, const Options& options, CdColor foreground, Rect digits) {
        shadow(area, options.reliefPercent / 100, options.cornerRadius);
        auto next = face(state.after, area, options, foreground, digits);
        if (state.progress >= 1 || state.before == state.after)
            target->DrawBitmap(next->bitmap.Get(), rectangle(area));
        else {
            auto previous = face(state.before, area, options, foreground, digits);
            half(next->bitmap.Get(), area, false);
            half(previous->bitmap.Get(), area, true);
            const double p = state.progress * state.progress * (3 - 2 * state.progress),
                         angle = (p < .5 ? p : 1 - p) * 3.14159265358979323846;
            const double shade = std::sin(angle) * .11;
            for (int i = 0; i < 7; ++i) {
                brush->SetColor(color({0, 0, 0, 255}, shade * (1 - i / 7.0)));
                target->FillRectangle(
                    D2D1::RectF(float(area.x), float(area.y + area.height * .5 + i * .75), float(area.x + area.width),
                                float(area.y + area.height * .5 + (i + 1) * .75)),
                    brush.Get());
            }
            moving(p < .5 ? *previous : *next, area, p >= .5, angle);
        }
        brush->SetColor(color(options.cardColor));
        target->FillRectangle(D2D1::RectF(float(area.x + .5), float(area.y + area.height * .5 - .30),
                                          float(area.x + area.width - .5), float(area.y + area.height * .5 + .30)),
                              brush.Get());
        return {area.x + next->digits.x, area.y + next->digits.y, next->digits.width, next->digits.height};
    }
};

FlipRenderer::FlipRenderer() : impl(std::make_unique<Impl>()) {}

FlipRenderer::~FlipRenderer() = default;

void FlipRenderer::bindOutput(const CdCreateContext& context) {
    impl->canvas.bindGpu(context);
}

bool FlipRenderer::render(Spec spec, const Options& options, Time time, const FlipPose& pose, const CdFrame& frame,
                          Rect safe, std::string* error) {
    const cdsdk::LanguageScope languageScope(options.language);
    try {
        auto& p = *impl;
        if (!p.canvas.valid(frame)) {
            throw std::invalid_argument("Invalid flip clock frame");
        }
        p.surface(frame);
        if (p.fonts->select(p.write.Get(), options.fontFamily)) {
            p.faces.clear();
        }
        p.canvas.fontCollection(p.fonts->collection());

        const double width = frame.width / frame.device_pixel_ratio, height = frame.height / frame.device_pixel_ratio;
        if (safe.width <= 0 || safe.height <= 0)
            safe = {0, 0, width, height};
        safe.x = std::clamp(safe.x, 0.0, width);
        safe.y = std::clamp(safe.y, 0.0, height);
        safe.width = std::clamp(safe.width, 0.0, width - safe.x);
        safe.height = std::clamp(safe.height, 0.0, height - safe.y);

        const auto geometry = arrangeFlip(spec, safe.width, safe.height, options.seconds);
        const auto foreground = ink(options);

        auto painting = p.canvas.frame(frame, {safe.x, safe.y, safe.width, safe.height});
        if (p.faceOwner != p.target) {
            p.faces.clear();
            p.faceOwner = p.target;
        }
        const auto defaultDigits = [](Rect area) {
            return Rect{area.width * .08, area.height * .20, area.width * .84, area.height * .60};
        };
        auto hourDigits = defaultDigits(geometry.hours), minuteDigits = defaultDigits(geometry.minutes);
        if (options.seconds) {
            const double space = std::max(3.0, geometry.minutes.height * .025);
            if (spec.columns == 4) {
                const double center = geometry.minutes.height * .5;
                minuteDigits.height = hourDigits.height = std::max(
                    1.0, std::min(minuteDigits.height, 2 * (geometry.seconds.y - geometry.minutes.y - space - center)));
                minuteDigits.y = hourDigits.y = center - minuteDigits.height * .5;
            } else {
                minuteDigits.width = std::max(1.0, geometry.seconds.x - geometry.minutes.x - space - minuteDigits.x);
                hourDigits.width = minuteDigits.width;
                hourDigits.x = (geometry.hours.width - hourDigits.width) * .5;
            }
        }
        p.card(geometry.hours, pose.values[0], options, foreground, hourDigits);
        const auto minuteInk = p.card(geometry.minutes, pose.values[1], options, foreground, minuteDigits);
        if (options.twelveHour) {
            const auto period =
                options.language == "en" ? (time.hour < 12 ? L"AM" : L"PM") : (time.hour < 12 ? L"上午" : L"下午");
            p.label(
                p.target.Get(), p.brush.Get(), period,
                {geometry.hours.x + 5, geometry.hours.y + 2, geometry.hours.width * .30, geometry.hours.height * .18},
                std::clamp(geometry.hours.height * .12, 6.0, 15.0), foreground, options, false);
        }
        if (options.seconds) {
            auto seconds = geometry.seconds;
            if (spec.columns == 4)
                seconds.x = minuteInk.x + minuteInk.width - seconds.width;
            p.card(seconds, pose.values[2], options, foreground, defaultDigits(seconds));
        }
        painting.finish();
        return true;
    } catch (const std::exception& failure) {
        if (error)
            *error = failure.what();
        return false;
    }
}
}  // namespace clockplugin
