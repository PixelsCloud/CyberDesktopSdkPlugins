#include <cmath>
#include <numbers>

#include "plugins/mecha-desk/Skin.h"
#include "plugins/mecha-desk/controllers/sign/Sign.h"
using aerisskin::Canvas;
using aerisskin::Skin;

namespace aeris_sign {
Renderer::Renderer(const CdCreateContext& context) : skin_(std::make_unique<Skin>()) {
    skin_->bind(context);
}

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
    return skin_->imageStats();
}

aerisskin::View Renderer::view(CdRect safe, cybercanvas::Size world) {
    return skin_->view(safe, world);
}

void Renderer::draw(Canvas& canvas, const Model& model, bool background, double, Format format, double aspect) {
    if (background) {
        skin_->worldBackdrop(canvas, false);
    }
    // This renderer uses the model's native logical plane while the registered
    // plot region supplies the live split-flap cells and shared hit region.
    const auto shape = deepGeometry();
    size_t slots = kSlots;
    if (skin_->terminal()) {
        while (slots > 1 && model.after()[slots - 1].find_first_not_of(L" ") == std::wstring::npos &&
               (model.progress(slots - 1) >= 1 ||
                model.before()[slots - 1].find_first_not_of(L" ") == std::wstring::npos)) {
            --slots;
        }
    }
    const double unit = shape.width / slots;
    for (size_t i = 0; i < slots; ++i) {
        const double progress = model.progress(i);
        const auto& before = model.before()[i];
        const auto& after = model.after()[i];
        const aerisskin::Rect r{unit * i + unit * .08, shape.height * .08, unit * .84, shape.height * .84};
        canvas.gradientBox(r, 7, 0x17303fff, 0x17303fff);
        canvas.strokeBox(r, 7, 0x6b828aff, {.width = 2.5});
        const bool falling = progress > 0 && progress < 1;
        const double center = r.y + r.height / 2;
        const std::array<std::wstring, 2> references{before, after};
        const auto half = [&](const std::wstring& text, bool upper, double projection = 1.) {
            canvas.clipped({r.x, upper ? r.y : center, r.width, r.height / 2}, [&] {
                canvas.saved([&] {
                    canvas.translate(0, center);
                    canvas.scale(1, std::max(.015, projection));
                    canvas.translate(0, -center);
                    canvas.gradientBox(r, 7, 0x17303fff, 0x17303fff);
                    skin_->label(canvas, text, r, r.height * .94, 0xd9f0e8ff, false, references);
                });
            });
        };
        half(after, true);
        half(falling ? before : after, false);
        if (falling) {
            const bool upper = progress < .5;
            half(upper ? before : after, upper, std::abs(std::cos(progress * std::numbers::pi)));
        }
        canvas.line(r.x, r.y + r.height / 2, r.x + r.width, r.y + r.height / 2, 0x0f222eff, 1.6);
    }
}
}  // namespace aeris_sign
