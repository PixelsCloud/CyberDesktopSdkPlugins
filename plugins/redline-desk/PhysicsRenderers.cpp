#include <cmath>
#include <numbers>

#include "controllers/beat/Machine.h"
#include "controllers/marble/Clock.h"
#include "controllers/precision/Clock.h"
#include "controllers/sign/Sign.h"
#include "controllers/toys/Toys.h"
#include "plugins/redline-desk/Skin.h"

namespace {
using redline::Canvas;
using redline::Point;
using redline::Skin;

void rail(Canvas& canvas, Point a, Point b, double width = 5) {
    canvas.line(a.x, a.y + 1.5, b.x, b.y + 1.5, 0x120b10cc, width + 7);
    canvas.line(a.x, a.y, b.x, b.y, 0x655043ff, width + 4);
    canvas.line(a.x, a.y - 1, b.x, b.y - 1, 0xedd6acff, width + 1);
    canvas.line(a.x, a.y, b.x, b.y, 0x9c8a79ff, width * .72);
    canvas.line(a.x, a.y + width * .15, b.x, b.y + width * .15, 0x3e2630ff, width * .40);
}

void ball(Canvas& canvas, Point point, double radius) {
    const D2D1_GRADIENT_STOP stops[]{{0, cybercanvas::color(0xf2f6dbff)},
                                     {.24f, cybercanvas::color(0xc9e0c7ff)},
                                     {.48f, cybercanvas::color(0x9ac0a6ff)},
                                     {.7f, cybercanvas::color(0xc3ddbeff)},
                                     {1, cybercanvas::color(0x4e7864ff)}};
    const auto brush =
        canvas.radialGradient({point.x - radius * .3, point.y - radius * .35}, {radius * 1.5, radius * 1.5}, stops);
    canvas.target->FillEllipse(
        D2D1::Ellipse(D2D1::Point2F(float(point.x), float(point.y)), float(radius), float(radius)), brush.Get());
    canvas.oval(point.x, point.y, radius, radius, 0x7ca285ff, std::max(.45, radius * .05));
    canvas.oval(point.x - radius * .28, point.y - radius * .32, radius * .23, radius * .17, 0xffffffdd);
}

void coil(Canvas& canvas, Point from, Point to, double amplitude = 2.8) {
    const double dx = to.x - from.x, dy = to.y - from.y, length = std::hypot(dx, dy);
    if (length < .01) {
        return;
    }
    auto path = canvas.path();
    path.moveTo(from);
    const unsigned turns = unsigned(std::clamp(std::round(length / 4), 4., 24.)), segments = turns * 12;
    for (unsigned i = 1; i <= segments; ++i) {
        const double t = double(i) / segments, side = std::sin(t * turns * 2 * std::numbers::pi) * amplitude;
        path.lineTo({from.x + dx * t + dy / length * side, from.y + dy * t - dx / length * side});
    }
    canvas.stroke(path, 0x261019ff, {.width = std::max(3.8, amplitude * .50)});
    canvas.stroke(path, 0xb09a80ff, {.width = std::max(2.5, amplitude * .32)});
    canvas.stroke(path, 0xf7e9cfff, {.width = std::max(.7, amplitude * .10)});
}
}  // namespace

