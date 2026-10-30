#include <cmath>
#include <numbers>

#include "plugins/mechanical-instruments/Artwork.h"
#include "plugins/mechanical-instruments/HangingParts.h"
#include "plugins/physics-toys/Toys.h"

namespace toys {
namespace {
using cybercanvas::Canvas;
using cybercanvas::Rect;
using mechanicalart::hangingPart;
using mechanicalart::physicsmaterials::registeredImage;

void cord(Canvas& canvas, Point a, Point b, double width = .8) {
    canvas.line(a.x + .4, a.y + .6, b.x + .4, b.y + .6, 0x05090ba0, width + .7);
    canvas.line(a.x, a.y, b.x, b.y, 0xa2adaeff, width);
    canvas.line(a.x - .2, a.y, b.x - .2, b.y, 0xe6e7dbb0, width * .3);
}

void spring(Canvas& canvas, Point a, Point b) {
    const double dx = b.x - a.x, dy = b.y - a.y, length = std::max(1., std::hypot(dx, dy));
    const unsigned turns = unsigned(std::clamp(length / 5, 3., 12.));
    auto back = canvas.path(), front = canvas.path();
    Point previous = a;
    for (unsigned i = 1; i <= turns * 20; ++i) {
        const double t = double(i) / (turns * 20);
        const double phase = t * turns * 2 * std::numbers::pi;
        const double offset = std::sin(phase) * 2.9;
        const Point p{a.x + dx * t + dy / length * offset, a.y + dy * t - dx / length * offset};
        auto& path = std::cos(phase) >= 0 ? front : back;
        path.moveTo(previous).lineTo(p);
        previous = p;
    }
    canvas.stroke(back, 0x384144ff, {.width = 1.7});
    canvas.stroke(front, 0x171d20ff, {.width = 2.2});
    canvas.stroke(front, 0xbec7c8ff, {.width = 1.25});
    canvas.saved([&] {
        canvas.translate(-.15, -.25);
        canvas.stroke(front, 0xf1e6ceaa, {.width = .45});
    });
}
}  // namespace

struct MechanicalRenderer::Impl {
    mechanicalart::Artwork art;

    void sprite(Canvas& canvas, const char* slot, const char* part, Rect bounds, double sceneScale) {
        const auto source = hangingPart(part);
        const double scale = std::min(bounds.width / source.visible.width, bounds.height / source.visible.height);
        if (auto image =
                art.material(canvas, slot, std::max(source.size.width, source.size.height) * scale * sceneScale)) {
            registeredImage(canvas, *image, source, bounds);
        }
    }

    void clock(Canvas& canvas, const Model& model, const Appearance& appearance, double sceneScale) {
        const bool compact = model.size().height < 150;
        const auto source = hangingPart(compact ? "spring-card-short" : "spring-card-tall");
        for (size_t i = 0; i < model.pieces().size(); ++i) {
            const auto& piece = model.pieces()[i];
            const auto a = model.anchor(piece), b = model.attachment(piece);
            spring(canvas, {a.x * 100, a.y * 100}, {b.x * 100, b.y * 100});
            const auto state = model.state(i);
            canvas.saved([&] {
                canvas.translate(state.position.x * 100, state.position.y * 100);
                canvas.rotate(state.angle * 180 / std::numbers::pi);
                const double scale = piece.half.width * 200 / source.visible.width;
                const Point eye = compact ? Point{470, 220} : Point{366, 335};
                if (const auto image = art.material(
                        canvas, "card", std::max(source.size.width, source.size.height) * scale * sceneScale)) {
                    canvas.image(*image,
                                 {-eye.x * scale, -piece.half.height * 100 - eye.y * scale, source.size.width * scale,
                                  source.size.height * scale},
                                 {.fit = cybercanvas::ImageFit::Stretch, .clip = false});
                } else {
                    canvas.gradientBox({-20, -piece.half.height * 100, 40, piece.half.height * 200}, 7, 0x666d70ff,
                                       0x151a1dff);
                }
                const double faceCenter = compact ? (938 - eye.y) * scale - piece.half.height * 100
                                                  : (1160 - eye.y) * scale - piece.half.height * 100;
                cybercanvas::TextStyle style;
                style.family = L"Bahnschrift";
                style.size = compact ? 43 : 57;
                style.weight = cybercanvas::FontWeight::SemiBold;
                style.align = cybercanvas::TextAlign::Center;
                style.vertical = cybercanvas::VerticalAlign::Center;
                style.tabularFigures = true;
                style.wrap = false;
                const auto digits = model.digits().empty() ? L"000000" : model.digits();
                canvas.text(std::wstring(1, digits[i]), style, {-17, faceCenter - 45, 34, 90}, mechanicalart::kIvory);
            });
        }
        sprite(canvas, "beam", "spring-beam", {5, 0, 350, 30}, sceneScale);
        const double y = compact ? 79 : 152;
        for (double x : {120.5, 232.5}) {
            for (double offset : {-6., 6.}) {
                canvas.oval(x, y + offset, 2.5, 2.5, 0x775735ff);
                canvas.oval(x - .3, y + offset - .5, 1.7, 1.7, 0xe6c581ff);
            }
        }
        if (appearance.twelve) {
            mechanicalart::text(canvas, model.afternoon() ? L"PM" : L"AM", {165, model.size().height - 11, 30, 10}, 7);
        }
    }

