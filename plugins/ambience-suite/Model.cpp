#include <CyberCanvas/Noise.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <tuple>

#include "plugins/ambience-suite/Atmosphere.h"

namespace atmosphere {
uint64_t wallSeconds() {
    return uint64_t(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}

void Model::advance(double seconds, double speed, bool moving) {
    if (!std::isfinite(seconds) || !std::isfinite(speed) || seconds <= 0 || speed < 0) {
        return;
    }
    motion.setSpeed(speed);
    motion.setPaused(!moving);
    motion.advance(seconds);
    interaction.advance(seconds);
    if (interaction.seconds() > 10) {
        interaction.seek(10);
    }
}

bool Model::feedbackActive() const {
    return interaction.seconds() < 3;
}

double Model::sway(double offset) const {
    static const cybercanvas::Tween<double> wave(-1, 1,
                                                 {.duration = 2.8,
                                                  .easing = cybercanvas::Ease::InOutSine,
                                                  .repeat = cybercanvas::Repeat::PingPong,
                                                  .cycles = 0});
    static const cybercanvas::NoiseField breeze({.seed = 2718, .frequency = .1, .octaves = 2});
    return .58 * wave.sample(motion.seconds() + offset).value +
           .42 * breeze.animated({offset * 29, 17}, motion.seconds(), .48);
}

double Model::reaction() const {
    static const cybercanvas::Tween<double> fade(1, 0, {.duration = 2.2, .easing = cybercanvas::Ease::OutCubic});
    static const cybercanvas::Tween<double> shake(-1, 1,
                                                  {.duration = .24,
                                                   .easing = cybercanvas::Ease::InOutSine,
                                                   .repeat = cybercanvas::Repeat::PingPong,
                                                   .cycles = 0});
    const double age = interaction.seconds();
    return fade.sample(age).value * shake.sample(age + .12).value;
}

double Model::waterOpacity() const {
    static const cybercanvas::Tween<double> fade(1, 0, {.duration = 2.4, .easing = cybercanvas::Ease::InCubic});
    return fade.sample(interaction.seconds()).value;
}

cybercanvas::Pose Model::breathingPose() const {
    static const cybercanvas::Tween<cybercanvas::Pose> breath({.opacity = .88}, {.scale = {1.04, 1.04}},
                                                              {.duration = 2.4,
                                                               .easing = cybercanvas::Ease::InOutSine,
                                                               .repeat = cybercanvas::Repeat::PingPong,
                                                               .cycles = 0});
    return breath.sample(motion.seconds()).value;
}

double Model::growth(uint64_t now, bool growing) const {
    if (!growing) {
        return 1;
    }
    const double elapsed = now >= plantedAt ? double(now - plantedAt) : 0;
    return std::clamp(.15 + elapsed / (7 * 86400) + bonus, .15, 1.);
}

void Model::water(uint64_t now) {
    if (!wateredAt || now >= wateredAt && now - wateredAt >= 3600) {
        bonus = std::min(.35, bonus + .04);
    }
    wateredAt = now;
    waterFeedback = true;
    interaction.seek(0);
}

void Model::click(double x, double y) {
    waterFeedback = false;
    clickX = x;
    clickY = y;
    interaction.seek(0);
}
}  // namespace atmosphere
