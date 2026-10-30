#pragma once

#include <CyberCanvas/Types.h>
#include <sdk/PluginApi.h>
#include <sdk/Settings.h>

#include <functional>
#include <memory>

namespace cdsdk {
struct MediaSnapshot;
}

namespace widgetplugin {
struct Media;

namespace media {
struct PlayerSpec {
    const char* id;
    const char* folder;
    unsigned columns, rows;
    cybercanvas::Size source;
    cybercanvas::Rect visible;
    cybercanvas::Size world;
    double partHeight;
    bool reels;
};

class View {
public:
    virtual ~View();
    virtual void onChange(std::function<void()>) = 0;
    virtual void onAction(std::function<void(uint32_t)>) = 0;
    virtual void pause(bool) = 0;
    virtual void cancelInput() = 0;
    virtual void setSeeking(bool) = 0;
    virtual void syncInput(const Media&, CdRect) = 0;
    virtual void input(const CdInput&) = 0;
    virtual bool ready() const = 0;
    virtual bool render(const cdsdk::Settings&, const Media&, double, const CdFrame&, CdRect) = 0;
    virtual bool animatedArtwork() const;
    virtual cybercanvas::Rect seekRegion(const PlayerSpec&, double, double) const = 0;
};

Media fromSnapshot(const cdsdk::MediaSnapshot&, const Media& previous);

class Controller {
public:
    Controller(const CdCreateContext&, PlayerSpec, std::unique_ptr<View>);
    virtual ~Controller();
    Controller(const Controller&) = delete;
    Controller& operator=(const Controller&) = delete;

    cdsdk::Settings settings;
    CdResult render(const CdFrame&);
    CdResult applySettings(const CdSettingValue*, uint32_t);
    void layoutChanged(const CdLayout&);
    void setPaused(bool);
    void input(const CdInput&);
    void invalidate();
    const Media& media() const;
    uint64_t pendingRequest() const;
    bool ready() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace media
}  // namespace widgetplugin
