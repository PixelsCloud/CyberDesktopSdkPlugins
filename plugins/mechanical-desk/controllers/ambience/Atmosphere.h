#pragma once
#include "plugins/ambience-suite/Atmosphere.h"
#include "plugins/mechanical-desk/Skin.h"

namespace mechanicaldesk_ambience {
using namespace atmosphere;

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

cdsdk::Settings defaults(Spec spec);
CdResult create(Spec, const CdCreateContext*, CdInstanceApi*, uint32_t);
}  // namespace mechanicaldesk_ambience
