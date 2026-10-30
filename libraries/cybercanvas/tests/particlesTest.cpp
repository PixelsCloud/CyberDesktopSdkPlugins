#include <CyberCanvas/Particles.h>

#include <iostream>

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
        return false;
    } catch (const std::exception&) {
        return true;
    }
}
}  // namespace

int main() {
    using namespace cybercanvas;
    try {
        std::vector<ParticleView> reference;
        for (const int rate : {30, 60, 120, 144}) {
            ParticleEmitter emitter;
            for (int i = 0; i < rate * 3; ++i) {
                emitter.advance(1. / rate);
            }
            const auto particles = emitter.particles();
            check("fixed simulation step count", emitter.stats().steps == 360);
            if (reference.empty()) {
                reference = particles;
            }
            check("display rates keep identical particle counts", particles.size() == reference.size());
            bool same = particles.size() == reference.size();
            for (size_t i = 0; same && i < particles.size(); ++i) {
                same = std::hypot(particles[i].position.x - reference[i].position.x,
                                  particles[i].position.y - reference[i].position.y) < 1e-7 &&
                       particles[i].color == reference[i].color;
            }
            check("fixed seed trajectories independent of refresh rate", same);
        }
        ParticleSettings settings;
        settings.rate = 0;
        settings.minimumSpeed = settings.maximumSpeed = 0;
        settings.gravity = {};
        settings.maximum = 8;
        ParticleEmitter local(settings);
        local.burst(20);
        check("particle capacity and dropped emission diagnosed",
              local.stats().alive == 8 && local.stats().dropped == 12);
        local.origin({50, 60});
        check("local particles follow emitter origin", local.particles()[0].position.x == 50);
        settings.space = ParticleSpace::World;
        ParticleEmitter world(settings);
        world.origin({10, 20});
        world.burst(1);
        world.origin({50, 60});
        check("world particles preserve birth origin", world.particles()[0].position.x == 10);
        const auto steps = local.stats().steps;
        local.visible(false);
        local.advance(60);
        check("hidden particles stop simulation", !local.active() && local.stats().steps == steps);
        local.visible(true);
        local.advance(60);
        check("catchup has hard step cap", local.stats().steps - steps == 30 && local.stats().droppedSteps > 1000);
        for (int i = 0; i < 10; ++i) {
            local.advance(.25);
        }
        check("dead nonemitting system stops scheduling", !local.active() && local.stats().alive == 0);
        for (auto shape :
             {EmissionShape::Point, EmissionShape::Line, EmissionShape::Rectangle, EmissionShape::Circle}) {
            settings.shape = shape;
            ParticleEmitter emitter(settings);
            emitter.burst(8);
            for (const auto& p : emitter.particles()) {
                check("emitter sampling stays in extent", std::abs(p.position.x) <= 20 && std::abs(p.position.y) <= 20);
            }
            Canvas canvas;
            std::vector<uint8_t> pixels(64 * 64 * 4);
            emitter.origin({32, 32});
            canvas.render({64, 64, 256, pixels.size(), pixels.data(), 1}, {}, [&](Canvas& target, Rect) {
                emitter.draw(target);
            });
        }
        ParticleEmitter wind(settings);
        wind.burst(1);
        wind.wind([&](Point, double) {
            wind.reset(1);
            return Point{};
        });
        check("reentrant wind callback mutation rejected", rejects([&] {
                  wind.advance(.02);
              }));
        wind.wind([](Point, double) {
            return Point{20, 0};
        });
        wind.advance(.02);
        check("wind applies acceleration", wind.particles()[0].velocity.x > 0);
        check("hard capacity cannot be exceeded", rejects([] {
                  ParticleEmitter invalid({.maximum = 1025});
              }));
        check("unknown emission shape rejected", rejects([] {
                  ParticleEmitter invalid({.shape = static_cast<EmissionShape>(99)});
              }));
        check("invalid elapsed time rejected", rejects([&] {
                  wind.advance(-1);
              }));
        world.reset(settings.seed);
        world.burst(1);
        world.reducedMotion(true);
        world.advance(.1);
        check("reduced motion freezes particles", !world.active() && world.stats().steps == 0);
        {
            ParticleEmitter unchanged;
            unchanged.burst(1);
            unchanged.advance(.005);
            unchanged.visible(true);
            unchanged.advance(.005);
            unchanged.paused(false);
            unchanged.advance(.005);
            unchanged.reducedMotion(false);
            unchanged.advance(.005);
            check("unchanged pause visibility and motion settings retain partial steps", unchanged.stats().steps == 2);
        }
        {
            ParticleSettings red;
            red.rate = 0;
            red.minimumSpeed = red.maximumSpeed = 0;
            red.size = Keyframes<double>{{{0, 12}}};
            red.opacity = Keyframes<double>{{{0, .5}}};
            red.colors = {{0, 0xff0000ff}, {1, 0xff0000ff}};
            ParticleEmitter sprite(red);
            sprite.origin({32, 32});
            sprite.burst(1);
            Canvas canvas;
            for (double dpi : {1., 1.25, 1.5, 2., 3.}) {
                const auto edge = uint32_t(64 * dpi), center = uint32_t(32 * dpi);
                std::vector<uint8_t> pixels(size_t(edge) * edge * 4);
                canvas.render({edge, edge, edge * 4, pixels.size(), pixels.data(), dpi}, {}, [&](Canvas& draw, Rect) {
                    sprite.draw(draw);
                });
                const auto offset = (size_t(center) * edge + center) * 4;
                check("circle particles retain RGBA color alpha and DPI",
                      pixels[offset] >= 126 && pixels[offset] <= 129 && pixels[offset + 2] == 0 &&
                          pixels[offset + 3] >= 126 && pixels[offset + 3] <= 129 && pixels[3] == 0);
            }
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return passed ? 0 : 1;
}
