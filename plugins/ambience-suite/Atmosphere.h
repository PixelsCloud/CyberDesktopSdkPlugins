#pragma once

#include <CyberCanvas/Animation.h>
#include <sdk/PluginApi.h>
#include <sdk/Scene.h>
#include <sdk/Settings.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include "plugins/ambience-suite/ThemedController.h"

namespace atmosphere {
enum class Kind { Plant = 0, Noise = 2 };

struct Spec {
    const char* id;
    Kind kind;
    int rows, columns;
};

struct Model {
    cybercanvas::AnimationClock motion, interaction{10};
    double clickX = 0, clickY = 0;
    uint64_t plantedAt = 0, wateredAt = 0;
    double bonus = 0;
    bool dark = false;
    bool waterFeedback = false;
    std::array<float, 6> levels{.6f, 0, 0, 0, 0, 0};
    float master = .25f;
    uint32_t audioStatus = 0, audioError = 0;
    uint64_t deadline = 0;
    int noisePage = 0;
    void advance(double seconds, double speed, bool moving = true);
    bool feedbackActive() const;
    double sway(double offset = 0) const;
    double reaction() const;
    double waterOpacity() const;
    cybercanvas::Pose breathingPose() const;
    double growth(uint64_t now, bool growing) const;
    void water(uint64_t now);
    void click(double x, double y);
};

uint64_t wallSeconds();
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
}  // namespace atmosphere
