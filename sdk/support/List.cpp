#include "sdk/List.h"

namespace cdsdk {
ListView::ListView(cybercanvas::ScrollStyle style)
    : cybercanvas::ListView(style), interaction_(router()), wheelStep_(style.wheelStep) {
    onInputReset([this] {
        interaction_.cancel();
    });
}

ListView::~ListView() {
    interaction_.cancel();
}

auto ListView::bind(const CdCreateContext& context) -> void {
    interaction_.bind(context);
    if (context.host && context.host->struct_size >= sizeof(CdHostApi) && context.host->request_redraw) {
        const auto host = *context.host;
        const auto hostContext = context.host_context;
        onInvalidate([host, hostContext] {
            host.request_redraw(hostContext);
        });
    }
}

auto ListView::input(const CdInput& event, cybercanvas::Point origin) -> cybercanvas::ListEvent {
    if (event.struct_size < sizeof(CdInput)) {
        return {};
    }
    const bool inside = contains({event.x - origin.x, event.y - origin.y});
    if (event.kind == CD_INPUT_WHEEL) {
        if (!inside || !std::isfinite(event.wheel_delta)) {
            return {};
        }
        cybercanvas::ListEvent result;
        result.handled = engaged() || wheel(-event.wheel_delta / 120 * wheelStep_);
        return result;
    }
    if (!inside && !engaged() && event.kind != CD_INPUT_CANCEL && event.kind != CD_INPUT_LEAVE) {
        targets(origin);
        accept(router().dispatch({cybercanvas::PointerKind::Leave}));
        return {};
    }
    if (event.kind == CD_INPUT_PRESS && (event.buttons & CD_BUTTON_LEFT) && !engaged()) {
        scrollTo(offset(), false);
    }
    targets(origin);
    return accept(interaction_.input(event));
}
}  // namespace cdsdk
