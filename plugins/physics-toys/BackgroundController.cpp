#include "plugins/physics-toys/BackgroundController.h"

#include "plugins/physics-toys/Model.h"
#include "sdk/Update.h"

namespace toys {
CdResult scheduleBackgroundClock(Model& model, cdsdk::UpdateLoop& updates, bool frozen, bool hidden, double speed,
                                 uint64_t monotonicMs) {
    model.pauseBackground(frozen);
    model.advance(0, 0, speed);
    if (hidden) {
        updates.stop();
        return CD_OK;
    }
    return updates.start(!model.backgrounded() && !frozen && model.active() ? 33 : 1000, monotonicMs);
}

bool scheduleBackground(Model& model, cdsdk::UpdateLoop& updates, bool frozen, bool hidden, double wind, double speed) {
    if (!model.backgrounded()) {
        return false;
    }
    model.pauseBackground(frozen);
    model.advance(0, model.kind() == Kind::Chime ? wind : 0, speed);
    if (hidden || model.kind() == Kind::Chime) {
        updates.stop();
    } else {
        updates.start(1000);
    }
    return true;
}
}  // namespace toys
