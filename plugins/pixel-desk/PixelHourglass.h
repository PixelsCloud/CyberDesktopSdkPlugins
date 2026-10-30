#pragma once

#include <CyberCanvas/PanelLayout.h>

#include <algorithm>
#include <cmath>
#include <utility>

#include "plugins/mechanical-hourglass/MechanicalHourglass.h"

namespace pixeldesk {
inline constexpr uint32_t kSandGold = 0xf3b94dff;

double hourglassHalfWidth(double position);

inline constexpr std::array<mechanicalhourglass::Spec, 2> kHourglassCatalog{{
    {"com.cyberdesktop.pixeldesk.hourglass.1x1", "hourglass-1x1", 1},
    {"com.cyberdesktop.pixeldesk.hourglass.2x1", "hourglass-2x1", 2},
}};
}  // namespace pixeldesk
