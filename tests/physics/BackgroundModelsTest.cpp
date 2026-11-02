#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "plugins/beat-machine/Model.h"
#include "plugins/brick-clock/Clock.h"
#include "plugins/deep-whale/BeatScene.h"
#include "plugins/hourglass-suite/PhysicalSand.h"
#include "plugins/marble-clock/Model.h"
#include "plugins/redline-desk/Kinetics.h"
#include "plugins/streetlink-desk/PhysicsModel.h"
#include "sdk/cpp/BackgroundReference.h"

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct Host {
    cdsdk::BackgroundReferenceHost background{cdsdk::BackgroundReferenceHost::Mode::Deterministic};
    CdServiceHostApi api{};
    CdBackgroundApi delegate{};
    uint64_t callbacks = 0, elapsed = 0;

    struct Observed {
        Host* host;
        CdBackgroundTaskDesc description;
    };

    Host() {
        delegate = background.api();
        api.input.media.base.struct_size = sizeof(api);
        api.input.media.base.abi_version = CD_PLUGIN_ABI_VERSION;
        api.query_service = [](void* pointer, const char* id, uint32_t version, void* output,
                               uint32_t capacity) -> CdResult {
            if (std::strcmp(id, CD_SERVICE_BACKGROUND) || version != CD_BACKGROUND_VERSION) {
                return CD_ERROR_UNSUPPORTED;
            }
            if (capacity < sizeof(CdBackgroundApi)) {
                return CD_ERROR_BUFFER;
            }
            auto& host = *static_cast<Host*>(pointer);
            auto result = host.delegate;
            result.context = &host;
            result.create = [](void* pointer, const CdBackgroundTaskDesc* desc, uint64_t* token) -> CdResult {
                auto& host = *static_cast<Host*>(pointer);
                auto observed = std::make_unique<Observed>(Observed{&host, *desc});
                auto wrapped = *desc;
                wrapped.model_context = observed.get();
                wrapped.execute = [](void* pointer, const CdBackgroundWork* work,
                                     CdBackgroundOutput* output) -> CdResult {
                    auto& observed = *static_cast<Observed*>(pointer);
                    ++observed.host->callbacks;
                    observed.host->elapsed = work->elapsed_ns;
                    return observed.description.execute(observed.description.model_context, work, output);
                };
                wrapped.destroy = [](void* pointer) {
                    const std::unique_ptr<Observed> observed(static_cast<Observed*>(pointer));
                    observed->description.destroy(observed->description.model_context);
                };
                const auto result = host.delegate.create(host.delegate.context, &wrapped, token);
                if (result == CD_OK) {
                    observed.release();
                }
                return result;
            };
            result.submit = [](void* pointer, uint64_t token, const CdBackgroundCommand* command) -> CdResult {
                auto& host = *static_cast<Host*>(pointer);
                return host.delegate.submit(host.delegate.context, token, command);
            };
            result.control = [](void* pointer, uint64_t token, const CdBackgroundControl* control,
                                uint64_t* generation) -> CdResult {
                auto& host = *static_cast<Host*>(pointer);
                return host.delegate.control(host.delegate.context, token, control, generation);
            };
            result.acquire = [](void* pointer, uint64_t token, CdBackgroundSnapshot* snapshot) -> CdResult {
                auto& host = *static_cast<Host*>(pointer);
                return host.delegate.acquire(host.delegate.context, token, snapshot);
            };
            result.stats = [](void* pointer, uint64_t token, CdBackgroundStats* stats) -> CdResult {
                auto& host = *static_cast<Host*>(pointer);
                return host.delegate.stats(host.delegate.context, token, stats);
            };
            *static_cast<CdBackgroundApi*>(output) = result;
            return CD_OK;
        };
    }

    CdCreateContext context() {
        return {sizeof(CdCreateContext),
                CD_PLUGIN_ABI_VERSION,
                "physics-model-contract",
                "differential",
                "",
                &api.input.media.base,
                this};
    }
};

void same(double a, double b) {
    require(std::isfinite(a) && std::isfinite(b) && std::abs(a - b) <= 1e-5,
            "Background model changed a reference trajectory");
}

void pose(const cybercanvas::physics::BodyState& a, const cybercanvas::physics::BodyState& b) {
    same(a.position.x, b.position.x);
    same(a.position.y, b.position.y);
    same(a.angle, b.angle);
    same(a.velocity.x, b.velocity.x);
    same(a.velocity.y, b.velocity.y);
}

