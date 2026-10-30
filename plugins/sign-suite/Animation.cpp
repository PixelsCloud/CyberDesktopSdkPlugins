#include "plugins/sign-suite/Animation.h"

namespace neon {
Effect effectFromName(std::string_view name) {
    if (name == "breathe") {
        return Effect::Breathe;
    }
    if (name == "scan") {
        return Effect::Scan;
    }
    if (name == "wave") {
        return Effect::Wave;
    }
    if (name == "glitch") {
        return Effect::Glitch;
    }
    return name == "mixed" ? Effect::Mixed : Effect::Flicker;
}

int faultAt(double seconds) {
    const double phase = std::fmod(seconds, 3.6);
    return (phase >= 1.1 && phase < 1.22) || (phase >= 1.48 && phase < 1.60) ? 1 : 0;
}

int glitchAt(double seconds) {
    const double phase = std::fmod(seconds, 3.6);
    if (phase >= 2.3 && phase < 2.48) {
        return 1;
    }
    return phase >= 2.65 && phase < 2.8 ? 2 : 0;
}

LightSample sampleLight(Effect effect, double seconds, double position, int row, double strength, double damage) {
    constexpr double tau = 6.283185307179586;
    LightSample value;
    if (effect == Effect::Breathe || effect == Effect::Mixed) {
        const double depth = effect == Effect::Mixed ? .25 : .65;
        value.power *= 1 - strength * depth * (.5 - .5 * std::cos(seconds * tau / 4));
    }
    if (effect == Effect::Wave) {
        value.power *= 1 - strength * .75 * (.5 - .5 * std::cos(seconds * tau / 4 - position * tau));
    }
    if (effect == Effect::Scan || effect == Effect::Mixed) {
        const double center = std::fmod(seconds, 3.) / 3. * 1.5 - .25;
        const double distance = (position - center) / .10;
        value.highlight = strength * std::exp(-distance * distance * .5);
        if (effect == Effect::Scan) {
            value.power *= 1 - strength * .35 * (1 - value.highlight);
        }
    }
    if ((effect == Effect::Flicker || effect == Effect::Mixed) && faultAt(seconds) && damage > 0 && position > .65 &&
        position < .76) {
        value.power *= 1 - strength * .88;
    }
    if (effect == Effect::Glitch || effect == Effect::Mixed) {
        const int fault = glitchAt(seconds);
        if (fault && (row == 2 || row == 3)) {
            value.offset = (fault == 1 ? 2.5 : -1.8) * strength;
            value.power *= 1 - strength * .25;
        }
    }
    return value;
}
}  // namespace neon
