#include "plugins/vampire-alice/controllers/marble/Clock.h"

namespace alice_marble {
Model::Model(bool compact) : marbleclock::Model(compact, marbleclock::GeometryStyle::ExtendedCompact) {}

Model::Model(const CdCreateContext& context, bool compact)
    : marbleclock::Model(context, compact, marbleclock::GeometryStyle::ExtendedCompact) {}

Geometry geometry(bool compact) {
    return marbleclock::geometry(compact, marbleclock::GeometryStyle::ExtendedCompact);
}

}  // namespace alice_marble