void beat(unsigned frames) {
    for (const auto style : {beatmachine::GeometryStyle::Classic, beatmachine::GeometryStyle::Illustrated,
                             beatmachine::GeometryStyle::Pixels}) {
        for (const bool compact : {false, true}) {
            Host host;
            beatmachine::Model actual(host.context(), 48, compact, style), reference(48, compact, style);
            host.background.tick(0);
            actual.consumeBackground();
            double now = 0;
            beatmachine::Signal signal;
            signal.bassMapped = true;
            for (unsigned frame = 0; frame < frames; ++frame) {
                signal.bass = (frame % 30 < 6) ? .8 : .02;
                signal.middle = .2;
                signal.high = .1;
                if (frame % 30 == 0) {
                    ++signal.beat;
                    ++signal.accent;
                    for (unsigned i = 0; i < 5; ++i) {
                        ++signal.pistonBeat[i];
                        signal.pistonBass[i] = .8;
                        signal.pistonStrength[i] = .7;
                    }
                }
                if (frame == 90) {
                    actual.beginDrag(actual.balls()[0].token, {150, 180});
                    reference.beginDrag(reference.balls()[0].token, {150, 180});
                }
                if (frame == 100) {
                    actual.drag({180, 200});
                    reference.drag({180, 200});
                }
                if (frame == 110) {
                    actual.cancelDrag();
                    reference.cancelDrag();
                }
                actual.advance(1. / 60, signal);
                reference.advance(1. / 60, signal);
                host.background.tick(now += 1. / 60);
                actual.consumeBackground();
                require(actual.stats().steps == reference.stats().steps, "Beat fixed step count changed");
                require(actual.hits() == reference.hits() && actual.ejections() == reference.ejections(),
                        "Beat collision/launch event changed");
                require(actual.balls().size() == reference.balls().size(), "Beat entity count changed");
                for (size_t i = 0; i < actual.balls().size(); ++i) {
                    pose(actual.state(actual.balls()[i].body), reference.state(reference.balls()[i].body));
                }
            }
        }
    }
    std::cout << "Beat: all geometry styles, ordered audio impulses and drag trajectories matched\n";
}

void sand(unsigned frames) {
    for (const bool projected : {false, true}) {
        Host host;
        hourglassplugin::Model timer;
        timer.reset(5);
        hourglassplugin::PhysicalSand actual(host.context()), reference;
        host.background.tick(0);
        actual.consumeBackground();
        double now = 0;
        for (unsigned frame = 0; frame < frames; ++frame) {
            timer.advance(1. / 60, true);
            actual.advance(1. / 60, timer, projected);
            reference.advance(1. / 60, timer, projected);
            host.background.tick(now += 1. / 60);
            actual.consumeBackground();
            require(actual.count() == reference.count() && actual.released() == reference.released(),
                    "Background sand lost grains or changed metering");
            require(actual.stats().steps == reference.stats().steps, "Sand fixed step count changed");
            const auto positions = actual.sample(), oldPositions = reference.sample();
            for (size_t i = 0; i < positions.size(); ++i) {
                same(positions[i].x, oldPositions[i].x);
                same(positions[i].y, oldPositions[i].y);
            }
        }
    }
    std::cout << "Sand: both flip modes, grain conservation and metering matched\n";
}

void kinetics(unsigned frames) {
    for (int kind = 0; kind < 4; ++kind) {
        Host host;
        redline::KineticModel actual(host.context(), kind, {468, 240}), reference(kind, {468, 240});
        double now = 0;
        host.background.tick(now);
        actual.consumeBackground();
        require(!actual.tryBeginDrag({-100, -100}), "Picking accepted a point outside the displayed snapshot");
        for (unsigned frame = 0; frame < frames; ++frame) {
            if (kind < 2 && frame == 90) {
                const auto pivot = reference.debugBodies()[1].state.position;
                const cybercanvas::Point point{pivot.x * 100, pivot.y * 100};
                require(actual.tryBeginDrag(point) && reference.tryBeginDrag(point),
                        "Snapshot picking lost the displayed rotor");
            }
            if (kind < 2 && frame == 100) {
                actual.drag({180, 110});
                reference.drag({180, 110});
            }
            if (kind < 2 && frame == 110) {
                actual.cancelDrag();
                reference.cancelDrag();
            }
            if (frame % 30 == 0) {
                actual.strike(frame / 30 % 3, .8);
                reference.strike(frame / 30 % 3, .8);
            }
            host.background.tick(now += 1. / 60);
            actual.consumeBackground();
            const auto target = actual.stats().steps;
            for (unsigned retry = 0; reference.stats().steps < target && retry < 10; ++retry) {
                reference.advance(1. / 60);
            }
            require(reference.stats().steps == target, "Kinetic comparison used different step numbers");
            const auto bodies = actual.debugBodies(), oldBodies = reference.debugBodies();
            require(bodies.size() == oldBodies.size(), "Kinetic entity count changed");
            for (size_t i = 0; i < bodies.size(); ++i) {
                pose(bodies[i].state, oldBodies[i].state);
            }
        }
    }
    std::cout << "RedLine: all four kinetic models matched at identical fixed steps\n";
}

