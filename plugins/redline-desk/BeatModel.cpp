#include "plugins/redline-desk/controllers/beat/Machine.h"

namespace redline_beat {
Model::Model(unsigned count, bool compact)
    : beatmachine::Model(count, compact, beatmachine::GeometryStyle::Illustrated, kRadius) {}

double laneHeight(double x, bool compact) {
    return beatmachine::illustratedLaneHeight(x, compact);
}
}  // namespace redline_beat
