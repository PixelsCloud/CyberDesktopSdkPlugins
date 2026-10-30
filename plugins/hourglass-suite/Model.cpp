#include <algorithm>
#include <cmath>

#include "plugins/hourglass-suite/Hourglass.h"

namespace hourglassplugin {
int chamberHalfWidth(int row) {
    constexpr int widths[]{0, 0, 7, 7, 6, 6, 5, 4, 4, 3, 2, 2, 1, 0, 0};
    if (row < 2 || row > 26) {
        return -1;
    }
    return widths[row <= 14 ? row : 28 - row];
}

Model::Model() {
    for (int y = 2; y <= 12; ++y) {
        const int half = chamberHalfWidth(y);
        for (int x = 9 - half; x <= 9 + half; ++x) {
            upper_.push_back({x, y});
            lower_.push_back({x, 28 - y});
        }
    }
    std::stable_sort(upper_.begin(), upper_.end(), [](Cell a, Cell b) {
        return a.y != b.y ? a.y < b.y : std::abs(a.x - 9) < std::abs(b.x - 9);
    });
    std::stable_sort(lower_.begin(), lower_.end(), [](Cell a, Cell b) {
        // A pile grows from the bottom center with a shallow angle of repose.
        return (26 - a.y) * 2.2 + std::abs(a.x - 9) < (26 - b.y) * 2.2 + std::abs(b.x - 9);
    });
}

void Model::reset(double duration) {
    duration_ = std::isfinite(duration) ? std::clamp(duration, 5.0, 3600.0) : 30;
    elapsed_ = turning_ = 0;
    phase_ = Phase::Flowing;
    cycles_ = 0;
}

void Model::advance(double seconds, bool repeat) {
    if (!std::isfinite(seconds) || seconds < 0 || phase_ == Phase::Finished) {
        return;
    }
    if (!repeat && phase_ == Phase::Turning) {
        phase_ = Phase::Finished;
        turning_ = 0;
        return;
    }
    if (seconds == 0) {
        return;
    }
    seconds = std::min(seconds, 86400.0);
    while (seconds > .000000001) {
        if (phase_ == Phase::Flowing) {
            const double step = std::min(seconds, duration_ - elapsed_);
            elapsed_ += step;
            seconds -= step;
            if (elapsed_ + .000000001 >= duration_) {
                elapsed_ = duration_;
                phase_ = repeat ? Phase::Turning : Phase::Finished;
                if (!repeat) {
                    return;
                }
            }
        } else {
            const double step = std::min(seconds, kFlipDuration - turning_);
            turning_ += step;
            seconds -= step;
            if (turning_ + .000000001 >= kFlipDuration) {
                elapsed_ = turning_ = 0;
                phase_ = Phase::Flowing;
                ++cycles_;
            }
        }
    }
}

Sand Model::sample() const {
    Sand result;
    const double emission = duration_ - .65;
    for (size_t i = 0; i < upper_.size(); ++i) {
        const double release = (i + 1) * emission / upper_.size();
        const double age = elapsed_ - release;
        if (age < 0) {
            result.upper.push_back(upper_[i]);
            continue;
        }
        const Cell landing = lower_[i];
        const double flight = .32 + (landing.y - 16) * .028;
        if (age >= flight || phase_ != Phase::Flowing) {
            result.lower.push_back(landing);
        } else {
            const double progress = std::clamp(age / flight, 0.0, 1.0);
            const double fall = progress * progress;
            const int row = std::clamp(int(std::round(13 + (landing.y - 13) * fall)), 13, 26);
            const int half = chamberHalfWidth(row);
            const int column = std::clamp(int(std::round(9 + (landing.x - 9) * fall)), 9 - half, 9 + half);
            result.falling.push_back({column, row});
        }
    }
    return result;
}

double Model::progress() const {
    return elapsed_ / duration_;
}

double Model::angle(bool continuous) const {
    if (phase_ != Phase::Turning) {
        return 0;
    }
    const double progress =
        continuous ? std::clamp(turning_ / kFlipDuration, 0.0, 1.0) : std::clamp((turning_ - .12) / .76, 0.0, 1.0);
    return 180 * progress * progress * (3 - 2 * progress);
}

int Model::remaining() const {
    return int(std::ceil(std::max(0.0, duration_ - elapsed_ - .000000001)));
}

size_t Model::capacity() const {
    return upper_.size();
}

Phase Model::phase() const {
    return phase_;
}

uint64_t Model::cycles() const {
    return cycles_;
}

CdRect glassArea(double width, double height, bool showTimer) {
    const double boxWidth = showTimer ? width * .38 : width;
    const double pixel = std::max(.1, std::min((boxWidth - 12) / kGridWidth, (height - 16) / kGridHeight));
    return {(boxWidth - pixel * kGridWidth) * .5, (height - pixel * kGridHeight) * .5 - 1, pixel * kGridWidth,
            pixel * kGridHeight};
}
}  // namespace hourglassplugin
