#pragma once

#include "sdk/List.h"
#include "sdk/Scene.h"

namespace cdsdk {
struct SceneListEvent {
    cybercanvas::SceneEvent control;
    cybercanvas::ListEvent row;
};

// A retained header/footer surrounding a virtual list. Arrange before input;
// the body decoration and list viewport always use the same clipped bounds.
class SceneList {
public:
    explicit SceneList(cybercanvas::View root = {});

    ~SceneList();

    SceneList(const SceneList&) = delete;
    SceneList& operator=(const SceneList&) = delete;

    Scene& scene();

    ListView& list();

    cybercanvas::Scene::Node body() const;

    void bind(const CdCreateContext& context);

    void paint(cybercanvas::ListView::PaintRow callback);

    void onRowAction(std::function<void(cybercanvas::ListEvent)> callback);

    void label(std::function<std::wstring(const cybercanvas::ListRow&)> callback);

    bool arrange(cybercanvas::Canvas& canvas, cybercanvas::Rect area, double dpr = 1, bool rtl = false);

    void draw(cybercanvas::Canvas& canvas, const cybercanvas::IconPainter& icons = {});

    SceneListEvent input(const CdInput& event, cybercanvas::Point origin = {});

    void cancel();

    bool advance(double seconds);

    bool active() const;

private:
    Scene scene_;
    ListView list_;
    cybercanvas::Scene::Node body_ = 0;
    cybercanvas::ListView::PaintRow paint_;
    cybercanvas::IconPainter icons_;
    std::function<void(cybercanvas::ListEvent)> rowAction_;
    std::function<std::wstring(const cybercanvas::ListRow&)> label_;
    bool available_ = false;
};
}  // namespace cdsdk
