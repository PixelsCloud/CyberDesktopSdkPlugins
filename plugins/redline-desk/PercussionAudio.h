#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

#include "plugins/beat-machine/Machine.h"

namespace redline {
// Feed each musical register through the established raw-onset detector. Its
// attack is consumed once, before display smoothing can hide a short transient.
class PercussionAudio {
public:
    void feed(std::span<const float> bands, uint32_t sampleRate);

    template <typename Strike>
    void advance(double seconds, bool stale, double gain, Strike strike) {
        for (unsigned i = 0; i < detectors_.size(); ++i) {
            auto& detector = detectors_[i];
            const auto before = detector.signal().beat;
            detector.advance(seconds, stale, gain);
            if (detector.signal().beat != before) {
                strike(i, detector.signal().strength);
            }
        }
    }

    void clear();

private:
    std::array<beatmachine::AudioMotion, 3> detectors_;
};
}  // namespace redline
