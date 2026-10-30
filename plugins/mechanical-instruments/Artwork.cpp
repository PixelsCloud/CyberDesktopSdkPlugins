#include "plugins/mechanical-instruments/Artwork.h"

namespace mechanicalart {
auto Stage::map(Rect r) const -> Rect {
    return {origin.x + r.x * scale, origin.y + r.y * scale, r.width * scale, r.height * scale};
}

Stage arrange(Kind kind, double width, double height) {
    const Rect artwork = kind == Kind::Timer    ? Rect{106, 10, 783, 988}
                         : kind == Kind::Disk   ? Rect{39, 25, 900, 960}
                         : kind == Kind::Player ? Rect{30, 70, 952, 495}
                                                : Rect{23, 78, 953, 483};
    const double margin = std::min(width, height) * .045;
    const auto view = cybercanvas::sceneViewport(
        {margin, margin, std::max(1., width - 2 * margin), std::max(1., height - 2 * margin)},
        {artwork.width, artwork.height});
    return {view.scale, {view.origin.x - artwork.x * view.scale, view.origin.y - artwork.y * view.scale}};
}

Rect timerButton(unsigned index) {
    return {index ? 605. : 230., 805, 158, 158};
}

Rect playerButton(unsigned index) {
    constexpr double centers[]{587, 720, 854};
    return {centers[std::min(index, 2u)] - 52, 317, 104, 104};
}

void text(Canvas& c, const std::wstring& value, Rect bounds, double size, uint32_t color, bool bold) {
    cybercanvas::TextStyle style;
    style.family = L"Microsoft YaHei UI";
    style.size = size;
    style.weight = bold ? cybercanvas::FontWeight::SemiBold : cybercanvas::FontWeight::Normal;
    style.align = cybercanvas::TextAlign::Center;
    style.vertical = cybercanvas::VerticalAlign::Center;
    style.ellipsis = true;
    style.wrap = false;
    style.tabularFigures = true;
    c.text(value, style, bounds, color);
}

Point polar(Point center, double radius, double degrees) {
    const double a = degrees * std::numbers::pi / 180;
    return {center.x + radius * std::cos(a), center.y + radius * std::sin(a)};
}

void scale(Canvas& c, Point center, double radius, double maximum, bool ivory, bool full) {
    const auto ink = ivory ? 0x423b2bff : kIvory;
    const unsigned divisions = full ? 60 : 50;
    for (unsigned i = 0; i <= divisions; ++i) {
        if (full && i == divisions) {
            continue;
        }
        const bool major = i % 10 == 0;
        const double angle = full ? -90 + 360. * i / divisions : 140 + 260. * i / divisions;
        const auto a = polar(center, radius, angle);
        const auto b = polar(center, radius * (major ? .88 : .94), angle);
        c.line(a.x, a.y, b.x, b.y, !ivory && i >= divisions * .9 ? 0xc75043ff : ink, major ? 3.2 : 1.6);
        if (major) {
            const auto p = polar(center, radius * .73, angle);
            text(c, std::to_wstring(int(std::lround(maximum * i / divisions))), {p.x - 50, p.y - 30, 100, 60},
                 radius * .15, ink);
        }
    }
}

void needle(Canvas& c, Point center, double length, double degrees, bool valid) {
    c.saved([&] {
        c.translate(center.x, center.y);
        c.rotate(degrees + 90);
        auto hand = c.path();
        // Widen toward the spindle; the outward reading end is always a fine tip.
        hand.moveTo({0, -length}).lineTo({6, 2}).lineTo({4, 19}).lineTo({-4, 19}).lineTo({-6, 2}).close();
        c.saved([&] {
            c.translate(2, 4);
            c.fill(hand, 0x00000090);
        });
        c.masked(hand, [&] {
            c.gradientBox({-9, -length, 18, length + 20}, 0, valid ? 0xffe3a1ff : 0xb4b6acff,
                          valid ? 0x92652dff : 0x616965ff);
        });
        c.line(0, -length * .80, -1, 9, 0xfff0c3c0, .9);
    });
    const double hub = std::min(19., length * .2);
    c.oval(center.x, center.y, hub, hub, 0x805c30ff);
    c.oval(center.x - hub / 19, center.y - hub / 19, hub * 16 / 19, hub * 16 / 19, 0xe2bf78ff);
    c.oval(center.x, center.y, hub * 9 / 19, hub * 9 / 19, 0x66665cff);
    c.oval(center.x - hub / 19, center.y - hub * 2 / 19, hub * 6 / 19, hub * 6 / 19, 0xf1eee1ff);
}

auto Artwork::bind(const CdCreateContext& context) -> void {
    images_.bind(context);
    if (context.plugin_directory && *context.plugin_directory && context.plugin_id &&
        std::string_view(context.plugin_id).find(".mechanical.") != std::string_view::npos) {
        root_ = std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(context.plugin_directory))) /
                "assets" / "mechanical";
    }
}