void clocks(unsigned frames) {
    for (const auto style : {marbleclock::GeometryStyle::Classic, marbleclock::GeometryStyle::Illustrated,
                             marbleclock::GeometryStyle::ExtendedCompact, marbleclock::GeometryStyle::Pixels}) {
        for (const bool compact : {false, true}) {
            Host host;
            marbleclock::Model actual(host.context(), compact, style), reference(compact, style);
            actual.setTime(19, 59, 50, false);
            reference.setTime(19, 59, 50, false);
            host.background.tick(0);
            actual.consumeBackground();
            for (unsigned frame = 1; frame <= frames; ++frame) {
                const int clock = (19 * 3600 + 59 * 60 + 50 + frame / 60) % 86400;
                const bool changed = frame % 60 == 0;
                if (changed) {
                    actual.setTime(clock / 3600, clock / 60 % 60, clock % 60);
                }
                cybercanvas::Point cursor{};
                if (frame == 90) {
                    const auto target = reference.state(reference.marbles().front()).position;
                    cursor = {target.x * 500, target.y * 500};
                }
                if (frame == 90) {
                    require(actual.beginDrag(actual.marbles().front().token, cursor) && actual.dragging(),
                            "Pending background drag lost capture before command acknowledgement");
                }
                if (frame == 100) {
                    actual.drag({200, 200});
                }
                if (frame == 110) {
                    actual.cancelDrag();
                    require(!actual.dragging(), "Drag cancel was not immediate");
                }
                const auto callbacks = host.callbacks;
                host.background.tick(double(frame) / 60);
                actual.consumeBackground();
                if (callbacks != host.callbacks) {
                    reference.advance(double(host.elapsed) / 1e9);
                }
                if (changed) {
                    reference.setTime(clock / 3600, clock / 60 % 60, clock % 60);
                }
                if (frame == 90) {
                    reference.beginDrag(reference.marbles().front().token, cursor);
                }
                if (frame == 100) {
                    reference.drag({200, 200});
                }
                if (frame == 110) {
                    reference.cancelDrag();
                }
                require(actual.stats().steps == reference.stats().steps, "Marble fixed step count changed");
                require(actual.marbles().size() == reference.marbles().size(), "Marble entity count changed");
                same(actual.flapAngle(), reference.flapAngle());
                for (size_t i = 0; i < actual.marbles().size(); ++i) {
                    pose(actual.state(actual.marbles()[i]), reference.state(reference.marbles()[i]));
                }
            }
        }
    }
    std::cout << "Marble: all four geometry styles and minute transitions matched\n";
    for (const auto enclosure : {brickclock::EnclosureGeometry{}, brickclock::EnclosureGeometry{8.85, -3.85, 13.85}}) {
        Host host;
        brickclock::Model actual(host.context(), enclosure, 991), reference(enclosure, 991);
        actual.setChipCount(20);
        reference.setChipCount(20);
        actual.setTime(19, 59, 50, false);
        reference.setTime(19, 59, 50, false);
        host.background.tick(0);
        actual.consumeBackground();
        for (unsigned frame = 1; frame <= frames; ++frame) {
            const int clock = (19 * 3600 + 59 * 60 + 50 + frame / 60) % 86400;
            const bool changed = frame % 60 == 0;
            if (changed) {
                actual.setTime(clock / 3600, clock / 60 % 60, clock % 60);
            }
            const auto callbacks = host.callbacks;
            host.background.tick(double(frame) / 60);
            actual.consumeBackground();
            if (callbacks != host.callbacks) {
                reference.advance(double(host.elapsed) / 1e9);
            }
            if (changed) {
                reference.setTime(clock / 3600, clock / 60 % 60, clock % 60);
            }
            require(actual.stats().steps == reference.stats().steps, "Brick fixed step count changed");
            require(actual.chips().size() == reference.chips().size(), "Brick lost chip geometry");
            for (size_t i = 0; i < actual.chips().size(); ++i) {
                pose(actual.state(actual.chips()[i]), reference.state(reference.chips()[i]));
                same(actual.chipAge(actual.chips()[i]), reference.chipAge(reference.chips()[i]));
            }
        }
    }
    std::cout << "Brick: both enclosures, maximum debris and digit transitions matched\n";
}

