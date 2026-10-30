#include <algorithm>
#include <cmath>

#include "plugins/beat-machine/Machine.h"

namespace beatmachine {
void AudioMotion::feed(std::span<const float> bands, uint32_t sampleRate) {
    const double previousBass = input_[0];
    const auto previousPistons = pistonInput_;
    input_.fill(0);
    pistonInput_.fill(0);
    if (bands.empty()) {
        attack_ = 0;
        pistonAttack_.fill(0);
        return;
    }
    std::array<unsigned, 3> counts{};
    std::array<double, 3> peaks{};
    std::array<unsigned, 5> pistonCounts{};
    std::array<double, 5> pistonPeaks{};
    // Match the host's existing logarithmic 20 Hz..min(16 kHz, .48*rate)
    // frequency map. Keep rumble below 30 Hz out of the piston trigger.
    const double ceiling = std::min(16000., (sampleRate >= 1000 ? sampleRate : 48000) * .48);
    for (size_t i = 0; i < bands.size(); ++i) {
        const double frequency = 20 * std::pow(ceiling / 20, (double(i) + .5) / bands.size());
        const double value = std::isfinite(bands[i]) ? std::clamp(double(bands[i]), 0., 1.) : 0;
        for (size_t piston = 0; piston < kPistonBassRanges.size(); ++piston) {
            if (frequency >= kPistonBassRanges[piston].low && frequency < kPistonBassRanges[piston].high) {
                pistonInput_[piston] += value * value;
                pistonPeaks[piston] = std::max(pistonPeaks[piston], value);
                ++pistonCounts[piston];
            }
        }
        if (frequency < 30) {
            continue;
        }
        const auto group = frequency < 200 ? 0 : (frequency < 2500 ? 1 : 2);
        input_[group] += value * value;
        peaks[group] = std::max(peaks[group], value);
        ++counts[group];
    }
    for (size_t i = 0; i < 3; ++i) {
        // Sparse musical fundamentals must not be diluted across 21 log bands.
        input_[i] = counts[i] && peaks[i] > .0045 ? .6 * std::sqrt(input_[i] / counts[i]) + .4 * peaks[i] : 0;
    }
    attack_ = std::max(attack_, std::max(0., input_[0] - previousBass));
    for (size_t i = 0; i < pistonInput_.size(); ++i) {
        pistonInput_[i] = pistonCounts[i] && pistonPeaks[i] > .0045
                              ? .6 * std::sqrt(pistonInput_[i] / pistonCounts[i]) + .4 * pistonPeaks[i]
                              : 0;
        pistonAttack_[i] = std::max(pistonAttack_[i], std::max(0., pistonInput_[i] - previousPistons[i]));
    }
}

bool AudioMotion::advance(double delta, bool stale, double gain) {
    if (!std::isfinite(delta) || delta <= 0 || !std::isfinite(gain)) {
        return false;
    }
    delta = std::min(delta, .1);
    if (stale) {
        input_.fill(0);
        attack_ = 0;
        pistonInput_.fill(0);
        pistonAttack_.fill(0);
    }
    time_ += delta;
    const auto before = signal_;
    for (size_t i = 0; i < 3; ++i) {
        const double target = stale ? 0 : std::pow(std::clamp(input_[i] * gain * 1.65, 0., 1.), .65);
        const double response = target > envelope_[i] ? (i == 0 ? .008 : .018) : (i == 0 ? .045 : .16);
        envelope_[i] += (target - envelope_[i]) * (1 - std::exp(-delta / response));
        background_[i] += (envelope_[i] - background_[i]) * (1 - std::exp(-delta / .65));
        if (envelope_[i] < .001) {
            envelope_[i] = 0;
        }
    }
    signal_.bass = envelope_[0];
    signal_.middle = envelope_[1];
    signal_.high = envelope_[2];
    // Consume each input attack once. A short refractory period permits rapid
    // eighth-note kicks, while a held bass tone cannot retrigger the pistons.
    if (!stale && attack_ > std::max(.0045, input_[0] * .12) && signal_.bass > .04 && time_ - lastBeat_ >= .075) {
        ++signal_.beat;
        const double onset = std::pow(std::clamp(attack_ * gain * 1.65, 0., 1.), .65);
        signal_.strength = std::clamp(onset / std::max(.18, background_[0] * .6), .25, 1.);
        lastBeat_ = time_;
        if (signal_.bass > .3 && signal_.strength > .5 && time_ - lastAccent_ > .85) {
            ++signal_.accent;
            lastAccent_ = time_;
        }
    }
    attack_ = 0;
    signal_.bassMapped = true;
    bool pistonChanged = false;
    for (size_t i = 0; i < pistonInput_.size(); ++i) {
        const bool launcher = i == 4;
        const double target = std::pow(std::clamp(pistonInput_[i] * gain * (launcher ? 2.1 : 1.65), 0., 1.), .65);
        const double response = target > pistonEnvelope_[i] ? .008 : .045;
        pistonEnvelope_[i] += (target - pistonEnvelope_[i]) * (1 - std::exp(-delta / response));
        if (pistonEnvelope_[i] < .001) {
            pistonEnvelope_[i] = 0;
        }
        signal_.pistonBass[i] = pistonEnvelope_[i];
        if (!stale && pistonAttack_[i] > std::max(launcher ? .0025 : .0045, pistonInput_[i] * (launcher ? .07 : .12)) &&
            pistonEnvelope_[i] > (launcher ? .025 : .04) && time_ - lastPistonBeat_[i] >= (launcher ? .075 : .085)) {
            ++signal_.pistonBeat[i];
            signal_.pistonStrength[i] =
                std::clamp(std::pow(std::clamp(pistonAttack_[i] * gain * 1.65, 0., 1.), .65) / .25, .25, 1.);
            lastPistonBeat_[i] = time_;
            pistonChanged = true;
        }
        pistonAttack_[i] = 0;
    }
    return std::abs(before.bass - signal_.bass) > .0005 || std::abs(before.middle - signal_.middle) > .0005 ||
           std::abs(before.high - signal_.high) > .0005 || before.beat != signal_.beat || pistonChanged;
}

void AudioMotion::clear() {
    *this = AudioMotion{};
}

const Signal& AudioMotion::signal() const {
    return signal_;
}

bool AudioMotion::active() const {
    return std::max({envelope_[0], envelope_[1], envelope_[2], input_[0], input_[1], input_[2],
                     *std::max_element(pistonInput_.begin(), pistonInput_.end()),
                     *std::max_element(pistonEnvelope_.begin(), pistonEnvelope_.end())}) > .003;
}
}  // namespace beatmachine
