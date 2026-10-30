#pragma once

#include "plugins/ambience-suite/Atmosphere.h"
#include "plugins/pixel-desk/BasicClock.h"

namespace pixeldesk {
const BasicSpec& ambiencePlate(std::string_view id);
const BasicSpec& ambienceFolder(std::string_view folder);
atmosphere::Spec ambienceSpec(const BasicSpec& plate);
cdsdk::Settings ambienceDefaults(atmosphere::Spec spec);
CdResult createAmbience(atmosphere::Spec, const CdCreateContext*, CdInstanceApi*, uint32_t);

class AmbienceRenderer final : public atmosphere::themed::View {
public:
    explicit AmbienceRenderer(std::wstring directory = {});
    ~AmbienceRenderer() override;
    void bindHost(const CdCreateContext&) override;
    void bindPlant(const CdCreateContext&, std::function<void(uint32_t)>) override;
    bool plantInput(atmosphere::Spec, const cdsdk::Settings&, CdRect, const CdInput&) override;
    void onInvalidate(std::function<void()>) override;
    void onAction(std::function<void(cybercanvas::SceneEvent)>) override;
    void onDiagnostics(std::function<void(cdsdk::InstanceStats&)>) override;
    cdsdk::Scene& noiseScene(atmosphere::Spec, const cdsdk::Settings&, const atmosphere::Model&, double, double,
                             double = 1);
    cybercanvas::SceneEvent noiseInput(atmosphere::Spec, const cdsdk::Settings&, const atmosphere::Model&, CdRect,
                                       const CdInput&, double, bool&) override;
    void cancelInput() override;
    bool advanceFeedback(double) override;
    bool feedbackActive() const override;
    bool imagesPending() const override;
    bool pollImages() override;
    void pauseImages(bool) override;
    bool render(atmosphere::Spec, const cdsdk::Settings&, const atmosphere::Model&, const CdFrame&, CdRect) override;
    bool save(const std::wstring&, const CdFrame&);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace pixeldesk
