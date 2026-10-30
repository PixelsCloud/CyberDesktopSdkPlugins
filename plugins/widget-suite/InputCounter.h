#pragma once

#include <CyberCanvas/Animation.h>
#include <sdk/PluginApi.h>

#include <array>

namespace widgetplugin {
std::array<unsigned, 5> counterDigits(uint64_t value);

class CounterDrums {
public:
    using Positions = std::array<double, 10>;

    Positions sample(uint64_t now) const;

    bool retarget(const CdInputStats& stats, uint64_t now, bool animate);

    bool active(uint64_t now) const;

    bool initialized() const;

    void finish();

    void reset();

private:
    Positions from_{}, to_{};
    std::array<unsigned, 10> digits_{};
    std::array<uint64_t, 10> started_{};
    uint32_t day_ = 0;
    bool initialized_ = false, valid_ = false;
};
}  // namespace widgetplugin
