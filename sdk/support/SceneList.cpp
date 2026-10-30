#include "sdk/SceneList.h"

namespace cdsdk {
SceneList::SceneList(cybercanvas::View root) : scene_(std::move(root)) {
    cybercanvas::View body;
    body.kind = cybercanvas::ViewKind::Decoration;
    body.layout.grow = 1;
    body.intrinsic = {};
    body.clickable = true;
    body.accessibleName = L"Items";
    body.semanticRole = cybercanvas::SemanticRole::List;
    body.semanticValue = [this] {
        for (const auto& row : list_.rows()) {
            if (row.id == list_.selected()) {
                return label_ ? label_(row) : std::wstring(row.id.begin(), row.id.end());
            }
        }
        return std::wstring{};
    };
    body.onKey = [this](cybercanvas::KeyEvent event) {
        const auto row = list_.key(event, available_);
        if (row.handled && rowAction_) {
            const auto callback = rowAction_;
            callback(row);
        }
        cybercanvas::SceneEvent result;
        result.handled = row.handled;
        return result;
    };
    body.paint = [this](auto& canvas, auto, const auto& theme) {
        if (available_) {
            list_.draw(canvas, theme, paint_, icons_);
        }
    };
    body_ = scene_.add(0, std::move(body));
    list_.onInvalidate([this] {
        scene_.invalidate();
    });
    scene_.diagnostics().extras([this](auto& stats) {
        stats.list = list_.stats();
    });
}

SceneList::~SceneList() {
    cancel();
}

auto SceneList::scene() -> Scene& {
    return scene_;
}

auto SceneList::list() -> ListView& {
    return list_;
}

auto SceneList::body() const -> cybercanvas::Scene::Node {
    return body_;
}

auto SceneList::bind(const CdCreateContext& context) -> void {
    scene_.bind(context);
    list_.bind(context);
    list_.onInvalidate([this] {
        scene_.invalidate();
    });
}

auto SceneList::paint(cybercanvas::ListView::PaintRow callback) -> void {
    paint_ = std::move(callback);
    scene_.invalidate();
}

auto SceneList::onRowAction(std::function<void(cybercanvas::ListEvent)> callback) -> void {
    rowAction_ = std::move(callback);
}

auto SceneList::label(std::function<std::wstring(const cybercanvas::ListRow&)> callback) -> void {
    label_ = std::move(callback);
    scene_.invalidate();
}

auto SceneList::arrange(cybercanvas::Canvas& canvas, cybercanvas::Rect area, double dpr, bool rtl) -> bool {
    const bool changed = scene_.arrange(canvas, area, dpr, rtl);
    available_ = scene_.contains(body_);
    if (available_) {
        for (auto node = body_;; node = scene_.parent(node)) {
            const auto& view = scene_.view(node);
            available_ &= view.layout.visible && view.enabled;
            if (!node) {
                break;
            }
        }
    }
    if (!available_) {
        list_.cancel();
        return changed;
    }
    return list_.arrange(scene_.bounds(body_), dpr, rtl) || changed;
}

auto SceneList::draw(cybercanvas::Canvas& canvas, const cybercanvas::IconPainter& icons) -> void {
    icons_ = icons;
    scene_.draw(canvas, icons);
}

auto SceneList::input(const CdInput& event, cybercanvas::Point origin) -> SceneListEvent {
    if (event.struct_size < sizeof(CdInput)) {
        return {};
    }
    if (event.kind == CD_INPUT_CANCEL || event.kind == CD_INPUT_LEAVE) {
        return {scene_.input(event, origin), list_.input(event, origin)};
    }
    if (scene_.router().pressed()) {
        return {scene_.input(event, origin), {}};
    }
    if (available_ && (list_.engaged() || list_.contains({event.x - origin.x, event.y - origin.y}))) {
        if (event.kind == CD_INPUT_PRESS && (event.buttons & CD_BUTTON_LEFT)) {
            scene_.keyboard().request();
            scene_.focus(body_);
        }
        if (!list_.engaged()) {
            scene_.cancel();
        }
        return {{}, list_.input(event, origin)};
    }
    list_.cancel();
    return {scene_.input(event, origin), {}};
}

auto SceneList::cancel() -> void {
    list_.cancel();
    scene_.cancel();
}

auto SceneList::advance(double seconds) -> bool {
    const bool changed = list_.advance(seconds);
    return scene_.advance(seconds) || changed;
}

auto SceneList::active() const -> bool {
    return list_.active() || scene_.active();
}
}  // namespace cdsdk
