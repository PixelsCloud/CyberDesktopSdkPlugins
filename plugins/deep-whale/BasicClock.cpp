#include "plugins/deep-whale/BasicClock.h"

namespace deep_basic {
auto BasicSpec::clock() const -> clockplugin::Spec {
    return {id,
            kind == BasicKind::Analog,
            int(rows),
            int(columns),
            kind == BasicKind::Flip,
            kind == BasicKind::Lcd ? clockplugin::Display::Lcd : clockplugin::Display::Classic};
}
}  // namespace deep_basic
