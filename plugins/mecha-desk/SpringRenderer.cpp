#include <cmath>
#include <format>
#include <numbers>

#include "controllers/toys/Toys.h"
#include "plugins/mecha-desk/Skin.h"

namespace {
using aerisskin::Canvas;
using aerisskin::Point;
using aerisskin::Skin;

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
    canvas.stroke(path, 0x24394cff, {.width = std::max(3.8, amplitude * .50)});
    canvas.stroke(path, 0x728690ff, {.width = std::max(2.5, amplitude * .32)});
    canvas.stroke(path, 0xe9eeebff, {.width = std::max(.7, amplitude * .10)});
}
}  // namespace

namespace aeris_toys {
MechanicalRenderer::MechanicalRenderer() : skin_(std::make_unique<Skin>()) {}

MechanicalRenderer::~MechanicalRenderer() = default;

void MechanicalRenderer::bind(const CdCreateContext& context, std::function<void()> changed,
                              std::function<void(uint32_t)> action) {
    skin_->bind(context, std::move(action));
    environment_.bind(context);
    skin_->onChange(std::move(changed));
}

void MechanicalRenderer::pause(bool value) {
    skin_->pause(value);
}

bool MechanicalRenderer::pending() const {
    return skin_->pending();
}

bool MechanicalRenderer::fit(Model& model, CdRect safe) {
    if (model.kind() != Kind::Clock || skin_->rows() != 1) {
        return false;
    }
    skin_->view(safe, model.size());
    const auto plane = skin_->size();
    const auto v = skin_->view(safe, plane);
    const cybercanvas::Rect limits{(safe.x + 6.5 - v.origin.x) / v.scale, (safe.y + 6.5 - v.origin.y) / v.scale + 7,
                                   (safe.width - 13) / v.scale, (safe.height - 13) / v.scale - 7};
    return model.fit(plane, limits, [this](Point point) {
        return skin_->reflow(point);
    });
}

aerisskin::View MechanicalRenderer::view(CdRect safe, cybercanvas::Size world) {
    return skin_->view(safe, world);
}

bool MechanicalRenderer::input(const CdInput& event, CdRect safe) {
    if (!skin_->terminal() || skin_->region("toggle").width <= 0) {
        return false;
    }
    std::vector<cdsdk::Controls::Item> items;
    bool inside = false;
    unsigned id = 1;
    for (const auto name : {"less", "toggle", "more"}) {
        items.push_back({id++, skin_->region(name), name});
        const auto r = skin_->displayRegion(name, safe);
        inside |= event.x >= safe.x + r.x && event.x < safe.x + r.x + r.width && event.y >= safe.y + r.y &&
                  event.y < safe.y + r.y + r.height;
    }
    skin_->controls(safe, items);
    skin_->input(event);
    return inside;
}

void MechanicalRenderer::draw(Canvas& canvas, const Model& model, const Appearance&, double scale,
                              const cdsdk::Settings& settings) {
    skin_->worldBackdrop(canvas, model.kind() != Kind::Clock);
    if (skin_->terminal()) {
        skin_->artLayer(canvas, [&](auto& art) {
            const auto t = cdsdk::Environment::localTime(environment_.snapshot());
            const auto r = skin_->region("readout0");
            if (r.width > 0) {
                skin_->label(art, std::format(L"{:02}", t.tm_hour), {r.x, r.y, r.width, r.height * .44});
                skin_->label(art, std::format(L"{:02}", t.tm_min),
                             {r.x, r.y + r.height * .56, r.width, r.height * .44});
            }
            const auto date = skin_->region("date");
            skin_->label(art,
                         skin_->terminal() ? std::format(L"{:02}/{:02}", t.tm_mon + 1, t.tm_mday)
                                           : std::format(L"{:02}/{:02} {}", t.tm_mon + 1, t.tm_mday, t.tm_year + 1900),
                         date);
            skin_->button(art, skin_->region("less"), "minus");
            skin_->button(art, skin_->region("toggle"), settings.at("running").integer ? "pause" : "play");
            skin_->button(art, skin_->region("more"), "plus");
            skin_->progress(art, skin_->region("wind"), settings.at("wind").number);
        });
    }
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
                coil(canvas, {0, 0}, {0, length}, skin_->rows() == 1 ? 1.2 : 1.8);
            });
        } else {
            canvas.saved([&] {
                canvas.translate(from.x, from.y);
                canvas.rotate(-std::atan2(to.x - from.x, to.y - from.y) * 180 / std::numbers::pi);
                canvas.line(0, 0, 0, std::hypot(to.x - from.x, to.y - from.y), 0x728690ff, .6);
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
                                     h * (compact ? .72 : .54), 0xe9eeebff);
                    });
                    glyphs_[i] = glyph;
                }
                canvas.target->DrawBitmap(
                    cards_[i].bitmap(),
                    D2D1::RectF(float(-w / 2 - 4), float(-h / 2 - 6), float(w / 2 + 4), float(h / 2 + 6)), 1,
                    D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                canvas.oval(0, -h / 2 - 3, 3.5, 3.5, aerisskin::kSilver, 1.5);
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
            canvas.oval(x, y - 4, 1.1, 1.1, aerisskin::kRuby);
            canvas.oval(x, y + 4, 1.1, 1.1, aerisskin::kRuby);
        }
    }
}
}  // namespace aeris_toys
