#pragma once

#include <CyberCanvas/Particles.h>
#include <sdk/Images.h>
#include <sdk/Scene.h>

#include <filesystem>

#include "plugins/ambience-suite/Atmosphere.h"

namespace atmosphere {
class NoiseScene {
public:
    explicit NoiseScene(std::filesystem::path directory);
    ~NoiseScene();
    cdsdk::Scene& prepare(cybercanvas::Canvas& canvas, Spec spec, const cdsdk::Settings& settings, const Model& model,
                          double width, double height, double dpr);
    void bind(const CdCreateContext& context);
    void cancel();
    bool advance(double seconds);
    bool active() const;
    void draw(cybercanvas::Canvas& canvas, const cybercanvas::IconPainter& icons);
    bool imagesPending() const;
    bool pollImages();
    void pauseImages(bool paused);
    void onInvalidate(std::function<void()> callback);
    void onAction(std::function<void(cybercanvas::SceneEvent)> callback);
    void onDiagnostics(std::function<void(cdsdk::InstanceStats&)> callback);

private:
    using Node = cybercanvas::Scene::Node;
    void build(bool compact);
    std::filesystem::path directory_;
    std::unique_ptr<cdsdk::Scene> scene_;
    std::optional<CdCreateContext> context_;
    bool compact_ = false, dark_ = false, background_ = true;
    double corner_ = 5;
    double footerWidth_ = -1, footerDpr_ = 0, footerHeight_ = 22;
    Node header_ = 0, heading_ = 0, subtitle_ = 0, play_ = 0, body_ = 0, footer_ = 0;
    Node timer_ = 0, master_ = 0, more_ = 0, volumeLabel_ = 0, spacer_ = 0;
    std::array<Node, 6> cells_{}, art_{}, labels_{}, tracks_{};
    cdsdk::Images images_;
    std::array<std::unique_ptr<cybercanvas::ParticleEmitter>, 6> particles_;
    std::array<int, 6> particleKinds_{{-1, -1, -1, -1, -1, -1}};
    std::function<void()> invalidated_;
    std::function<void(cybercanvas::SceneEvent)> action_;
    std::function<void(cdsdk::InstanceStats&)> diagnostics_;
};
}  // namespace atmosphere
