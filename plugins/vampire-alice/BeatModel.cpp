#include "plugins/vampire-alice/controllers/beat/Machine.h"

namespace alice_beat {
Model::Model(unsigned count, bool compact)
    : beatmachine::Model(count, compact, beatmachine::GeometryStyle::Illustrated) {}

double laneHeight(double x, bool compact) {
    return beatmachine::illustratedLaneHeight(x, compact);
}
}  // namespace alice_beat
