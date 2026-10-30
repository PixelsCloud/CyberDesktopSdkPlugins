#pragma once

#include "plugins/redline-desk/Skin.h"
#include "plugins/widget-suite/Widget.h"

namespace redline_widget {
using namespace widgetplugin;

class Canvas {
public:
    Canvas();
    ~Canvas();
    void bind(const CdCreateContext&, std::function<void(uint32_t)>);
    void input(const CdInput&);
    void cancelInput();
    void pause(bool paused);
    void syncInput(Spec spec, const Media& media, CdRect safe, const Photo& photo = {}, AlbumPlayback album = {});
    bool render(Spec, const cdsdk::Settings&, const Spectrum&, const Photo&, const Photo&, float fade, Date,
                int monthOffset, const CdFrame&, CdRect, const Media& media = {}, int pressed = 0,
                const CdInputStats& inputStats = {}, const widgetplugin::CounterDrums* counterDrums = nullptr,
                AlbumPlayback album = {});
    bool savePng(const std::wstring&, const CdFrame&);
    bool pendingImages() const;
    cybercanvas::Rect photoRegion(CdRect safe) const;
    cybercanvas::Rect displayRegion(std::string_view name, CdRect safe) const;

private:
    std::unique_ptr<redline::Skin> skin_;
};

CdResult create(Spec, const CdCreateContext*, CdInstanceApi*, uint32_t);

}  // namespace redline_widget
