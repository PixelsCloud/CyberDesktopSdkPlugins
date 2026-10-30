#include <memory>
#include <utility>

#include "plugins/widget-suite/AlbumArtwork.h"
#include "plugins/widget-suite/ThemedController.h"
#include "plugins/widget-suite/Widget.h"

namespace widgetplugin {
namespace {
class View final : public themed::View {
public:
    explicit View(Spec);
    void bind(const CdCreateContext&, std::function<void(uint32_t)>) override;
    void input(const CdInput&) override;
    void cancelInput() override;
    void pause(bool) override;
    void syncInput(Spec, const Media&, CdRect, const Photo&, AlbumPlayback) override;
    bool render(Spec, const cdsdk::Settings&, const Spectrum&, const Photo&, const Photo&, float, Date, int,
                const CdFrame&, CdRect, const Media&, int, const CdInputStats&, const CounterDrums*,
                AlbumPlayback) override;
    cybercanvas::Rect photoRegion(CdRect) const override;
    bool photoHit(CdRect, const cdsdk::Settings&, const CdInput&) const override;

private:
    Canvas canvas_;
    bool mechanical_;
};

View::View(Spec spec) : mechanical_(spec.mechanical) {}

void View::bind(const CdCreateContext& context, std::function<void(uint32_t)> action) {
    canvas_.bind(context, std::move(action));
}

void View::input(const CdInput& input) {
    canvas_.input(input);
}

void View::cancelInput() {
    canvas_.cancelInput();
}

void View::pause(bool paused) {
    canvas_.pause(paused);
}

void View::syncInput(Spec spec, const Media& media, CdRect safe, const Photo& photo, AlbumPlayback album) {
    canvas_.syncInput(spec, media, safe, photo, album);
}

bool View::render(Spec spec, const cdsdk::Settings& settings, const Spectrum& spectrum, const Photo& photo,
                  const Photo& previous, float fade, Date date, int month, const CdFrame& frame, CdRect safe,
                  const Media& media, int pressed, const CdInputStats& stats, const CounterDrums* drums,
                  AlbumPlayback album) {
    return canvas_.render(spec, settings, spectrum, photo, previous, fade, date, month, frame, safe, media, pressed,
                          stats, drums, album);
}

cybercanvas::Rect View::photoRegion(CdRect safe) const {
    return mechanical_ ? arrangeAlbum(safe.width, safe.height).photo
                       : cybercanvas::Rect{1, 1, safe.width - 4, safe.height - 5};
}

bool View::photoHit(CdRect safe, const cdsdk::Settings& settings, const CdInput& input) const {
    return themed::albumPhotoHit(safe, settings.at("counter").integer != 0, input);
}

std::unique_ptr<themed::View> makeView(Spec spec) {
    return std::make_unique<View>(spec);
}
}  // namespace

cdsdk::Settings defaults(Kind kind, bool mechanical) {
    return themed::defaults(kind, mechanical, {0x11CADFFF, 0x46E9B1FF, false});
}

CdResult create(Spec spec, const CdCreateContext* context, CdInstanceApi* out, uint32_t size) {
    return themed::create(spec, context, out, size, makeView, {0x11CADFFF, 0x46E9B1FF, false});
}
}  // namespace widgetplugin
