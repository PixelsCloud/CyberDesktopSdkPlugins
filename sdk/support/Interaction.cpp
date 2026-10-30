#include "sdk/Interaction.h"

namespace cdsdk {
Interaction::Interaction() : router(ownedRouter_) {}

Interaction::Interaction(cybercanvas::InputRouter& shared) : router(shared) {}

auto Interaction::regions(std::vector<cybercanvas::HitRegion> values) -> cybercanvas::PointerResult {
    const auto result = router.regions(std::move(values));
    if (streaming_ && !router.captured()) {
        api_.capture(context_, 0);
    }
    return result;
}

auto Interaction::bind(const CdCreateContext& context) -> void {
    context_ = context.host_context;
    if (!context.host || context.host->struct_size < sizeof(CdServiceHostApi)) {
        return;
    }
    const auto* host = reinterpret_cast<const CdServiceHostApi*>(context.host);
    if (host->query_service && host->query_service(context_, CD_SERVICE_INTERACTION, 1, &api_, sizeof(api_)) == CD_OK &&
        api_.struct_size >= sizeof(api_) && api_.version == 1 && api_.enable && api_.capture &&
        api_.enable(context_, 1) == CD_OK) {
        streaming_ = true;
    } else {
        api_ = {};
    }
}

auto Interaction::input(const CdInput& event) -> cybercanvas::PointerResult {
    using cybercanvas::PointerKind;
    PointerKind kind;
    switch (event.kind) {
        case CD_INPUT_PRESS:
            kind = PointerKind::Press;
            break;
        case CD_INPUT_RELEASE:
            kind = PointerKind::Release;
            break;
        case CD_INPUT_MOVE:
            kind = PointerKind::Move;
            break;
        case CD_INPUT_LEAVE:
            kind = PointerKind::Leave;
            break;
        case CD_INPUT_CANCEL:
            kind = PointerKind::Cancel;
            break;
        default:
            return {};
    }
    const auto result = router.dispatch({kind, {event.x, event.y}, bool(event.buttons & CD_BUTTON_LEFT)});
    if (streaming_) {
        if (result.drag == cybercanvas::DragPhase::Begin) {
            if (api_.capture(context_, 1) != CD_OK) {
                return cancel();
            }
        } else if (!router.captured()) {
            api_.capture(context_, 0);
        }
    }
    return result;
}

auto Interaction::cancel() -> cybercanvas::PointerResult {
    const auto result = router.dispatch({cybercanvas::PointerKind::Cancel});
    if (streaming_) {
        api_.capture(context_, 0);
    }
    return result;
}

auto Interaction::streaming() const -> bool {
    return streaming_;
}
}  // namespace cdsdk
