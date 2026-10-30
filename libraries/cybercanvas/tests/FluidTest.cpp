#include <CyberCanvas/Fluid.h>

#include <chrono>
#include <future>
#include <iostream>
#include <limits>
#include <thread>

namespace {
using namespace cybercanvas;
bool passed = true;

void check(const char* name, bool value) {
    passed &= value;
    std::cout << (value ? "PASS " : "FAIL ") << name << '\n';
}

template <class F>
bool rejects(F action) {
    try {
        action();
        return false;
    } catch (const std::exception&) {
        return true;
    }
}

std::vector<FluidSample> block() {
    std::vector<FluidSample> result;
    for (int y = 20; y < 60; y += 4) {
        for (int x = 20; x < 60; x += 4) {
            result.push_back({{double(x), double(y)}, {}});
        }
    }
    return result;
}

std::vector<FluidSample> container() {
    std::vector<FluidSample> result;
    for (int layer = 0; layer < 3; ++layer) {
        for (int x = -12; x <= 132; x += 4) {
            result.push_back({{double(x), 120. + layer * 4}, {}});
        }
        for (int y = -12; y < 120; y += 4) {
            result.push_back({{-double(layer) * 4, double(y)}, {}});
            result.push_back({{120. + layer * 4, double(y)}, {}});
        }
    }
    return result;
}

bool finite(const FluidSnapshot& snapshot) {
    for (const auto& particle : snapshot.particles) {
        if (!std::isfinite(particle.position.x) || !std::isfinite(particle.position.y) ||
            !std::isfinite(particle.velocity.x) || !std::isfinite(particle.velocity.y)) {
            return false;
        }
    }
    return true;
}

double shear(double viscosity, double tension = 0) {
    FluidSystem fluid({.viscosity = viscosity, .surfaceTension = tension, .gravity = {}});
    auto particles = block();
    for (size_t n = 0; n < particles.size(); ++n) {
        particles[n].velocity.x = n % 2 ? 20 : -20;
    }
    fluid.add(particles);
    for (int i = 0; i < 30; ++i) {
        fluid.advance(1. / 120);
    }
    double energy = 0;
    const auto snapshot = fluid.snapshot();
    for (const auto& particle : snapshot.particles) {
        energy += particle.velocity.x * particle.velocity.x + particle.velocity.y * particle.velocity.y;
    }
    check("viscosity/tension simulation stays finite", finite(snapshot));
    return energy;
}
}  // namespace

