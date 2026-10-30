#include <sdk/Language.h>
#include <sdk/WindowsFonts.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>
#include <optional>
#include <tuple>

#include "plugins/pixel-desk/PixelDigits.h"
#include "plugins/pixel-desk/PixelImages.h"
#include "plugins/pixel-desk/PixelLayout.h"
#include "plugins/pixel-desk/PixelParts.h"
#include "plugins/pixel-desk/PixelProductivity.h"

namespace pixeldesk {
using cybercanvas::Canvas;
using cybercanvas::Rect;
using Microsoft::WRL::ComPtr;

namespace {
constexpr std::array<Rect, 2> kTimerButtons{{{151, 334, 90, 82}, {269, 334, 90, 82}}};
constexpr std::array<Rect, 2> kCompactButtons{{{246, 153, 46, 30}, {313, 153, 46, 30}}};

void ringTick(Canvas& layer, unsigned index, uint32_t color) {
    layer.saved([&] {
        layer.translate(254, 282);
        layer.rotate(150 + index * 240. / 23);
        layer.box(139, -10, 24, 20, 0, color);
    });
}
}  // namespace

TimerGeometry timerGeometry(double width, double height, bool compact) {
    const double bw = compact ? 442 : 505, bh = compact ? 215 : 497;
    const auto bounds = fitBody(width, height, bw, bh);
    const double scale = bounds.width / bw;
    const cybercanvas::Point origin{bounds.x - 2 * scale, bounds.y - 2 * scale};
    const auto map = [&](Rect r) {
        return Rect{origin.x + r.x * scale, origin.y + r.y * scale, r.width * scale, r.height * scale};
    };
    const auto buttons = compact ? kCompactButtons : kTimerButtons;
    return {map({0, 0, compact ? 446. : 509., compact ? 219. : 501.}),
            {map(buttons[0]), map(buttons[1])},
            origin,
            scale,
            map(compact ? Rect{218, 75, 181, 62} : Rect{144, 234, 230, 72}),
            map(compact ? Rect{135, 71, 54, 25} : Rect{183, 190, 142, 27})};
}

double timerFraction(const cdsdk::Settings& settings, const productivity::Data& data) {
    const auto total = settings.at(data.resting ? "break_minutes" : "focus_minutes").integer * 60;
    return std::clamp(double(data.remaining) / std::max<int64_t>(1, total), 0., 1.);
}

struct TimerFace::Impl {
    using Key = std::tuple<double, double, double, const cybercanvas::ImageData*>;
    cdsdk::Canvas canvas;
    cdsdk::Images images;
    cdsdk::Controls controls;
    cdsdk::WindowsFonts fonts{canvas.nativeTextFactory()};
    cdsdk::RedrawRequests redraw;
    std::filesystem::path path;
    std::shared_ptr<const cybercanvas::ImageData> body;
    std::optional<Key> key;
    ComPtr<ID2D1RenderTarget> owner;
    cybercanvas::SurfaceImage plate;
    uint64_t builds = 0;
    bool compact = false;

    void prepare(double width, double height, double dpi) {
        const auto g = timerGeometry(width, height, compact);
        const auto edge = uint32_t(std::clamp(std::ceil(g.artwork.width * dpi / 64) * 64, 64., 4096.));
        pixeldesk::requestPixelImage(images, "body", path, 1, edge, dpi);
        images.poll();
        body = images.get("body").image;
        const Key next{width, height, dpi, body.get()};
        if (key == next && owner == canvas.target) {
            return;
        }
        cybercanvas::OffscreenSurface surface;
        canvas.renderLayer(
            surface, {uint32_t(std::ceil(width * dpi)), uint32_t(std::ceil(height * dpi)), dpi}, [&](Canvas& layer) {
                if (body) {
                    layer.image(*body, g.artwork,
                                {.fit = cybercanvas::ImageFit::Stretch,
                                 .clip = false,
                                 .sampling = cybercanvas::ImageSampling::Nearest});
                }
                if (!compact) {
                    layer.saved([&] {
                        layer.translate(g.origin.x, g.origin.y);
                        layer.scale(g.scale, g.scale);
                        for (unsigned i = 0; i < 24; ++i) {
                            ringTick(layer, i, 0x52758aff);
                        }
                        for (unsigned i = 0; i < kTimerButtons.size(); ++i) {
                            const auto r = kTimerButtons[i];
                            stepped(layer, {r.x - 3, r.y - 3, r.width + 6, r.height + 6}, 7, kInk);
                            stepped(layer, r, 6, i ? 0x466b85ff : kMint);
                            layer.line(r.x + 8, r.y + 4, r.x + r.width - 9, r.y + 4, i ? 0x7296adff : 0xc1f0ceff, 3);
                        }
                    });
                }
            });
        plate = surface.snapshot();
        owner = canvas.target;
        key = next;
        ++builds;
    }
};

TimerFace::TimerFace() : impl_(std::make_unique<Impl>()) {}

TimerFace::~TimerFace() = default;

void TimerFace::bind(const CdCreateContext& context, std::function<void(uint32_t)> action) {
    impl_->compact = std::string_view(context.plugin_id).ends_with(".2x1");
    impl_->images.bind(context);
    impl_->canvas.bindGpu(context);
    impl_->redraw.bind(context);
    impl_->images.onChange([this] {
        impl_->redraw.invalidate();
    });
    impl_->controls.bind(context, std::move(action));
    impl_->path =
        std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(context.plugin_directory))) /
        "assets/body.png";
}

