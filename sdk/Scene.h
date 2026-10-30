#pragma once

#include <CyberCanvas/Scene.h>

#include <functional>
#include <optional>

#include "sdk/Diagnostics.h"
#include "sdk/Interaction.h"
#include "sdk/Keyboard.h"

namespace cdsdk {
// Shares the engine router with the existing optional host capture service.
class Scene : public cybercanvas::Scene {
public:
    explicit Scene(cybercanvas::View rootView = {});

    ~Scene();

    void bind(const CdCreateContext& context);

    cybercanvas::SceneEvent input(const CdInput& event, cybercanvas::Point origin = {});

    Keyboard& keyboard();

    DiagnosticSource& diagnostics();

    bool arrange(cybercanvas::Canvas& canvas, cybercanvas::Rect area, double dpr = 1, bool rtl = false);

    void onAction(std::function<void(cybercanvas::SceneEvent)> callback);

private:
    using cybercanvas::Scene::onInputReset;
    using cybercanvas::Scene::onSemanticsChanged;
    Interaction interaction_;
    Keyboard keyboard_;
    std::optional<std::reference_wrapper<cybercanvas::Canvas>> canvas_;
    DiagnosticSource diagnostics_;
};
}  // namespace cdsdk
