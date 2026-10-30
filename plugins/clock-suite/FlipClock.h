#pragma once
#include <array>

#include "plugins/clock-suite/Clock.h"

namespace clockplugin {
struct FlipValue {
    int before = 0, after = 0;
    double progress = 1;
};

struct FlipPose {
    std::array<FlipValue, 3> values;
};

class FlipTimeline {
    struct Value {
        int before = -1, after = -1;
        uint64_t started = 0;
    };

    std::array<Value, 3> values;

public:
    void reset(Time, const Options&, uint64_t);
    FlipPose sample(Time, const Options&, uint64_t);
    bool active(uint64_t, double duration) const;
};

struct FlipGeometry {
    Rect hours, minutes, seconds;
};

FlipGeometry arrangeFlip(Spec, double width, double height, bool seconds);

class FlipRenderer {
public:
    FlipRenderer();
    ~FlipRenderer();
    void bindOutput(const CdCreateContext& context);
    bool render(Spec, const Options&, Time, const FlipPose&, const CdFrame&, Rect safe = {},
                std::string* error = nullptr);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}  // namespace clockplugin
