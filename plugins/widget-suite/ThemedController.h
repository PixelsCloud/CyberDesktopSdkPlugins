#pragma once

#include <sdk/PluginApi.h>

#include <functional>
#include <memory>

namespace cdsdk {
class Settings;
}

namespace cybercanvas {
struct Rect;
}

namespace widgetplugin {
struct Spec;
enum class Kind;
struct Media;
struct Photo;
struct AlbumPlayback;
struct Spectrum;
struct Date;
class CounterDrums;
}  // namespace widgetplugin

namespace widgetplugin::themed {
struct Defaults {
    uint32_t spectrumColor = 0xadd7efff;
    uint32_t spectrumTop = 0xf0d9adff;
    bool spectrumBackground = true;
};

struct PlayerPointer {
    int control = 0;
    double seekSeconds = 0;
};

bool albumPhotoHit(CdRect safe, bool counter, const CdInput& input);

// One theme view per instance; the controller owns subscriptions and scheduling.
class View {
public:
    virtual ~View();
    virtual void bind(const CdCreateContext&, std::function<void(uint32_t)>) = 0;
    virtual void input(const CdInput&) = 0;
    virtual void cancelInput() = 0;
    virtual void pause(bool paused) = 0;
    virtual void syncInput(Spec, const Media&, CdRect, const Photo&, AlbumPlayback) = 0;
    virtual bool render(Spec, const cdsdk::Settings&, const Spectrum&, const Photo&, const Photo&, float, Date, int,
                        const CdFrame&, CdRect, const Media&, int, const CdInputStats&, const CounterDrums*,
                        AlbumPlayback) = 0;
    virtual cybercanvas::Rect photoRegion(CdRect) const = 0;
    virtual bool photoHit(CdRect, const cdsdk::Settings&, const CdInput&) const;
    virtual PlayerPointer playerPointer(Spec, CdRect, const cdsdk::Settings&, const Media&, const CdInput&) const;
};

cdsdk::Settings defaults(Kind, bool mechanical, Defaults presentation);

using ViewFactory = std::unique_ptr<View> (*)(Spec);
CdResult create(Spec, const CdCreateContext*, CdInstanceApi*, uint32_t, ViewFactory, Defaults defaults = {});
}  // namespace widgetplugin::themed
