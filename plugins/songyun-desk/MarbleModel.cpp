#include "plugins/songyun-desk/controllers/marble/Clock.h"

namespace songyun_marble {
Model::Model(bool compact) : marbleclock::Model(compact, marbleclock::GeometryStyle::Illustrated) {}

Geometry geometry(bool compact) {
    return marbleclock::geometry(compact, marbleclock::GeometryStyle::Illustrated);
}

}  // namespace songyun_marble
