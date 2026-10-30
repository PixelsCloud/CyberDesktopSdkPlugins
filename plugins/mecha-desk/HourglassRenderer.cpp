#include <cmath>
#include <numbers>

#include "controllers/hourglass/MechanicalHourglass.h"
#include "plugins/mecha-desk/Skin.h"
using aerisskin::Canvas;
using aerisskin::Skin;

namespace aeris_hourglass {
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

unsigned Renderer::hit(CdRect safe, cybercanvas::Point point) const {
    for (unsigned i = 0; i < 2; ++i) {
        const auto r = skin_->displayRegion(i ? "reset" : "toggle", safe);
        if (point.x >= r.x && point.y >= r.y && point.x < r.x + r.width && point.y < r.y + r.height) {
            return i + 1;
        }
    }
    return 0;
}

bool Renderer::render(bool timer, const Model& model, const PhysicalSand* sand, const cdsdk::Settings& settings,
                      const CdFrame& frame, CdRect safe) {
    skin_->appearance(settings);
    return skin_->render(frame, safe, [&](auto& canvas) {
        const auto r = skin_->region("glass");
        // The approved glass reflections remain separate from live sand. Warm
        // demonstration grains are colour-keyed from this cached material only.
        canvas.saved([&] {
            const double centerY = r.y + r.height / 2;
            const double projection = std::cos(model.angle(true) * std::numbers::pi / 180);
            canvas.translate(0, centerY);
            canvas.scale(1, std::abs(projection) < .005 ? .005 : projection);
            canvas.translate(0, -centerY);
            auto vessel = canvas.path();
            const auto p = [&](double x, double y) {
                return aerisskin::Point{r.x + r.width * x, r.y + r.height * y};
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
            canvas.saved([&] {
                canvas.translate(r.x + r.width / 2, r.y + r.height / 2);

                if (!skin_->textured()) {
                    auto path = canvas.path();
                    path.moveTo({-r.width * .43, -r.height * .47});
                    path.cubicTo({-r.width * .50, -r.height * .15}, {-r.width * .04, -r.height * .08},
                                 {-r.width * .035, 0});
                    path.cubicTo({-r.width * .04, r.height * .08}, {-r.width * .50, r.height * .15},
                                 {-r.width * .43, r.height * .47});
                    canvas.stroke(path, aerisskin::kSilver, {.width = r.width * .025});
                    auto other = canvas.path();
                    other.moveTo({r.width * .43, -r.height * .47});
                    other.cubicTo({r.width * .50, -r.height * .15}, {r.width * .04, -r.height * .08},
                                  {r.width * .035, 0});
                    other.cubicTo({r.width * .04, r.height * .08}, {r.width * .50, r.height * .15},
                                  {r.width * .43, r.height * .47});
                    canvas.stroke(other, aerisskin::kSilver, {.width = r.width * .025});
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
        canvas.saved([&] {
            const double centerY = r.y + r.height / 2;
            // Grain coordinates are mirrored at the boundary; glass reflections
            // keep their accumulated orientation instead of resetting the bitmap.
            const double projection =
                (model.cycles() % 2 ? -1 : 1) * std::cos(model.angle(true) * std::numbers::pi / 180);
            canvas.translate(0, centerY);
            canvas.scale(1, std::abs(projection) < .005 ? .005 : projection);
            canvas.translate(0, -centerY);
            skin_->part(canvas, "glass", r, true);
        });
        if (timer || skin_->terminal()) {
            const auto text = remainingText(model.remaining());
            const auto title = skin_->region("title");
            skin_->label(canvas, std::wstring(text.begin(), text.end()), title, title.height * .77,
                         timer ? uint32_t(settings.at("text_color").integer) : skin_->foreground(aerisskin::kInk));
            skin_->progress(canvas, skin_->region("progress"), 1 - model.progress());
            if (timer || skin_->columns() > 1) {
                skin_->button(canvas, skin_->region("toggle"), settings.at("running").integer ? "pause" : "play");
            }
            if (timer) {
                skin_->button(canvas, skin_->region("reset"), "rotate-ccw");
            }
        }
    });
}
}  // namespace aeris_hourglass
