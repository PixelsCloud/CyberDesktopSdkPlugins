#include "plugins/songyun-desk/controllers/beat/Machine.h"

namespace songyun_beat {
Model::Model(unsigned count, bool compact)
    : beatmachine::Model(count, compact, beatmachine::GeometryStyle::Illustrated, kRadius) {}

double laneHeight(double x, bool compact) {
    return beatmachine::illustratedLaneHeight(x, compact);
}
}  // namespace songyun_beat
