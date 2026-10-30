#include <CyberCanvas/Noise.h>
#include <CyberCanvas/Particles.h>

#include <future>
#include <iostream>
#include <limits>
#include <numbers>

namespace {
bool passed = true;

void check(const char* name, bool value) {
    passed &= value;
    std::cout << (value ? "PASS " : "FAIL ") << name << '\n';
}

template <class F>
bool rejects(F action) {
    try {
        action();
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}
}  // namespace

int main() {
    using namespace cybercanvas;
    try {
        const NoiseField field({.seed = 42}), same({.seed = 42}), other({.seed = 43});
        check("seed recreates field", field.sample(19.25, -33.1) == same.sample(19.25, -33.1));
        check("different seeds change field", field.sample(19.25, -33.1) != other.sample(19.25, -33.1));
        bool bounded = true, continuous = true, varying = false;
        for (auto algorithm : {NoiseAlgorithm::OpenSimplex2S, NoiseAlgorithm::Perlin, NoiseAlgorithm::Value}) {
            for (auto fractal : {NoiseFractal::None, NoiseFractal::FBm, NoiseFractal::Ridged}) {
                const NoiseField noise({.algorithm = algorithm, .fractal = fractal});
                for (int i = -100; i < 100; ++i) {
                    const double x = i * 13.17;
                    const double a = noise.sample(x, -x * .7), b = noise.sample(x, 17, x * .3);
                    bounded &= std::isfinite(a) && std::abs(a) <= 1 && std::isfinite(b) && std::abs(b) <= 1;
                    continuous &= std::abs(a - noise.sample(x + .001, -x * .7)) < .001;
                    varying |= std::abs(a - b) > .01;
                }
            }
        }
        check("all algorithms/fractals bounded in 2D and 3D", bounded);
        check("small coordinate changes remain smooth", continuous);
        check("field varies across space", varying);
        check("invalid frequency rejected", rejects([] {
                  NoiseField noise({.frequency = 0});
              }));
        check("octave budget enforced", rejects([] {
                  NoiseField noise({.octaves = 9});
              }));
        check("invalid algorithm rejected", rejects([] {
                  NoiseField noise({.algorithm = NoiseAlgorithm(99)});
              }));
        check("invalid fractal rejected", rejects([] {
                  NoiseField noise({.fractal = NoiseFractal(99)});
              }));
        check("NaN settings rejected", rejects([] {
                  NoiseField noise({.gain = std::numeric_limits<double>::quiet_NaN()});
              }));
        check("infinite coordinate rejected", rejects([&] {
                  field.sample(std::numeric_limits<double>::infinity(), 0);
              }));
        check("lattice overflow rejected", rejects([&] {
                  field.sample(1e30, 0);
              }));
        check("negative time rejected", rejects([&] {
                  field.animated({}, -1);
              }));
        check("zero speed freezes animation", field.animated({33, 41}, 10, 0) == field.animated({33, 41}, 1000, 0));
        const double period = 64 * std::numbers::pi / .3;
        check("time loop closes without a jump",
              std::abs(field.animated({33, 41}, 0) - field.animated({33, 41}, period)) < 1e-6);
        check("time loop is smooth across seam",
              std::abs(field.animated({33, 41}, period - .001) - field.animated({33, 41}, period + .001)) < .001);
        check("long uptime stays finite", std::isfinite(field.animated({33, 41}, 1e9)));
        const NoiseField detailed({.frequency = 1, .octaves = 8, .lacunarity = 4});
        check("maximum detail supports animated path", std::isfinite(detailed.animated({}, 300)));
        auto sum = [field] {
            double result = 0;
            for (int i = 0; i < 1000; ++i) {
                result += field.sample(i * .17, -23.5);
            }
            return result;
        };
        auto concurrent = std::async(std::launch::async, sum);
        check("copied immutable fields permit concurrent sampling", concurrent.get() == sum());

        const NoiseWind wind({.seed = 42, .strength = 7});
        bool windBounded = true;
        for (int i = 0; i < 1000; ++i) {
            const auto value = wind({i * .3, 15}, i * .1);
            windBounded &= std::isfinite(value.x) && std::isfinite(value.y) && std::hypot(value.x, value.y) <= 7.000001;
        }
        check("wind acceleration magnitude bounded", windBounded);
        const NoiseWind calm({.strength = 0});
        check("zero strength produces no force", calm({15, 5}, 12).x == 0 && calm({15, 5}, 12).y == 0);
        check("nonfinite wind settings rejected", rejects([] {
                  NoiseWind noise({.temporalFrequency = std::numeric_limits<double>::infinity()});
              }));
        std::vector<ParticleView> reference;
        for (int rate : {30, 60, 120, 144}) {
            ParticleEmitter emitter;
            emitter.wind(wind);
            for (int i = 0; i < rate * 3; ++i) {
                emitter.advance(1. / rate);
            }
            auto particles = emitter.particles();
            if (reference.empty()) {
                reference = particles;
            }
            bool equal = particles.size() == reference.size();
            for (size_t i = 0; equal && i < particles.size(); ++i) {
                equal = std::hypot(particles[i].position.x - reference[i].position.x,
                                   particles[i].position.y - reference[i].position.y) < 1e-7;
            }
            check("noise-driven particles invariant across display rates", equal);
            const auto steps = emitter.stats().steps;
            emitter.paused(true);
            emitter.advance(.2);
            check("paused wind does not advance simulation", emitter.stats().steps == steps);
            emitter.paused(false);
            emitter.visible(false);
            emitter.advance(.2);
            check("hidden wind does not advance simulation", emitter.stats().steps == steps);
            emitter.visible(true);
            emitter.reducedMotion(true);
            emitter.advance(.2);
            check("reduced motion does not advance simulation", emitter.stats().steps == steps);
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
    return passed ? 0 : 1;
}