namespace redline_toys {
MechanicalRenderer::MechanicalRenderer() : skin_(std::make_unique<Skin>()) {}

MechanicalRenderer::~MechanicalRenderer() = default;

void MechanicalRenderer::bind(const CdCreateContext& context, std::function<void()> changed) {
    skin_->bind(context);
    skin_->onChange(std::move(changed));
}

void MechanicalRenderer::pause(bool value) {
    skin_->pause(value);
}

bool MechanicalRenderer::pending() const {
    return skin_->pending();
}

redline::View MechanicalRenderer::view(CdRect safe, cybercanvas::Size world) {
    return skin_->view(safe, world);
}

void MechanicalRenderer::draw(Canvas& canvas, const Model& model, const Appearance&, double scale) {
    skin_->worldBackdrop(canvas, model.kind() != Kind::Clock);
    const auto size = model.size();
    const double density = std::max(.05, scale * canvas.devicePixelRatio());
    if (density_ != density || artworkRevision_ != skin_->cacheBuilds()) {
        for (auto& card : cards_) {
            card.invalidate();
        }
        density_ = density;
        artworkRevision_ = skin_->cacheBuilds();
    }
    for (size_t i = 0; i < model.pieces().size(); ++i) {
        const auto& piece = model.pieces()[i];
        const auto a = model.anchor(piece), b = model.attachment(piece);
        const auto state = model.state(i);
        const Point from{a.x * 100, a.y * 100}, to{b.x * 100, b.y * 100};
        if (model.kind() == Kind::Clock) {
            const double length = std::hypot(to.x - from.x, to.y - from.y);
            canvas.saved([&] {
                canvas.translate(from.x, from.y);
                canvas.rotate(-std::atan2(to.x - from.x, to.y - from.y) * 180 / std::numbers::pi);
                const double width = skin_->rows() == 1 ? 18 : 22;
                skin_->part(canvas, "spring", {-width / 2, 0, width, length}, true);
            });
        } else {
            canvas.saved([&] {
                canvas.translate(from.x, from.y);
                canvas.rotate(-std::atan2(to.x - from.x, to.y - from.y) * 180 / std::numbers::pi);
                skin_->part(canvas, "chain" + std::to_string(piece.art),
                            {-6.5, 0, 13, std::hypot(to.x - from.x, to.y - from.y)}, true);
            });
        }
        canvas.saved([&] {
            canvas.translate(state.position.x * 100, state.position.y * 100);
            canvas.rotate(state.angle * 180 / std::numbers::pi);
            const double w = piece.half.width * 200, h = piece.half.height * 200;
            if (model.kind() == Kind::Clock) {
                const auto glyph = i < model.digits().size() ? model.digits()[i] : L'0';
                const auto width = uint32_t(std::max(1., std::ceil((w + 8) * density)));
                const auto height = uint32_t(std::max(1., std::ceil((h + 12) * density)));
                if (glyphs_[i] != glyph || !cards_[i].matches(canvas.target.Get(), {width, height, 1})) {
                    canvas.renderLayer(cards_[i], {width, height, 1}, [&](auto& tile) {
                        tile.scale(density, density);
                        tile.translate(w / 2 + 4, h / 2 + 6);
                        skin_->part(tile, "card" + std::to_string(i), {-w / 2, -h / 2, w, h});
                        const bool compact = skin_->rows() == 1;
                        skin_->label(tile, std::wstring(1, glyph),
                                     {-w * .34, -h * (compact ? .32 : .28), w * .68, h * .64},
                                     h * (compact ? .72 : .54), redline::kIvory);
                    });
                    glyphs_[i] = glyph;
                }
                canvas.target->DrawBitmap(
                    cards_[i].bitmap(),
                    D2D1::RectF(float(-w / 2 - 4), float(-h / 2 - 6), float(w / 2 + 4), float(h / 2 + 6)), 1,
                    D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                canvas.oval(0, -h / 2 - 3, 3.5, 3.5, redline::kSilver, 1.5);
            } else {
                skin_->part(canvas, "charm" + std::to_string(piece.art), {-w / 2, -h / 2, w, h});
            }
        });
    }
    if (model.kind() == Kind::Clock && model.pieces().size() == 6) {
        for (const size_t split : {size_t(1), size_t(3)}) {
            const auto left = model.state(split), right = model.state(split + 1);
            const double x = (left.position.x + right.position.x) * 50;
            const double y = (left.position.y + right.position.y) * 50;
            canvas.oval(x, y - 4, 1.1, 1.1, redline::kRuby);
            canvas.oval(x, y + 4, 1.1, 1.1, redline::kRuby);
        }
    }
}
}  // namespace redline_toys

namespace redline_marble {
Renderer::Renderer(const std::filesystem::path& assets, bool) : skin_(std::make_unique<Skin>(assets)) {}

cybercanvas::Rect Renderer::control(std::string_view name, cybercanvas::Size world) const {
    return skin_->worldRegion(name, world);
}

bool Renderer::pollAssets() {
    return skin_->poll();
}

bool Renderer::pendingAssets() const {
    return skin_->pending();
}

void Renderer::pauseAssets(bool value) {
    skin_->pause(value);
}

cdsdk::ImageSlotStats Renderer::assetStats() const {
    return {};
}

redline::View Renderer::view(CdRect safe, cybercanvas::Size world) {
    return skin_->view(safe, world);
}

void Renderer::draw(Canvas& canvas, const Model& model, Appearance, double) {
    skin_->worldBackdrop(canvas, false);
    const auto& shape = model.shape();
    for (const auto& marble : model.marbles()) {
        const auto state = model.state(marble);
        ball(canvas, {state.position.x * kPixelsPerMeter, state.position.y * kPixelsPerMeter}, kRadius);
    }
    skin_->artLayer(canvas, [&](auto& art) {
        unsigned digit = 0;
        for (const auto glyph : model.timeText()) {
            if (glyph >= L'0' && glyph <= L'9') {
                const auto r = skin_->region("digit" + std::to_string(digit++));
                skin_->label(art, std::wstring(1, glyph), r, r.height * .83);
            }
        }
    });
}
}  // namespace redline_marble

namespace redline_beat {
Renderer::Renderer(const std::filesystem::path& assets, bool) : skin_(std::make_unique<Skin>(assets)) {}

bool Renderer::pollAssets() {
    return skin_->poll();
}

bool Renderer::pendingAssets() const {
    return skin_->pending();
}

void Renderer::pauseAssets(bool value) {
    skin_->pause(value);
}

cdsdk::ImageSlotStats Renderer::assetStats() const {
    return {};
}

redline::View Renderer::view(CdRect safe, cybercanvas::Size world) {
    return skin_->view(safe, world);
}

void Renderer::draw(Canvas& canvas, const Model& model, const Signal&, Appearance, double) {
    skin_->worldBackdrop(canvas, false);
    for (size_t i = 0; i < 4; ++i) {
        const auto& piston = model.pistons()[i];
        const auto state = model.state(piston.body);
        const Point p{state.position.x * kUnits, state.position.y * kUnits};
        skin_->springPart(canvas, "spring" + std::to_string(i), {p.x - piston.rest.x, p.y - piston.rest.y});
        skin_->movingPart(canvas, "base" + std::to_string(i));
        skin_->movingPart(canvas, "cap" + std::to_string(i), {p.x - piston.rest.x, p.y - piston.rest.y});
    }
    const auto& launcher = model.pistons()[4];
    const auto launcherState = model.state(launcher.body);
    skin_->movingPart(
        canvas, "launcher",
        {launcherState.position.x * kUnits - launcher.rest.x, launcherState.position.y * kUnits - launcher.rest.y});
    for (size_t i = 0; i < model.paddles().size(); ++i) {
        const auto& paddle = model.paddles()[i];
        const auto state = model.state(paddle.body);
        skin_->movingPart(canvas, "paddle" + std::to_string(i),
                          {state.position.x * kUnits - paddle.rest.x, state.position.y * kUnits - paddle.rest.y},
                          (state.angle + .30) * 180 / std::numbers::pi, "paddlePivot" + std::to_string(i));
    }
    for (size_t i = 0; i < model.bumpers().size(); ++i) {
        if (model.flashes()[i] > 0) {
            const auto p = model.bumpers()[i];
            canvas.oval(p.x, p.y, model.compact() ? 12 : 15, model.compact() ? 12 : 15, 0xffa5bd77);
        }
    }
    for (const auto& value : model.balls()) {
        const auto state = model.state(value.body);
        ball(canvas, {state.position.x * kUnits, state.position.y * kUnits}, kRadius);
    }
}
}  // namespace redline_beat

namespace redline_precision {
Renderer::Renderer(Kind, const std::filesystem::path& assets) : skin_(std::make_unique<Skin>(assets)) {}

bool Renderer::poll() {
    return skin_->poll();
}

bool Renderer::pending() const {
    return skin_->pending();
}

void Renderer::pause(bool value) {
    skin_->pause(value);
}

cdsdk::ImageSlotStats Renderer::assets() const {
    return {};
}

redline::View Renderer::view(CdRect safe, cybercanvas::Size world) {
    return skin_->view(safe, world);
}

void Renderer::draw(Canvas& canvas, const State& state, const Options& options, double) {
    skin_->worldBackdrop(canvas, false);
    const bool light = skin_->folder().starts_with("moon-");
    skin_->artLayer(canvas, [&](auto& art) {
        if (light) {
            const auto r = skin_->region("moon");
            const double cx = r.x + r.width / 2, cy = r.y + r.height * .45, radius = r.height * .34;
            auto aperture = art.path();
            aperture.moveTo({r.x, r.y + r.height})
                .cubicTo({r.x, r.y - r.height * .35}, {r.x + r.width, r.y - r.height * .35},
                         {r.x + r.width, r.y + r.height})
                .cubicTo({r.x + r.width * .92, r.y + r.height * .55}, {r.x + r.width * .67, r.y + r.height * .55},
                         {cx, r.y + r.height * .85})
                .cubicTo({r.x + r.width * .33, r.y + r.height * .55}, {r.x + r.width * .08, r.y + r.height * .55},
                         {r.x, r.y + r.height})
                .close();
            art.masked(aperture, [&] {
                art.gradientBox(r, 0, 0x13263bff, 0x070e19ff);
                if (!options.detail) {
                    return;
                }
                art.oval(cx, cy, radius, radius, 0x243447ff);
                const double cosine = std::cos(state.lunarPhase * 2 * std::numbers::pi);
                const bool waxing = state.lunarPhase < .5;
                auto lit = art.path();
                for (int i = 0; i <= 60; ++i) {
                    const double y = -radius + 2 * radius * i / 60;
                    const double x = std::sqrt(std::max(0., radius * radius - y * y));
                    const Point p{cx + (waxing ? x : -x), cy + y};
                    if (i) {
                        lit.lineTo(p);
                    } else {
                        lit.moveTo(p);
                    }
                }
                for (int i = 60; i >= 0; --i) {
                    const double y = -radius + 2 * radius * i / 60;
                    const double x = std::sqrt(std::max(0., radius * radius - y * y));
                    lit.lineTo({cx + (waxing ? cosine * x : -cosine * x), cy + y});
                }
                lit.close();
                art.fill(lit, redline::kIvory);
            });
        }
        if (light || skin_->folder().starts_with("dive-")) {
            const auto r = skin_->region("date");
            skin_->clear(art, r, redline::kInk, true);
            skin_->label(art, std::to_wstring(state.day), r, r.height * .83, redline::kIvory, false);
        } else if (skin_->folder().starts_with("skeleton-")) {
            constexpr std::array<unsigned, 7> teeth{45, 60, 66, 34, 22, 24, 20};
            for (unsigned i = 0; i < teeth.size(); ++i) {
                const auto r = skin_->region("gear" + std::to_string(i));
                art.oval(r.x + r.width / 2, r.y + r.height / 2, r.width / 2, r.height / 2, redline::kInk);
            }
            for (unsigned i : {4u, 5u, 6u, 0u, 1u, 2u, 3u}) {
                const auto r = skin_->region("gear" + std::to_string(i));
                if (r.width <= 0 || r.height <= 0) {
                    continue;
                }
                art.saved([&] {
                    art.translate(r.x + r.width / 2, r.y + r.height / 2);
                    if (options.detail) {
                        art.rotate(gearAngle(state.mechanismSeconds, teeth[i], i % 2));
                    }
                    auto mask = art.path();
                    mask.moveTo({r.width / 2, 0})
                        .arcTo({-r.width / 2, 0}, {r.width / 2, r.height / 2})
                        .arcTo({r.width / 2, 0}, {r.width / 2, r.height / 2})
                        .close();
                    art.masked(mask, [&] {
                        skin_->part(art, "gear" + std::to_string(i), {-r.width / 2, -r.height / 2, r.width, r.height});
                    });
                });
            }
            // The engraved bridge and jewel bearings sit above the moving wheels.
            skin_->part(art, "gearBridge", skin_->region("gearBridge"));
        }
    });
    const auto hand = [&](double turns, double length, double width, uint32_t color) {
        const double a = turns * 2 * std::numbers::pi;
        canvas.saved([&] {
            canvas.translate(500, 500);
            canvas.rotate(a * 180 / std::numbers::pi);
            auto shape = canvas.path();
            shape.moveTo({0, -length})
                .lineTo({width * .70, -length * .69})
                .lineTo({width * .33, -length * .49})
                .lineTo({width * .33, 48})
                .lineTo({-width * .33, 48})
                .lineTo({-width * .33, -length * .49})
                .lineTo({-width * .70, -length * .69})
                .close();
            canvas.fill(shape, color);
            canvas.stroke(shape, redline::kSilver, {.width = 3});
            canvas.line(0, 20, 0, -length * .84, 0xffffffff, width * .14);
        });
    };
    const bool diver = skin_->folder().starts_with("dive-");
    hand(state.hour / 360, 255, diver ? 34 : 20, redline::kIvory);
    hand(state.minute / 360, 380, diver ? 22 : 12, redline::kIvory);
    if (options.seconds) {
        hand(state.second / 360, 400, 3, redline::kRuby);
    }
    canvas.oval(500, 500, 20, 20, 0x151a1eff);
    canvas.oval(500, 500, 20, 20, redline::kSilver, 5);
    canvas.oval(500, 500, 9, 9, redline::kRuby);
}
}  // namespace redline_precision

namespace redline_sign {
Renderer::Renderer(const std::filesystem::path& assets) : skin_(std::make_unique<Skin>(assets)) {}

bool Renderer::poll() {
    return skin_->poll();
}

bool Renderer::pending() const {
    return skin_->pending();
}

void Renderer::pause(bool value) {
    skin_->pause(value);
}

cdsdk::ImageSlotStats Renderer::assets() const {
    return {};
}

redline::View Renderer::view(CdRect safe, cybercanvas::Size world) {
    return skin_->view(safe, world);
}

void Renderer::draw(Canvas& canvas, const Model& model, bool, double, Format format, double aspect) {
    skin_->worldBackdrop(canvas, false);
    // This renderer uses the model's native logical plane while the registered
    // plot region supplies the live split-flap cells and shared hit region.
    const auto shape = redlineGeometry();
    const double unit = shape.width / kSlots;
    for (size_t i = 0; i < kSlots; ++i) {
        const double progress = model.progress(i);
        const auto& before = model.before()[i];
        const auto& after = model.after()[i];
        const redline::Rect r{unit * i + unit * .08, shape.height * .08, unit * .84, shape.height * .84};
        skin_->clear(canvas, r, 0x170e14ff);
        canvas.strokeBox(r, 2, redline::kSilver, {.width = 1.5});
        const bool falling = progress > 0 && progress < 1;
        const double squeeze = falling ? std::max(.08, std::abs(std::cos(progress * std::numbers::pi))) : 1;
        auto text = progress >= .5 ? after : before;
        std::transform(text.begin(), text.end(), text.begin(), [](wchar_t character) {
            return character >= L'a' && character <= L'z' ? wchar_t(character - L'a' + L'A') : character;
        });
        skin_->label(canvas, text, {r.x, r.y + r.height * (1 - squeeze) / 2, r.width, r.height * squeeze},
                     r.height * .79 * squeeze, redline::kIvory, false);
        canvas.line(r.x, r.y + r.height / 2, r.x + r.width, r.y + r.height / 2, 0x735963ff, 1.6);
    }
}
}  // namespace redline_sign
