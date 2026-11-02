// rdev/liquid-glass-react/src/shader-utils.ts, ac48eab.
// Copyright 2025 MAX ROVENSKY, MIT (resources/LiquidGlassReact-MIT.txt).
#include "libraries/liquidglass/src/react/Displacement.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace liquidglass::react {
namespace {
struct Vector {
    double x, y;
};

double smoothStep(double a, double b, double value) {
    const double t = std::clamp((value - a) / (b - a), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}

Vector fragment(double x, double y) {
    const double ix = x - .5, iy = y - .5;
    const double qx = std::abs(ix) - .3 + .6, qy = std::abs(iy) - .2 + .6;
    const double ox = std::max(qx, 0.0), oy = std::max(qy, 0.0);
    const double distance = std::min(std::max(qx, qy), 0.0) + std::sqrt(ox * ox + oy * oy) - .6;
    const double displacement = smoothStep(.8, 0, distance - .15);
    const double scaled = smoothStep(0, 1, displacement);
    return {ix * scaled + .5, iy * scaled + .5};
}
}  // namespace

std::vector<uint8_t> shaderDisplacement(int width, int height) {
    if (width < 1 || height < 1 || int64_t(width) * height > 2 * 1024 * 1024) {
        throw std::invalid_argument("React displacement map exceeds its logical-pixel budget");
    }
    double maximum = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto position = fragment(double(x) / width, double(y) / height);
            const double dx = position.x * width - x, dy = position.y * height - y;
            maximum = std::max({maximum, std::abs(dx), std::abs(dy)});
        }
    }
    maximum = std::max(maximum, 1.0);
    std::vector<uint8_t> result(size_t(width) * height * 2);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto position = fragment(double(x) / width, double(y) / height);
            const double edgeDistance = std::min({x, y, width - x - 1, height - y - 1});
            const double edgeFactor = std::min(1.0, edgeDistance / 2);
            const double dx = (position.x * width - x) * edgeFactor;
            const double dy = (position.y * height - y) * edgeFactor;
            const size_t index = (size_t(y) * width + x) * 2;
            // Uint8ClampedArray uses nearest-even rounding, as nearbyint does
            // with the default C++ floating-point environment.
            result[index] = uint8_t(std::nearbyint(std::clamp((dx / maximum + .5) * 255, 0.0, 255.0)));
            result[index + 1] = uint8_t(std::nearbyint(std::clamp((dy / maximum + .5) * 255, 0.0, 255.0)));
        }
    }
    return result;
}
}  // namespace liquidglass::react
