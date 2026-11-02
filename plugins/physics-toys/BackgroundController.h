#pragma once

#include <cstdint>

#include "sdk/PluginApi.h"

namespace cdsdk {
class UpdateLoop;
}

namespace toys {
class Model;

// Shared UI scheduling policy for every theme using the toys model. Physics
// wakes autonomously on its lane; only clock business time stays on the UI.
bool scheduleBackground(Model& model, cdsdk::UpdateLoop& updates, bool frozen, bool hidden, double wind, double speed);
CdResult scheduleBackgroundClock(Model& model, cdsdk::UpdateLoop& updates, bool frozen, bool hidden, double speed,
                                 uint64_t monotonicMs);
}  // namespace toys
