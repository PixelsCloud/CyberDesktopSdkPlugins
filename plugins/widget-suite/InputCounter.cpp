#include "plugins/widget-suite/InputCounter.h"

namespace widgetplugin {
std::array<unsigned, 5> counterDigits(uint64_t value) {
    value = std::min<uint64_t>(value, 99999);
    std::array<unsigned, 5> digits{};
    for (size_t i = digits.size(); i > 0; --i) {
        digits[i - 1] = unsigned(value % 10);
        value /= 10;
    }
    return digits;
}

auto CounterDrums::sample(uint64_t now) const -> Positions {
    Positions values{};
    for (size_t i = 0; i < values.size(); ++i) {
        const double seconds = now >= started_[i] ? (now - started_[i]) * .001 : 0;
        values[i] =
            cybercanvas::Tween<double>(from_[i], to_[i], {.16, cybercanvas::Ease::OutCubic}).sample(seconds).value;
    }
    return values;
}

auto CounterDrums::retarget(const CdInputStats& stats, uint64_t now, bool animate) -> bool {
    const bool valid = (stats.status & CD_INPUT_STATS_ACTIVE) != 0;
    const auto keyboard = counterDigits(stats.keyboard_today), mouse = counterDigits(stats.mouse_today);
    std::array<unsigned, 10> digits{};
    std::copy(keyboard.begin(), keyboard.end(), digits.begin());
    std::copy(mouse.begin(), mouse.end(), digits.begin() + 5);
    const bool changed = !initialized_ || digits != digits_ || valid != valid_ || stats.day != day_;
    const bool snap = !initialized_ || !animate || !valid || valid != valid_ || stats.day != day_;
    const auto current = sample(now);
    for (size_t i = 0; i < digits.size(); ++i) {
        if (snap) {
            from_[i] = to_[i] = digits[i];
            started_[i] = now;
        } else if (digits[i] != digits_[i]) {
            from_[i] = current[i];
            to_[i] = std::floor(current[i] / 10) * 10 + digits[i];
            if (to_[i] < current[i]) {
                to_[i] += 10;
            }
            started_[i] = now;
        }
    }
    digits_ = digits;
    valid_ = valid;
    day_ = stats.day;
    initialized_ = true;
    return changed;
}

auto CounterDrums::active(uint64_t now) const -> bool {
    for (size_t i = 0; i < to_.size(); ++i) {
        if (from_[i] != to_[i] && now >= started_[i] && now - started_[i] < 160) {
            return true;
        }
    }
    return false;
}

auto CounterDrums::initialized() const -> bool {
    return initialized_;
}

auto CounterDrums::finish() -> void {
    from_ = to_;
}

auto CounterDrums::reset() -> void {
    *this = {};
}
}  // namespace widgetplugin