    void chime(Canvas& canvas, const Model& model, double sceneScale) {
        cord(canvas, {27, 52}, {130, 10}, 1);
        cord(canvas, {130, 10}, {233, 52}, 1);
        canvas.oval(130, 9, 6, 6, 0x513d21ff, 2);
        canvas.oval(130, 8, 5.7, 5.7, 0xdcb879ff, 1.5);
        sprite(canvas, "beam", "chime-beam", {25, 34, 210, 39}, sceneScale);
        const char* slots[]{"moon", "bell", "star", "leaf", "tag", "leaf"};
        const char* parts[]{"chime-moon", "chime-bell", "chime-star", "chime-leaf", "chime-tag", "chime-leaf"};
        for (size_t i = 0; i < model.pieces().size(); ++i) {
            const auto& piece = model.pieces()[i];
            const auto from = model.anchor(piece), to = model.attachment(piece);
            const Point a{from.x * 100, from.y * 100}, b{to.x * 100, to.y * 100};
            cord(canvas, a, b);
            const auto state = model.state(i);
            const auto index = piece.art;
            canvas.saved([&] {
                canvas.translate(state.position.x * 100, state.position.y * 100);
                canvas.rotate(state.angle * 180 / std::numbers::pi);
                const auto source = hangingPart(parts[index]);
                const double scale = piece.half.height * 200 / source.visible.height;
                const Point eye = index == 0   ? Point{658, 82}
                                  : index == 1 ? Point{627, 181}
                                  : index == 2 ? Point{627, 144}
                                  : index == 4 ? Point{474, 224}
                                               : Point{433, 244};
                if (const auto image = art.material(
                        canvas, slots[index], std::max(source.size.width, source.size.height) * scale * sceneScale)) {
                    canvas.image(*image,
                                 {-eye.x * scale, -piece.half.height * 100 - eye.y * scale, source.size.width * scale,
                                  source.size.height * scale},
                                 {.fit = cybercanvas::ImageFit::Stretch, .clip = false});
                } else {
                    canvas.oval(0, 0, piece.half.width * 100, piece.half.height * 100, mechanicalart::kBrass);
                }
            });
            canvas.oval(b.x, b.y - 3, 2, 3, 0x795936ff, 1.3);
            canvas.oval(b.x - .2, b.y - 3.2, 1.7, 2.7, 0xe6c17aff, .7);
        }
    }
};

MechanicalRenderer::MechanicalRenderer() : impl_(std::make_unique<Impl>()) {}

MechanicalRenderer::~MechanicalRenderer() = default;

void MechanicalRenderer::bind(const CdCreateContext& context, std::function<void()> invalidate) {
    impl_->art.onChange(std::move(invalidate));
    impl_->art.bind(context);
}

void MechanicalRenderer::pause(bool paused) {
    impl_->art.pause(paused);
}

bool MechanicalRenderer::pending() const {
    return impl_->art.pending();
}

void MechanicalRenderer::draw(Canvas& canvas, const Model& model, const Appearance& appearance, double scale) {
    if (model.kind() == Kind::Clock) {
        impl_->clock(canvas, model, appearance, scale);
    } else {
        impl_->chime(canvas, model, scale);
    }
}
}  // namespace toys