auto Artwork::pause(bool paused) -> void {
    images_.pause(paused);
}

auto Artwork::onChange(std::function<void()> callback) -> void {
    images_.onChange(std::move(callback));
}

auto Artwork::pending() const -> bool {
    return images_.pending();
}

auto Artwork::poll() -> bool {
    return images_.poll();
}

auto Artwork::material(Canvas& c, const std::string& name, double logicalEdge)
    -> std::shared_ptr<const cybercanvas::ImageData> {
    if (root_.empty()) {
        return {};
    }
    const auto edge = uint32_t(std::clamp(std::ceil(logicalEdge * c.devicePixelRatio()), 16., 2048.));
    images_.set(name, root_ / (name + ".png"), 1, edge);
    images_.poll();
    auto& retained = materials_[name];
    if (const auto next = images_.get(name).image) {
        retained = next;
    }
    return retained;
}

auto Artwork::body(Canvas& c, Kind kind, const Stage& stage) -> bool {
    if (!root_.empty()) {
        const auto edge = uint32_t(std::clamp(std::ceil(1000 * stage.scale * c.devicePixelRatio()), 64., 2048.));
        images_.set("body", root_ / "body.png", 0, edge);
        images_.poll();
        if (const auto next = images_.get("body").image) {
            image_ = next;
        }
    }
    if (image_) {
        c.image(*image_, {0, 0, 1000, 1000. * image_->height() / image_->width()},
                {.fit = cybercanvas::ImageFit::Stretch, .clip = false});
        return true;
    }
    // Missing artwork still leaves a readable device rather than a blank tile.
    const bool timer = kind == Kind::Timer;
    if (timer || kind == Kind::Disk) {
        const Point center = timer ? Point{492, 497} : Point{462, 461};
        c.oval(center.x, center.y, timer ? 378 : 380, timer ? 378 : 380, 0x88938dff);
        c.oval(center.x, center.y, timer ? 310 : 315, timer ? 310 : 315, timer ? 0xe9dbafff : 0x151e22ff);
        if (timer) {
            for (unsigned i = 0; i < 2; ++i) {
                const auto b = timerButton(i);
                c.oval(b.x + b.width / 2, b.y + b.height / 2, b.width / 2, b.height / 2, 0xaeb6b1ff);
            }
        } else {
            c.gradientBox({213, 821, 515, 110}, 50, 0x192124ff, 0x070d10ff);
        }
    } else if (kind == Kind::Weather) {
        for (const auto circle : {Rect{100, 130, 330, 330}, Rect{472, 190, 210, 210}, Rect{732, 190, 210, 210}}) {
            c.oval(circle.x + circle.width / 2, circle.y + circle.height / 2, circle.width / 2 + 8,
                   circle.height / 2 + 8, 0x9ba6a0ff);
            c.oval(circle.x + circle.width / 2, circle.y + circle.height / 2, circle.width / 2, circle.height / 2,
                   0x151e22ff);
        }
    }
    return false;
}

auto Artwork::icon(Canvas& c, const Stage& stage, Rect box, Icon symbol, uint32_t color) -> void {
    paintIcon(c, stage.map(box), symbol, color);
}

auto Artwork::paintIcon(Canvas& c, Rect bounds, Icon symbol, uint32_t color) -> void {
    mechanicalart::paintIcon(c, bounds, symbol, color);
}
}  // namespace mechanicalart
