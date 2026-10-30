#include "plugins/redline-desk/controllers/marble/Clock.h"

namespace redline_marble {
Model::Model(bool compact) : marbleclock::Model(compact, marbleclock::GeometryStyle::Illustrated) {}

Geometry geometry(bool compact) {
    return marbleclock::geometry(compact, marbleclock::GeometryStyle::Illustrated);
}

}  // namespace redline_marble