int main() {
    using namespace cybercanvas;
    try {
        FluidSnapshot reference;
        for (int fps : {30, 60, 120, 144}) {
            FluidSystem fluid;
            fluid.add(block());
            const auto wall = fluid.boundary(container());
            for (int i = 0; i < fps * 2; ++i) {
                fluid.advance(1. / fps);
            }
            auto snapshot = fluid.snapshot();
            check("liquid pool is finite and conserves particle count",
                  snapshot.particles.size() == 100 && finite(snapshot));
            bool contained = true, spread = false;
            double minX = 1e9, maxX = -1e9, maxY = -1e9;
            for (const auto& particle : snapshot.particles) {
                contained &= particle.position.y < 125 && particle.position.x > -5 && particle.position.x < 125;
                spread |= particle.position.x > 65;
                minX = std::min(minX, particle.position.x);
                maxX = std::max(maxX, particle.position.x);
                maxY = std::max(maxY, particle.position.y);
            }
            check("sampled walls contain falling liquid", contained);
            std::cout << "POOL x=" << minX << ".." << maxX << " yMax=" << maxY << '\n';
            check("pressure solver spreads liquid across floor", spread);
            check("fluid uses fixed 120Hz clock", snapshot.stats.steps == 240);
            if (reference.particles.empty()) {
                reference = snapshot;
            }
            double difference = 0;
            for (size_t n = 0; n < snapshot.particles.size(); ++n) {
                difference += std::hypot(snapshot.particles[n].position.x - reference.particles[n].position.x,
                                         snapshot.particles[n].position.y - reference.particles[n].position.y);
            }
            check("fluid state independent of refresh rate", difference < .001);
            auto reaction = fluid.takeReaction(wall);
            check("liquid transfers nonzero impulse into walls",
                  std::hypot(reaction.impulse.x, reaction.impulse.y) > .001);
            reaction = fluid.takeReaction(wall);
            check("reaction can only be consumed once",
                  reaction.impulse.x == 0 && reaction.impulse.y == 0 && reaction.moment == 0);
            const auto steps = fluid.stats().steps;
            fluid.activity(false);
            fluid.advance(60);
            fluid.activity(true, true);
            fluid.advance(60);
            fluid.activity(true, false, true);
            fluid.advance(60);
            check("hidden pause and reduced motion stop fluid", fluid.stats().steps == steps);
            fluid.activity(true);
            fluid.advance(1. / 120);
            check("resume never catches up hidden elapsed time", fluid.stats().steps == steps + 1);
            fluid.advance(60);
            check("fluid catchup bounded", fluid.stats().steps == steps + 9 && fluid.stats().droppedSteps > 7000);
            fluid.reset();
            check("reset releases all fluid and boundaries",
                  fluid.stats().particles == 0 && fluid.stats().boundaryPoints == 0);
            const auto newWall = fluid.boundary(container());
            check("boundary IDs never alias after reset", newWall != wall);
            check("stale boundary update rejected", rejects([&] {
                      fluid.updateBoundary(wall, container());
                  }));
        }
        const auto low = shear(0), high = shear(.3), tension = shear(0, .5);
        std::cout << "ENERGY low=" << low << " viscous=" << high << " tension=" << tension << '\n';
        check("higher viscosity dissipates shear", high < low * .8);
        check("surface tension changes actual motion", std::abs(tension - low) > .001);
        FluidSystem limited({.maximum = 4});
        check("particle budget rejects atomically", rejects([&] {
                                                        limited.add(block());
                                                    }) &&
                                                        limited.stats().particles == 0);
        auto invalid = block();
        invalid[0].position.x = std::numeric_limits<double>::quiet_NaN();
        FluidSystem validated;
        check("NaN rejected before entering native solver", rejects([&] {
                                                                validated.add(invalid);
                                                            }) &&
                                                                validated.stats().particles == 0);
        check("settings validate before initialization", rejects([] {
                  FluidSystem invalid({.viscosity = -1});
              }));
        check("negative elapsed rejected", rejects([&] {
                  validated.advance(-1);
              }));
        {
            FluidSystem fast({.surfaceTension = 0, .gravity = {}});
            const FluidSample particle{{50, 100}, {0, 10000}};
            fast.add(std::span(&particle, 1));
            fast.boundary(container());
            fast.advance(1. / 120);
            check("swept collision stops fast isolated spray", fast.snapshot().particles[0].position.y < 120);
        }
        {
            FluidSystem moving({.viscosity = .2, .surfaceTension = 0, .gravity = {}});
            const FluidSample particle{{50, 48}, {}};
            moving.add(std::span(&particle, 1));
            std::vector<FluidSample> paddle;
            for (int x = 36; x <= 64; x += 4) {
                paddle.push_back({{double(x), 52}, {0, -50}});
            }
            const auto id = moving.boundary(paddle);
            for (int n = 0; n < 10; ++n) {
                for (auto& sample : paddle) {
                    sample.position.y -= 50. / 120;
                }
                moving.updateBoundary(id, paddle);
                moving.advance(1. / 120);
            }
            check("moving boundary transfers motion into fluid", moving.snapshot().particles[0].position.y < 47);
        }
        {
            FluidSystem a, b;
            a.add(block());
            b.add(block());
            a.boundary(container());
            b.boundary(container());
            const auto untouched = b.snapshot();
            bool stable = true;
            for (int n = 0; n < 1200; ++n) {
                a.advance(1. / 120);
                if (n % 120 == 0) {
                    stable &= finite(a.snapshot());
                }
            }
            check("ten-second pool remains bounded and finite",
                  stable && a.stats().particles == 100 && a.snapshot().particles[0].position.y < 125);
            check("independent solver instances do not share state",
                  b.stats().steps == 0 && b.snapshot().particles[0].position.y == untouched.particles[0].position.y);
        }
        {
            FluidSystem larger({.surfaceTension = 0, .gravity = {}});
            std::vector<FluidSample> samples;
            for (int y = 0; y < 32; ++y) {
                for (int x = 0; x < 32; ++x) {
                    samples.push_back({{x * 4., y * 4.}, {}});
                }
            }
            larger.add(samples);
            const auto started = std::chrono::steady_clock::now();
            for (int n = 0; n < 120; ++n) {
                larger.advance(1. / 120);
            }
            std::cout << "TIMING 1024 particles, mean outer step ms="
                      << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count() /
                             120
                      << '\n';
            check("default-capacity fluid pool stays finite",
                  larger.stats().particles == 1024 && finite(larger.snapshot()));
        }
        {
            Canvas canvas;
            FluidSnapshot sample;
            sample.radius = 8;
            sample.particles.push_back({{32, 32}, {}});
            for (double dpi : {1., 1.25, 1.5, 2., 3.}) {
                const auto edge = static_cast<uint32_t>(64 * dpi), center = static_cast<uint32_t>(32 * dpi);
                std::vector<uint8_t> pixels(size_t(edge) * edge * 4);
                canvas.render({edge, edge, edge * 4, pixels.size(), pixels.data(), dpi}, {}, [&](Canvas& draw, Rect) {
                    sample.draw(draw, 0xff000080);
                });
                const auto offset = (size_t(center) * edge + center) * 4;
                bool antialias = false;
                for (size_t p = 3; p < pixels.size(); p += 4) {
                    antialias |= pixels[p] > 0 && pixels[p] < 125;
                }
                check("fluid draw preserves RGBA transparency actual DPI and antialiasing",
                      pixels[offset] >= 126 && pixels[offset] <= 129 && pixels[offset + 2] == 0 &&
                          pixels[offset + 3] >= 126 && pixels[offset + 3] <= 129 && pixels[3] == 0 && antialias);
            }
        }

#ifdef CYBERCANVAS_FLUID_BOX2D
        FluidSystem fluid;
        fluid.add(block());
        physics::World world({.gravity = {}, .fixedStep = 1. / 120, .maximumSteps = 1});
        const auto body = world.createBody({.position = {0, 1.2}});
        world.box(body, {2, .1}, {}, 0, {.density = 1000});
        std::vector<Point> local;
        for (int layer = 0; layer < 3; ++layer) {
            for (int x = -40; x < 160; x += 4) {
                local.push_back({double(x), layer * 4.});
            }
        }
        {
            FluidBodyBinding binding(fluid, world, body, local);
            for (int i = 0; i < 90; ++i) {
                binding.synchronize();
                fluid.advance(1. / 120);
                binding.apply();
                world.advance(1. / 120);
            }
            check("fluid reaction moves a Box2D body", world.state(body).position.y > 1.2001);
        }
        check("binding releases boundary samples", fluid.stats().boundaryPoints == 0);
#endif

        FluidWorker worker;
        std::promise<void> initialized;
        auto ready = initialized.get_future();
        check("worker accepts initialization", worker.submit([&](FluidSystem& fluid) {
            fluid.add(block());
            initialized.set_value();
        }));
        check("worker initializes off caller thread",
              ready.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
        worker.advance(.05);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (worker.snapshot()->stats.steps < 6 && worker.snapshot()->error.empty() &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check("worker publishes immutable advanced snapshot",
              worker.snapshot()->stats.steps == 6 && worker.snapshot()->error.empty());
        worker.activity(false);
        check("hidden worker rejects elapsed", !worker.advance(10));
        const auto held = worker.snapshot();
        worker.activity(true);
        worker.advance(1. / 120);
        const auto resumeDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (worker.snapshot()->stats.steps < 7 && std::chrono::steady_clock::now() < resumeDeadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check("old snapshots remain immutable while worker advances",
              held->stats.steps == 6 && worker.snapshot()->stats.steps == 7);
        worker.activity(false);
        std::promise<void> blocked, release;
        auto releaseFuture = release.get_future().share();
        auto blockedFuture = blocked.get_future();
        worker.submit([&blocked, releaseFuture](FluidSystem&) {
            blocked.set_value();
            releaseFuture.wait();
        });
        const bool waiting = blockedFuture.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
        size_t accepted = 0;
        for (int i = 0; i < 70; ++i) {
            accepted += worker.submit([](FluidSystem&) {
            });
        }
        release.set_value();
        check("worker command queue bounded", waiting && accepted == 64);
    } catch (const std::exception& error) {
        std::cout << "FAIL exception: " << error.what() << '\n';
        return 1;
    }
    return passed ? 0 : 1;
}
