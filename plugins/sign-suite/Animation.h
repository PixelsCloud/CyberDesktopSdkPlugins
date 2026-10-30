#pragma once

#include <algorithm>
#include <cmath>
#include <string_view>

namespace neon {
enum class Effect { Flicker, Breathe, Scan, Wave, Glitch, Mixed };

Effect effectFromName(std::string_view name);

int faultAt(double seconds);

int glitchAt(double seconds);

struct LightSample {
    double power = 1, offset = 0, highlight = 0;
};

// Position is normalized along the sign. Only two narrow rows move during a
// contact fault; the backing plate and the complete word never jump around.
LightSample sampleLight(Effect effect, double seconds, double position, int row, double strength, double damage);
}  // namespace neon
