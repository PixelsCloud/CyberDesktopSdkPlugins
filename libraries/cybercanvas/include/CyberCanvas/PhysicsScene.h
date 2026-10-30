#pragma once

#include <map>
#include <numbers>

#include "CyberCanvas/Physics2D.h"
#include "CyberCanvas/Scene.h"

namespace cybercanvas::physics {
// An explicitly owned bridge. Scene and World must outlive it. Bound nodes
// must be direct children of root; visual descendants may animate freely.
class SceneBinding {
public:
    SceneBinding(World& world, Scene& scene, Scene::Node root, double pixelsPerMeter = 100);

    ~SceneBinding();

    SceneBinding(const SceneBinding&) = delete;
    SceneBinding& operator=(const SceneBinding&) = delete;

    void bind(Body body, Scene::Node node, std::optional<Point> pivot = {});

    void unbind(Body body);

    void update();

    Point toWorld(Point visual) const;

    // visual is in scene coordinates; inputOrigin is the SDK host-to-scene offset.
    // Pass the same origin as Scene::input to preserve host capture.
    bool beginDrag(Point visual, double maximumForce = 1000, Point inputOrigin = {});

    void drag(Point visual);

    void cancelDrag();

    bool dragging() const;

    void debugDraw(Canvas& canvas, std::optional<Rect> query = {}, std::optional<RayHit> hit = {}) const;

private:
    struct Binding {
        Scene::Node node;
        std::optional<Point> pivot;
    };

    World& world_;
    Scene& scene_;
    Scene::Node root_;
    double scale_;
    std::map<uint64_t, Binding> bindings_;
    Body ground_{}, dragBody_{};
    Joint drag_{};

    D2D1::Matrix3x2F mapping() const;
};
}  // namespace cybercanvas::physics
