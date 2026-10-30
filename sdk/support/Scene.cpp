#include "sdk/Scene.h"

namespace cdsdk {
Scene::Scene(cybercanvas::View rootView)
    : cybercanvas::Scene(std::move(rootView)), interaction_(router()), keyboard_(*this) {
    onInputReset([this] {
        interaction_.cancel();
    });
    onSemanticsChanged([this] {
        keyboard_.sync();
    });
    diagnostics_.onSample([this] {
        InstanceStats result;
        if (canvas_) {
            result.canvas = canvas_->get().stats();
        }
        result.scene = stats();
        return result;
    });
    diagnostics_.onTree([this] {
        auto result = debugNodes();
        const auto offset = keyboard_.origin();
        for (auto& node : result) {
            for (auto* rect : {&node.bounds, &node.clip, &node.hit}) {
                rect->x += offset.x;
                rect->y += offset.y;
            }
        }
        return result;
    });
}

Scene::~Scene() {
    diagnostics_.unbind();
    onSemanticsChanged({});
    keyboard_.unbind();
    interaction_.cancel();
}

auto Scene::bind(const CdCreateContext& context) -> void {
    interaction_.bind(context);
    keyboard_.bind(context);
    diagnostics_.bind(context);
    if (context.host && context.host->struct_size >= sizeof(CdHostApi) && context.host->request_redraw) {
        const auto host = *context.host;
        const auto hostContext = context.host_context;
        onInvalidate([host, hostContext] {
            host.request_redraw(hostContext);
        });
    }
}

auto Scene::input(const CdInput& event, cybercanvas::Point origin) -> cybercanvas::SceneEvent {
    if (event.struct_size < sizeof(CdInput)) {
        return {};
    }
    targets(origin);
    keyboard_.origin(origin);
    if (event.kind == CD_INPUT_PRESS && (event.buttons & CD_BUTTON_LEFT)) {
        keyboard_.request();
    }
    return accept(interaction_.input(event));
}

auto Scene::keyboard() -> Keyboard& {
    return keyboard_;
}

auto Scene::diagnostics() -> DiagnosticSource& {
    return diagnostics_;
}

auto Scene::arrange(cybercanvas::Canvas& canvas, cybercanvas::Rect area, double dpr, bool rtl) -> bool {
    const bool changed = cybercanvas::Scene::arrange(canvas, area, dpr, rtl);
    canvas_ = std::ref(canvas);
    return changed;
}

auto Scene::onAction(std::function<void(cybercanvas::SceneEvent)> callback) -> void {
    keyboard_.onAction(std::move(callback));
}
}  // namespace cdsdk
