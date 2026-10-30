#pragma once

#include <functional>
#include <memory>
#include <string>

#include "plugins/ambience-suite/Atmosphere.h"
#include "plugins/ambience-suite/ThemedController.h"

namespace mechanicaldesk_ambience {
using namespace atmosphere;

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

cdsdk::Settings defaults(Spec spec);
CdResult create(Spec, const CdCreateContext*, CdInstanceApi*, uint32_t);
}  // namespace mechanicaldesk_ambience
