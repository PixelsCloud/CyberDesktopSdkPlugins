#include <CyberCanvas/Noise.h>
#include <FastNoiseLite.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace cybercanvas {
namespace {
void range(double value, double minimum, double maximum) {
    if (!std::isfinite(value) || value < minimum || value > maximum) {
        throw std::invalid_argument("Noise parameter outside supported range");
    }
}
}  // namespace

struct NoiseField::Impl {
    NoiseSettings settings;
    FastNoiseLite noise;
    double maximumCoordinate;

    explicit Impl(NoiseSettings value) : settings(value), noise(value.seed) {
        if (static_cast<unsigned>(value.algorithm) > 2 || static_cast<unsigned>(value.fractal) > 2 ||
            value.octaves < 1 || value.octaves > 8) {
            throw std::invalid_argument("Invalid noise algorithm, fractal or octave count");
        }
        range(value.frequency, .000001, 1000);
        range(value.lacunarity, 1, 4);
        range(value.gain, 0, 1);
        constexpr FastNoiseLite::NoiseType algorithms[]{
            FastNoiseLite::NoiseType_OpenSimplex2S, FastNoiseLite::NoiseType_Perlin, FastNoiseLite::NoiseType_Value};
        constexpr FastNoiseLite::FractalType fractals[]{FastNoiseLite::FractalType_None, FastNoiseLite::FractalType_FBm,
                                                        FastNoiseLite::FractalType_Ridged};
        noise.SetNoiseType(algorithms[static_cast<unsigned>(value.algorithm)]);
        noise.SetFractalType(fractals[static_cast<unsigned>(value.fractal)]);
        noise.SetFrequency(float(value.frequency));
        noise.SetFractalOctaves(value.octaves);
        noise.SetFractalLacunarity(float(value.lacunarity));
        noise.SetFractalGain(float(value.gain));
        // Keep every octave's transformed coordinates far inside FNL's integer lattice.
        const int layers = value.fractal == NoiseFractal::None ? 1 : value.octaves;
        maximumCoordinate = 4000000. / (value.frequency * std::pow(value.lacunarity, layers - 1));
    }

    void coordinate(double value) const {
        range(value, -maximumCoordinate, maximumCoordinate);
    }
};

NoiseField::NoiseField(NoiseSettings settings) : impl_(std::make_shared<Impl>(settings)) {}

double NoiseField::sample(double x, double y) const {
    impl_->coordinate(x);
    impl_->coordinate(y);
    return std::clamp(double(impl_->noise.GetNoise(x, y)), -1., 1.);
}

double NoiseField::sample(double x, double y, double z) const {
    impl_->coordinate(x);
    impl_->coordinate(y);
    impl_->coordinate(z);
    return std::clamp(double(impl_->noise.GetNoise(x, y, z)), -1., 1.);
}

const NoiseSettings& NoiseField::settings() const {
    return impl_->settings;
}

double NoiseField::animated(Point position, double seconds, double speed) const {
    range(seconds, 0, 1e9);
    range(speed, 0, 10);
    const double phase = seconds * speed / 32.;
    const double radius = 32. / impl_->settings.frequency;
    return sample(position.x + radius * (std::cos(phase) - 1), position.y, radius * std::sin(phase));
}

NoiseWind::NoiseWind(NoiseWindSettings settings)
    : settings_(settings),
      horizontal_({.seed = settings.seed, .frequency = 1}),
      vertical_({.seed = std::bit_cast<int32_t>(uint32_t(settings.seed) ^ 0x5bd1e995u), .frequency = 1}) {
    range(settings.strength, 0, 10000);
    range(settings.spatialFrequency, .000001, 10);
    range(settings.temporalFrequency, 0, 10);
}

Point NoiseWind::operator()(Point position, double seconds) const {
    range(seconds, 0, 1e9);
    const double x = position.x * settings_.spatialFrequency;
    const double y = position.y * settings_.spatialFrequency;
    Point result{horizontal_.animated({x, y}, seconds, settings_.temporalFrequency),
                 vertical_.animated({x, y}, seconds, settings_.temporalFrequency)};
    const double scale = settings_.strength / std::max(1., std::hypot(result.x, result.y));
    return {result.x * scale, result.y * scale};
}
}  // namespace cybercanvas
