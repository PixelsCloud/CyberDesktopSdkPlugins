#pragma once

#include "plugins/ambience-suite/Atmosphere.h"
#include "plugins/pixel-desk/BasicClock.h"

namespace pixeldesk {
const BasicSpec& ambiencePlate(std::string_view id);
const BasicSpec& ambienceFolder(std::string_view folder);
atmosphere::Spec ambienceSpec(const BasicSpec& plate);
}  // namespace pixeldesk
