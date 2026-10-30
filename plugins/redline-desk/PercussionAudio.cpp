#include "plugins/redline-desk/PercussionAudio.h"

namespace redline {
auto PercussionAudio::feed(std::span<const float> bands, uint32_t sampleRate) -> void {
    std::array<std::array<float, 63>, 3> mapped{};
    constexpr std::array<double, 4> edges{30, 200, 2500, 16000};
    const double ceiling = std::min(16000., (sampleRate >= 1000 ? sampleRate : 48000) * .48);
    for (size_t i = 0; i < bands.size(); ++i) {
        const double frequency = 20 * std::pow(ceiling / 20, (i + .5) / bands.size());
        if (frequency < edges.front() || !std::isfinite(bands[i])) {
            continue;
        }
        const unsigned channel = frequency < edges[1] ? 0 : frequency < edges[2] ? 1 : 2;
        const double position =
            std::clamp(std::log(frequency / edges[channel]) / std::log(edges[channel + 1] / edges[channel]), 0., 1.);
        const double remapped = 40 * std::pow(4., position);
        const auto bin = size_t(std::clamp(std::log(remapped / 20) / std::log(800.) * 63, 0., 62.));
        mapped[channel][bin] = std::max(mapped[channel][bin], std::clamp(bands[i], 0.f, 1.f));
    }
    for (size_t i = 0; i < detectors_.size(); ++i) {
        detectors_[i].feed(mapped[i], 48000);
    }
}

auto PercussionAudio::clear() -> void {
    for (auto& detector : detectors_) {
        detector.clear();
    }
}
}  // namespace redline
