#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "plugins/widget-suite/ThemedController.h"
#include "plugins/widget-suite/Widget.h"

namespace redline {
class Skin;
}

namespace redline_widget {
using namespace widgetplugin;

class Canvas final : public widgetplugin::themed::View {
public:
    Canvas();
    ~Canvas() override;
    void bind(const CdCreateContext&, std::function<void(uint32_t)>) override;
    void input(const CdInput&) override;
    void cancelInput() override;
    void pause(bool paused) override;
    void syncInput(Spec spec, const Media& media, CdRect safe, const Photo& photo = {},
                   AlbumPlayback album = {}) override;
    bool render(Spec, const cdsdk::Settings&, const Spectrum&, const Photo&, const Photo&, float fade, Date,
                int monthOffset, const CdFrame&, CdRect, const Media& media = {}, int pressed = 0,
                const CdInputStats& inputStats = {}, const widgetplugin::CounterDrums* counterDrums = nullptr,
                AlbumPlayback album = {}) override;
    bool savePng(const std::wstring&, const CdFrame&);
    bool pendingImages() const;
    cybercanvas::Rect photoRegion(CdRect safe) const override;
    cybercanvas::Rect displayRegion(std::string_view name, CdRect safe) const;

    bool photoHit(CdRect safe, const cdsdk::Settings& settings, const CdInput& input) const override;

    themed::PlayerPointer playerPointer(Spec spec, CdRect safe, const cdsdk::Settings& settings, const Media& media,
                                        const CdInput& input) const override;

private:
    std::unique_ptr<redline::Skin> skin_;
};

CdResult create(Spec, const CdCreateContext*, CdInstanceApi*, uint32_t);

}  // namespace redline_widget
