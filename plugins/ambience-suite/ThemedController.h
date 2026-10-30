#pragma once

#include <sdk/PluginApi.h>

#include <functional>
#include <memory>

namespace cdsdk {
class Settings;
struct InstanceStats;
}  // namespace cdsdk

namespace cybercanvas {
struct SceneEvent;
}

namespace atmosphere {
struct Spec;
struct Model;

namespace themed {
struct Defaults {
    bool plantBackground = true;
    bool plantInfo = true;
    bool compactSucculent = false;
    bool noiseAnimation = true;
    bool trackToggle = false;
    bool indicateMissingAudio = false;
};

class View {
public:
    virtual ~View();
    virtual void bindHost(const CdCreateContext&) = 0;
    virtual void bindPlant(const CdCreateContext&, std::function<void(uint32_t)>) = 0;
    virtual bool plantInput(Spec, const cdsdk::Settings&, CdRect, const CdInput&) = 0;
    virtual cybercanvas::SceneEvent noiseInput(Spec, const cdsdk::Settings&, const Model&, CdRect, const CdInput&,
                                               double dpr, bool& needsPaint) = 0;
    virtual void onInvalidate(std::function<void()>) = 0;
    virtual void onAction(std::function<void(cybercanvas::SceneEvent)>) = 0;
    virtual void onDiagnostics(std::function<void(cdsdk::InstanceStats&)>) = 0;
    virtual void cancelInput() = 0;
    virtual bool advanceFeedback(double) = 0;
    virtual bool feedbackActive() const = 0;
    virtual bool imagesPending() const = 0;
    virtual bool pollImages() = 0;
    virtual void pauseImages(bool) = 0;
    virtual bool render(Spec, const cdsdk::Settings&, const Model&, const CdFrame&, CdRect) = 0;
};

using ViewFactory = std::unique_ptr<View> (*)(const CdCreateContext&);
cdsdk::Settings defaults(Spec, Defaults presentation = {});
CdResult create(Spec, const CdCreateContext*, CdInstanceApi*, uint32_t, ViewFactory, Defaults presentation = {});
}  // namespace themed
}  // namespace atmosphere
