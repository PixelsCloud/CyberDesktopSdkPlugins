#include "CyberCanvas/PhysicsScene.h"

namespace cybercanvas::physics {
SceneBinding::SceneBinding(World& world, Scene& scene, Scene::Node root, double pixelsPerMeter)
    : world_(world), scene_(scene), root_(root), scale_(pixelsPerMeter) {
    if (!scene.contains(root) || !std::isfinite(scale_) || scale_ <= 0 || scale_ > 1e6) {
        throw std::invalid_argument("Invalid physics scene mapping");
    }
}

SceneBinding::~SceneBinding() {
    if (drag_.id && world_.valid(drag_)) {
        world_.destroy(drag_);
    }
    if (ground_.id && world_.valid(ground_)) {
        world_.destroy(ground_);
    }
}

auto SceneBinding::unbind(Body body) -> void {
    if (dragBody_ == body) {
        cancelDrag();
    }
    bindings_.erase(body.id);
}

auto SceneBinding::update() -> void {
    mapping();
    const auto rootBounds = scene_.layoutBounds(root_);
    for (auto it = bindings_.begin(); it != bindings_.end();) {
        const Body body{it->first};
        const auto& binding = it->second;
        if (!world_.valid(body) || !scene_.contains(binding.node)) {
            if (dragBody_ == body) {
                cancelDrag();
            }
            it = bindings_.erase(it);
            continue;
        }
        if (scene_.parent(binding.node) != root_) {
            throw std::logic_error("Rebind physical nodes after changing their parent");
        }
        const auto area = scene_.layoutBounds(binding.node);
        const auto state = world_.interpolated(body);
        auto pose = scene_.view(binding.node).transform;
        pose.pivot = binding.pivot.value_or(Point{area.width * .5, area.height * .5});
        pose.translation = {rootBounds.x + state.position.x * scale_ - area.x - pose.pivot.x,
                            rootBounds.y + state.position.y * scale_ - area.y - pose.pivot.y};
        pose.rotation = state.angle * 180 / std::numbers::pi;
        scene_.transform(binding.node, pose);
        ++it;
    }
}

auto SceneBinding::toWorld(Point visual) const -> Point {
    auto matrix = mapping();
    if (!matrix.Invert()) {
        throw std::logic_error("Singular physics mapping");
    }
    const auto p = matrix.TransformPoint({float(visual.x), float(visual.y)});
    const auto area = scene_.layoutBounds(root_);
    return {(p.x - area.x) / scale_, (p.y - area.y) / scale_};
}

auto SceneBinding::drag(Point visual) -> void {
    if (drag_.id && world_.valid(drag_)) {
        world_.mouseTarget(drag_, toWorld(visual));
    }
}

auto SceneBinding::cancelDrag() -> void {
    if (drag_.id && world_.valid(drag_)) {
        world_.destroy(drag_);
    }
    drag_ = {};
    dragBody_ = {};
}

auto SceneBinding::dragging() const -> bool {
    return drag_.id && world_.valid(drag_);
}

auto SceneBinding::mapping() const -> D2D1::Matrix3x2F {
    const auto matrix = scene_.worldTransform(root_);
    const double x = std::hypot(matrix._11, matrix._12), y = std::hypot(matrix._21, matrix._22);
    const double dot = double(matrix._11) * matrix._21 + double(matrix._12) * matrix._22;
    if (x < 1e-6 || std::abs(x - y) > 1e-5 * x || std::abs(dot) > 1e-5 * x * y || matrix.Determinant() <= 0) {
        throw std::invalid_argument("Physics root requires a positive uniform transform without shear");
    }
    return matrix;
}

auto SceneBinding::bind(Body body, Scene::Node node, std::optional<Point> pivot) -> void {
    if (!world_.valid(body) || !scene_.contains(node) || scene_.parent(node) != root_) {
        throw std::invalid_argument("Physics binding needs a valid body and direct child of its root");
    }
    if (pivot && (!std::isfinite(pivot->x) || !std::isfinite(pivot->y))) {
        throw std::invalid_argument("Invalid physics visual pivot");
    }
    for (const auto& [existing, binding] : bindings_) {
        if (binding.node == node && existing != body.id) {
            throw std::invalid_argument("A scene node cannot have two physical owners");
        }
    }
    bindings_[body.id] = {node, pivot};
}

auto SceneBinding::beginDrag(Point visual, double maximumForce, Point inputOrigin) -> bool {
    cancelDrag();
    update();
    scene_.targets(inputOrigin);
    auto hit = scene_.router().hit({visual.x + inputOrigin.x, visual.y + inputOrigin.y});
    // A clickable visual child can pick its bound physical ancestor.
    while (hit && scene_.parent(hit) != root_) {
        hit = scene_.parent(hit);
    }
    for (const auto& [id, binding] : bindings_) {
        if (binding.node == hit) {
            if (!ground_.id) {
                ground_ = world_.createBody({.type = BodyType::Static});
            }
            dragBody_ = {id};
            drag_ = world_.mouse(ground_, dragBody_, toWorld(visual), maximumForce);
            return true;
        }
    }
    return false;
}

auto SceneBinding::debugDraw(Canvas& canvas, std::optional<Rect> query, std::optional<RayHit> hit) const -> void {
    const auto matrix = mapping();
    const auto origin = scene_.layoutBounds(root_);
    canvas.saved([&] {
        D2D1::Matrix3x2F old;
        canvas.target->GetTransform(&old);
        canvas.target->SetTransform(matrix * old);
        canvas.translate(origin.x, origin.y);
        canvas.scale(scale_, scale_);
        for (const auto& shape : world_.debugShapes()) {
            if (shape.vertices.empty()) {
                continue;
            }
            canvas.ink(shape.sensor ? 0xffa000ff : shape.awake ? 0x20cc80ff : 0x808080ff);
            if (shape.vertices.size() == 1) {
                const auto center = shape.vertices.front();
                canvas.target->DrawEllipse(
                    D2D1::Ellipse({float(center.x), float(center.y)}, float(shape.radius), float(shape.radius)),
                    canvas.brush.Get(), float(1 / scale_));
            } else if (shape.vertices.size() == 2 && shape.radius > 0) {
                const auto a = shape.vertices[0], b = shape.vertices[1];
                const double length = std::hypot(b.x - a.x, b.y - a.y);
                const Point normal{(b.y - a.y) / length * shape.radius, -(b.x - a.x) / length * shape.radius};
                auto path = canvas.path();
                path.moveTo({a.x + normal.x, a.y + normal.y})
                    .lineTo({b.x + normal.x, b.y + normal.y})
                    .arcTo({b.x - normal.x, b.y - normal.y}, {shape.radius, shape.radius})
                    .lineTo({a.x - normal.x, a.y - normal.y})
                    .arcTo({a.x + normal.x, a.y + normal.y}, {shape.radius, shape.radius})
                    .close();
                canvas.target->DrawGeometry(path.geometry(canvas.factory.Get()), canvas.brush.Get(), float(1 / scale_));
            } else {
                auto path = canvas.path();
                path.moveTo(shape.vertices.front());
                for (size_t i = 1; i < shape.vertices.size(); ++i) {
                    path.lineTo(shape.vertices[i]);
                }
                if (shape.vertices.size() > 2) {
                    path.close();
                }
                canvas.target->DrawGeometry(path.geometry(canvas.factory.Get()), canvas.brush.Get(), float(1 / scale_));
            }
        }
        for (const auto& body : world_.debugBodies()) {
            const auto p = body.centerOfMass;
            const double edge = 3 / scale_;
            canvas.line(p.x - edge, p.y, p.x + edge, p.y, 0xf4d35eff, 1 / scale_);
            canvas.line(p.x, p.y - edge, p.x, p.y + edge, 0xf4d35eff, 1 / scale_);
            const auto v = body.state.velocity;
            canvas.line(p.x, p.y, p.x + v.x * .15, p.y + v.y * .15, 0x74baffff, 1 / scale_);
        }
        for (const auto& joint : world_.debugJoints()) {
            canvas.line(joint.anchorFirst.x, joint.anchorFirst.y, joint.anchorSecond.x, joint.anchorSecond.y,
                        joint.mouse ? 0xff77ccff : 0xb7a6ffff, 1 / scale_);
        }
        if (query) {
            detail::layoutRect(*query);
            canvas.ink(0xffdd66ff);
            canvas.target->DrawRectangle(D2D1::RectF(float(query->x), float(query->y), float(query->x + query->width),
                                                     float(query->y + query->height)),
                                         canvas.brush.Get(), float(1 / scale_));
        }
        if (hit) {
            canvas.line(hit->point.x, hit->point.y, hit->point.x + hit->normal.x * .2,
                        hit->point.y + hit->normal.y * .2, 0xff6677ff, 2 / scale_);
        }
    });
    const auto stats = world_.stats();
    const std::wstring label = L"Bodies " + std::to_wstring(stats.bodies) + L"  Shapes " +
                               std::to_wstring(stats.shapes) + L"  Joints " + std::to_wstring(stats.joints) +
                               L"  Awake " + std::to_wstring(stats.awakeBodies);
    canvas.text(label, {.size = 10, .vertical = VerticalAlign::Top, .ellipsis = true},
                {origin.x + 4, origin.y + 4, std::max(1., origin.width - 8), 16}, 0xf4d35eff);
}
}  // namespace cybercanvas::physics
