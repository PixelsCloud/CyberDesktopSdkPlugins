#include <cmath>
#include <numbers>

#include "plugins/deep-whale/Skin.h"
#include "plugins/deep-whale/controllers/hourglass/MechanicalHourglass.h"
using whaleskin::Canvas;
using whaleskin::Skin;

namespace deep_hourglass {
Renderer::Renderer(const CdCreateContext& context) : skin_(std::make_unique<Skin>()) {
    skin_->bind(context);
}

Renderer::~Renderer() = default;

void Renderer::onChange(std::function<void()> callback) {
    skin_->onChange(std::move(callback));
}

void Renderer::pause(bool value) {
    skin_->pause(value);
}

bool Renderer::ready() const {
    return skin_->ready();
}

uint64_t Renderer::cacheBuilds() const {
    return skin_->cacheBuilds();
}

bool Renderer::save(const std::filesystem::path& path, const CdFrame& frame) {
    return skin_->save(path, frame);
}

bool Renderer::render(bool timer, const Model& model, const PhysicalSand* sand, const cdsdk::Settings& settings,
                      const CdFrame& frame, CdRect safe) {
    skin_->appearance(settings);
    return skin_->render(frame, safe, [&](auto& canvas) {
        const auto r = skin_->region("glass");
        canvas.clipped(r, [&] {
            canvas.target->Clear(D2D1::ColorF(0, 0));
        });
        // Repaint the live glass cavity as code-native glass. The immutable artwork's demonstration sand is excluded.
        canvas.saved([&] {
            const double centerY = r.y + r.height / 2;
            const double projection = std::cos(model.angle() * std::numbers::pi / 180);
            canvas.translate(0, centerY);
            canvas.scale(1, std::abs(projection) < .005 ? .005 : projection);
            canvas.translate(0, -centerY);
            auto vessel = canvas.path();
            const auto p = [&](double x, double y) {
                return whaleskin::Point{r.x + r.width * x, r.y + r.height * y};
            };
            vessel.moveTo(p(.10, 0))
                .lineTo(p(.90, 0))
                .cubicTo(p(1.12, .20), p(1.00, .29), p(.64, .43))
                .cubicTo(p(.55, .47), p(.52, .49), p(.50, .50))
                .cubicTo(p(.48, .51), p(.45, .53), p(.36, .57))
                .cubicTo(p(0, .71), p(-.12, .80), p(.10, 1))
                .lineTo(p(.90, 1))
                .cubicTo(p(1.12, .80), p(1, .71), p(.64, .57))
                .cubicTo(p(.55, .53), p(.52, .51), p(.50, .50))
                .cubicTo(p(.48, .49), p(.45, .47), p(.36, .43))
                .cubicTo(p(0, .29), p(-.12, .20), p(.10, 0))
                .close();
            const D2D1_GRADIENT_STOP stops[]{{0, cybercanvas::color(0xe9f7ffff)},
                                             {.35f, cybercanvas::color(0xa9ceeaff)},
                                             {.7f, cybercanvas::color(0x7fa8d1ff)},
                                             {1, cybercanvas::color(0xd3eaf6ff)}};
            const auto glass = canvas.linearGradient({r.x, r.y}, {r.x + r.width, r.y + r.height}, stops);
            canvas.target->FillGeometry(vessel.geometry(canvas.factory.Get()), glass.Get());
            canvas.stroke(vessel, 0xf6fcffee, {.width = std::max(.4, r.width * .015)});
            canvas.saved([&] {
                canvas.translate(r.x + r.width / 2, r.y + r.height / 2);

                if (!skin_->textured()) {
                    auto path = canvas.path();
                    path.moveTo({-r.width * .43, -r.height * .47});
                    path.cubicTo({-r.width * .50, -r.height * .15}, {-r.width * .04, -r.height * .08},
                                 {-r.width * .035, 0});
                    path.cubicTo({-r.width * .04, r.height * .08}, {-r.width * .50, r.height * .15},
                                 {-r.width * .43, r.height * .47});
                    canvas.stroke(path, whaleskin::kSilver, {.width = r.width * .025});
                    auto other = canvas.path();
                    other.moveTo({r.width * .43, -r.height * .47});
                    other.cubicTo({r.width * .50, -r.height * .15}, {r.width * .04, -r.height * .08},
                                  {r.width * .035, 0});
                    other.cubicTo({r.width * .04, r.height * .08}, {r.width * .50, r.height * .15},
                                  {r.width * .43, r.height * .47});
                    canvas.stroke(other, whaleskin::kSilver, {.width = r.width * .025});
                }
                const auto dot = [&](double x, double y) {
                    const auto tint = uint32_t(settings.at("sand_color").integer);
                    const double glow = settings.at("glow").integer / 100.;
                    if (glow > 0) {
                        canvas.oval(x, y, std::max(.25, r.width * .011), std::max(.25, r.width * .011),
                                    (tint & 0xffffff00) | uint32_t(glow * 90));
                    }
                    canvas.oval(x, y, std::max(.25, r.width * .019), std::max(.25, r.width * .019),
                                uint32_t(settings.at("sand_color").integer));
                };
                if (sand) {
                    for (const auto& grain : sand->sample()) {
                        const auto p =
                            mechanicalhourglass::grainPosition(grain, {-r.width / 2, -r.height / 2, r.width, r.height});
                        dot(p.x, p.y);
                    }
                } else {
                    const auto state = model.sample();
                    for (const auto* grains : {&state.upper, &state.lower, &state.falling}) {
                        for (const auto& grain : *grains) {
                            dot((grain.x / 18. - .5) * r.width * .86, (grain.y / 28. - .5) * r.height * .90);
                        }
                    }
                }
            });
        });
        if (timer) {
            const auto text = remainingText(model.remaining());
            const auto title = skin_->region("title");
            const auto configured = uint32_t(settings.at("text_color").integer);
            // Keep the selected sand-gold ink legible on an enamel inset. Custom
            // colors retain their original ivory background and settings identity.
            if (configured == 0xffe3a6ff) {
                canvas.gradientBox(title, 3, whaleskin::kInk, 0x234d78ff);
                canvas.strokeBox(title, 3, whaleskin::kRuby, {.width = .6});
            }
            skin_->label(canvas, std::wstring(text.begin(), text.end()), title, title.height * .95, configured);
            skin_->progress(canvas, skin_->region("progress"), 1 - model.progress());
        }
    });
}
}  // namespace deep_hourglass
