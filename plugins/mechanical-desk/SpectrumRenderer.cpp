#include <algorithm>

#include "plugins/mechanical-desk/Skin.h"
#include "plugins/mechanical-desk/controllers/widget/Widget.h"

namespace mechanicaldesk_widget {
using mechanicaldesk::Skin;

Canvas::Canvas() : skin_(std::make_unique<Skin>()) {}

Canvas::~Canvas() = default;

void Canvas::bind(const CdCreateContext& context, std::function<void(uint32_t)> action) {
    skin_->bind(context, std::move(action));
}

void Canvas::pause(bool paused) {
    skin_->pause(paused);
}

void Canvas::input(const CdInput& event) {
    skin_->input(event);
}

void Canvas::cancelInput() {
    skin_->cancel();
}

void Canvas::syncInput(Spec, const Media&, CdRect safe, const Photo&, AlbumPlayback) {
    skin_->controls(safe, {});
}

bool Canvas::pendingImages() const {
    return skin_->pending();
}

bool Canvas::savePng(const std::wstring& path, const CdFrame& frame) {
    return skin_->save(path, frame);
}

bool Canvas::render(Spec, const cdsdk::Settings& settings, const Spectrum& spectrum, const Photo&, const Photo&, float,
                    Date, int, const CdFrame& frame, CdRect safe, const Media&, int, const CdInputStats&,
                    const widgetplugin::CounterDrums*, AlbumPlayback) {
    return skin_->render(frame, safe, [&](auto& canvas) {
        const auto area = skin_->region("plot");
        const unsigned count = skin_->columns() * 8 - 1;
        const double pitch = area.width / count, diameter = std::min(pitch * .65, area.height * .16);
        for (unsigned i = 0; i < count; ++i) {
            const double p = double(i) * (Spectrum::count - 1) / (count - 1);
            const double level = std::clamp(double(spectrum.sample(p)), 0., 1.);
            const double peak = std::clamp(double(spectrum.sample(p, true)), 0., 1.);
            const double x = area.x + (i + .5) * pitch;
            const double bottom = area.y + area.height;
            const double h = std::max(2., (area.height - diameter) * level);
            canvas.clipped({x - pitch / 2, bottom - h, pitch, h}, [&] {
                skin_->part(canvas, "rod", {x - pitch * .17, area.y + diameter, pitch * .34, area.height - diameter});
            });
            canvas.box(x - pitch * .26, bottom - 3, pitch * .52, 6, 1, mechanicaldesk::kBrass);
            if (settings.at("peaks").integer) {
                skin_->part(
                    canvas, "ball",
                    {x - diameter / 2, bottom - diameter - (area.height - diameter) * peak, diameter, diameter});
            }
        }
    });
}
}  // namespace mechanicaldesk_widget
