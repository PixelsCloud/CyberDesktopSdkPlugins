#pragma once

#include <cstdint>
#include <memory>

#include "CyberCanvas/Types.h"

namespace cybercanvas {
enum class NoiseAlgorithm { OpenSimplex2S, Perlin, Value };
enum class NoiseFractal { None, FBm, Ridged };

struct NoiseSettings {
    int32_t seed = 1337;
    NoiseAlgorithm algorithm = NoiseAlgorithm::OpenSimplex2S;
    NoiseFractal fractal = NoiseFractal::FBm;
    double frequency = .01;
    int octaves = 3;
    double lacunarity = 2;
    double gain = .5;
};

// Immutable, copyable field. Concurrent const sampling is safe and allocation-free.
// Values are in [-1, 1]. Coordinates are logical units, not physical pixels.
// Invalid/non-finite inputs throw invalid_argument. No clock or redraw scheduling.
class NoiseField {
public:
    explicit NoiseField(NoiseSettings settings = {});
    double sample(double x, double y) const;
    double sample(double x, double y, double z) const;
    // Continuous circular time path avoids unbounded time coordinates. Speed is
    // lattice units/second; the gentle loop has period 64*pi/speed seconds.
    double animated(Point position, double seconds, double speed = .3) const;
    const NoiseSettings& settings() const;

private:
    struct Impl;
    std::shared_ptr<const Impl> impl_;
};

struct NoiseWindSettings {
    int32_t seed = 1337;
    double strength = 8;  // Maximum acceleration magnitude, DIP/s^2.
    double spatialFrequency = .025;
    double temporalFrequency = .3;
};

// Callable directly from ParticleEmitter::wind(). Time must be the simulation's
// elapsed seconds: paused/hidden/reduced-motion emitters do not advance it.
class NoiseWind {
public:
    explicit NoiseWind(NoiseWindSettings settings = {});
    Point operator()(Point position, double seconds) const;

private:
    NoiseWindSettings settings_;
    NoiseField horizontal_, vertical_;
};
}  // namespace cybercanvas
