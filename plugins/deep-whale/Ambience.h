#pragma once

#include <sdk/PluginApi.h>
#include <sdk/Scene.h>
#include <sdk/Settings.h>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "plugins/ambience-suite/Atmosphere.h"
#include "plugins/ambience-suite/ThemedController.h"

namespace deep_ambience {
using atmosphere::Kind;
using atmosphere::Model;
using atmosphere::Spec;
using atmosphere::wallSeconds;
cdsdk::Settings defaults(Spec spec);

class Renderer final : public atmosphere::themed::View {
public:
    explicit Renderer(std::wstring directory = {});
    ~Renderer() override;
    void bindHost(const CdCreateContext& context) override;
    void bindPlant(const CdCreateContext&, std::function<void(uint32_t)>) override;
    bool plantInput(Spec, const cdsdk::Settings&, CdRect, const CdInput&) override;
    void onInvalidate(std::function<void()> callback) override;
    void onAction(std::function<void(cybercanvas::SceneEvent)> callback) override;
    void onDiagnostics(std::function<void(cdsdk::InstanceStats&)> callback) override;
    cybercanvas::SceneEvent noiseInput(Spec, const cdsdk::Settings&, const Model&, CdRect, const CdInput&);
    cdsdk::Scene& noiseScene(Spec spec, const cdsdk::Settings& settings, const Model& model, double width,
                             double height, double dpr = 1);
    cybercanvas::SceneEvent noiseInput(Spec, const cdsdk::Settings&, const Model&, CdRect, const CdInput&, double,
                                       bool& needsPaint) override;
    void cancelInput() override;
    bool advanceFeedback(double seconds) override;
    bool feedbackActive() const override;
    bool imagesPending() const override;
    bool pollImages() override;
    void pauseImages(bool paused) override;
    bool render(Spec spec, const cdsdk::Settings& settings, const Model& model, const CdFrame& frame,
                CdRect safe) override;
    bool save(const std::wstring& filename, const CdFrame& frame);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

CdResult create(Spec spec, const CdCreateContext* context, CdInstanceApi* output, uint32_t size);
}  // namespace deep_ambience