void customScenes(unsigned frames) {
    for (const bool clock : {false, true}) {
        Host host;
        streetlink::PhysicsModel actual(host.context()), reference;
        actual.cancelDrag();  // UI initialization cancels capture before first reset.
        for (auto* model : {&actual, &reference}) {
            model->clock = clock;
            if (clock) {
                model->lanes = {{{2, 10}, {30, 15}, {60, 15}}, {{2, 25}, {30, 20}, {60, 20}}};
            } else {
                model->chambers = {{2, 2, 30, 30}, {38, 2, 30, 30}};
            }
            model->reset(clock ? 0 : 48);
        }
        if (clock) {
            for (unsigned i = 0; i < 59; ++i) {
                actual.spawn(i, true);
                reference.spawn(i, true);
            }
        }
        host.background.tick(0);
        actual.consumeBackground();
        double target = 0;
        beatmachine::Signal signal;
        signal.bass = .7;
        signal.middle = .2;
        signal.high = .1;
        for (unsigned frame = 0; frame < frames; ++frame) {
            const double phase = double(frame) / 60;
            if (frame % 30 == 0) {
                actual.kick(2, phase);
                reference.kick(2, phase);
            }
            const auto previous = target;
            target += 1. / 60;
            actual.advancePhysics(1. / 60, signal, true, phase);
            reference.advancePhysics(target - previous, signal, true, phase);
            host.background.tick(target);
            actual.consumeBackground();
            require(actual.stats().steps == reference.stats().steps, "Streetlink fixed step count changed");
            require(actual.particles.size() == reference.particles.size(), "Streetlink entity count changed");
            for (size_t i = 0; i < actual.particles.size(); ++i) {
                pose(actual.state(actual.particles[i].body), reference.state(reference.particles[i].body));
            }
        }
    }
    std::cout << "Streetlink: channel and chamber trajectories/impulses matched\n";
    for (const bool compact : {false, true}) {
        Host host;
        deep_motion::BeatScene actual(host.context()), reference;
        const cybercanvas::Rect area{10, 20, 320, 180};
        actual.resetBeat(area, 48, compact);
        reference.resetBeat(area, 48, compact);
        host.background.tick(0);
        actual.consumeBackground();
        double target = 0;
        beatmachine::Signal signal;
        signal.middle = .2;
        signal.strength = .8;
        for (unsigned frame = 0; frame < frames; ++frame) {
            if (frame % 30 == 0) {
                ++signal.beat;
                ++signal.accent;
            }
            const auto previous = target;
            target += 1. / 60;
            actual.stepBeat(1. / 60, signal);
            reference.stepBeat(target - previous, signal);
            host.background.tick(target);
            actual.consumeBackground();
            require(actual.stats().steps == reference.stats().steps, "Deep Whale fixed step count changed");
            same(actual.pulse, reference.pulse);
            for (size_t i = 0; i < actual.balls.size(); ++i) {
                pose(actual.state(actual.balls[i].body), reference.state(reference.balls[i].body));
            }
            for (size_t i = 0; i < actual.pendulums.size(); ++i) {
                pose(actual.state(actual.pendulums[i].body), reference.state(reference.pendulums[i].body));
            }
        }
    }
    std::cout << "Deep Whale: both footprints, moving floor and audio events matched\n";
}

}  // namespace

int main(int argc, char** argv) try {
    const unsigned frames = argc > 1 ? std::max(600u, unsigned(std::stoul(argv[1]))) : 600;
    beat(frames);
    sand(frames);
    kinetics(frames);
    clocks(frames);
    customScenes(frames);
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