void TimerFace::pause(bool paused) {
    impl_->images.pause(paused);
    impl_->redraw.pause(paused);
    if (paused) {
        cancelInput();
    }
}

void TimerFace::cancelInput() {
    impl_->controls.cancel();
}

void TimerFace::input(const CdInput& event) {
    impl_->controls.input(event);
}

void TimerFace::syncInput(const cdsdk::Settings& settings, const productivity::Data& data, CdRect safe) {
    const auto geometry = timerGeometry(safe.width, safe.height, impl_->compact);
    const bool english = settings.at("language").text == "en";
    std::vector<cdsdk::Controls::Item> items;
    for (unsigned i = 0; i < 2; ++i) {
        const auto r = geometry.buttons[i];
        items.push_back({i + 1, r,
                         i == 1         ? (english ? "Reset" : "重置")
                         : data.running ? (english ? "Pause" : "暂停")
                                        : (english ? "Start" : "开始"),
                         true});
    }
    impl_->controls.items(std::move(items), {safe.x, safe.y});
}

bool TimerFace::ready() const {
    return impl_->body && !impl_->images.pending() && impl_->images.get("body").image == impl_->body;
}

bool TimerFace::render(const cdsdk::Settings& settings, const productivity::Data& data, const CdFrame& frame,
                       CdRect safe) {
    if (!impl_->canvas.valid(frame)) {
        return false;
    }
    impl_->redraw.beginFrame();
    const cdsdk::LanguageScope language(settings.at("language").text);
    impl_->fonts.select(impl_->canvas.nativeTextFactory(), settings.at("font_family").text);
    impl_->canvas.fontCollection(impl_->fonts.collection());
    impl_->canvas.render(frame, safe, [&](Canvas& layer, CdRect area) {
        impl_->prepare(area.width, area.height, frame.device_pixel_ratio);
        syncInput(settings, data, area);
        const auto g = timerGeometry(area.width, area.height, impl_->compact);
        layer.target->DrawBitmap(impl_->plate.Get(), D2D1::RectF(0, 0, float(area.width), float(area.height)), 1,
                                 D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
        layer.saved([&] {
            layer.translate(g.origin.x, g.origin.y);
            layer.scale(g.scale, g.scale);
            const double fraction = timerFraction(settings, data);
            if (impl_->compact) {
                const double angle = (-90 + (1 - fraction) * 360) * std::numbers::pi / 180;
                layer.line(117, 141, 117 + std::cos(angle) * 32, 141 + std::sin(angle) * 32, kInk, 3);
                pixelDisc(layer, 117, 141, 4, kInk);
                pixelDisc(layer, 117, 141, 2, kClay);
            } else if (data.running && !data.finished) {
                for (unsigned i = 0; i < 24; ++i) {
                    if (double(i) / 24 < fraction) {
                        ringTick(layer, i, kMint);
                    }
                }
            }
        });
        const auto seconds = std::clamp<int64_t>(data.remaining, 0, 10800);
        pixelDigits(layer, std::format("{:02}:{:02}", seconds / 60, seconds % 60), g.display, kCream, impl_->compact);

        const bool english = settings.at("language").text == "en";
        std::wstring phase = data.finished                             ? (english ? L"Done" : L"完成")
                             : data.resting                            ? (english ? L"Break" : L"休息")
                             : settings.at("mode").text == "countdown" ? (english ? L"Countdown" : L"倒计时")
                                                                       : (english ? L"Focus" : L"专注");
        if (impl_->compact && !data.finished && !data.resting && settings.at("mode").text == "countdown") {
            phase = english ? L"Timer" : L"\u8ba1\u65f6";
        }
        if (data.completed > 0 && !impl_->compact) {
            phase += L" · " + std::to_wstring(data.completed);
        }
        cybercanvas::TextStyle label;
        label.family = impl_->fonts.family();
        label.locale = cdsdk::drawingLocale();
        label.size = impl_->compact ? std::max(6., 17 * g.scale) : std::max(7.5, 18 * g.scale);
        label.bold = true;
        label.align = cybercanvas::TextAlign::Center;
        label.vertical = cybercanvas::VerticalAlign::Center;
        label.ellipsis = true;
        layer.text(phase, label, g.phase, impl_->compact ? kInk : kCream);
        for (unsigned i = 0; i < 2; ++i) {
            const auto r = g.buttons[i];
            const auto state = impl_->controls.visual(i + 1);
            if (state.hovered || state.pressed) {
                layer.box(r.x, r.y, r.width, r.height, 0, state.pressed ? 0x14242e40 : 0xf5e7cd24);
            }
            const double inset = std::min(r.width, r.height) * (impl_->compact ? .12 : .22);
            impl_->controls.icons.paint(layer, {r.x + inset, r.y + inset, r.width - inset * 2, r.height - inset * 2},
                                        i == 1         ? "rotate-ccw"
                                        : data.running ? "pause"
                                                       : "play",
                                        impl_->compact ? kInk
                                        : i == 1       ? kCream
                                                       : kInk);
        }
    });
    return true;
}

bool TimerFace::save(const std::filesystem::path& path, const CdFrame& frame) {
    return impl_->canvas.save(path.wstring(), frame);
}

uint64_t TimerFace::cacheBuilds() const {
    return impl_->builds;
}
}  // namespace pixeldesk
