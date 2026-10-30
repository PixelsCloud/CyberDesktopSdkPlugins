#pragma once

#include <sdk/PluginApi.h>
#include <sdk/Scene.h>
#include <sdk/Settings.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include "plugins/ambience-suite/Atmosphere.h"
#include "plugins/mecha-desk/Skin.h"

namespace aeris_ambience {
using atmosphere::Kind;
using atmosphere::Model;
using atmosphere::Spec;
using atmosphere::wallSeconds;
cdsdk::Settings defaults(Spec spec);

class Renderer {
public:
    explicit Renderer(std::wstring directory = {});
    ~Renderer();
    void bindHost(const CdCreateContext& context);
    void bindPlant(const CdCreateContext&, std::function<void(uint32_t)>);
    bool plantInput(Spec, const cdsdk::Settings&, CdRect, const CdInput&);
    void onInvalidate(std::function<void()> callback);
    void onAction(std::function<void(cybercanvas::SceneEvent)> callback);
    void onDiagnostics(std::function<void(cdsdk::InstanceStats&)> callback);
    cybercanvas::SceneEvent noiseInput(Spec, const cdsdk::Settings&, const Model&, CdRect, const CdInput&);
    cdsdk::Scene& noiseScene(Spec spec, const cdsdk::Settings& settings, const Model& model, double width,
                             double height, double dpr = 1);
    void cancelInput();
    bool advanceFeedback(double seconds);
    bool feedbackActive() const;
    bool imagesPending() const;
    bool pollImages();
    void pauseImages(bool paused);
    bool render(Spec spec, const cdsdk::Settings& settings, const Model& model, const CdFrame& frame, CdRect safe);
    bool save(const std::wstring& filename, const CdFrame& frame);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

CdResult create(Spec spec, const CdCreateContext* context, CdInstanceApi* output, uint32_t size);
}  // namespace aeris_ambience
