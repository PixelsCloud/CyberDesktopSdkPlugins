#include <CyberCanvas/Physics2D.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>

#include "plugins/beat-machine/Model.h"
#include "plugins/brick-clock/Clock.h"
#include "plugins/deep-whale/BeatScene.h"
#include "plugins/hourglass-suite/PhysicalSand.h"
#include "plugins/marble-clock/Model.h"
#include "plugins/physics-toys/Model.h"
#include "plugins/redline-desk/Kinetics.h"
#include "plugins/streetlink-desk/Physics.h"
#include "sdk/tools/Runtime.h"

namespace {
using Json = nlohmann::json;
using cybercanvas::physics::PhysicsStats;

struct Peak {
    PhysicsStats value;

    void sample(PhysicsStats current) {
        value.bodies = std::max(value.bodies, current.bodies);
        value.shapes = std::max(value.shapes, current.shapes);
        value.joints = std::max(value.joints, current.joints);
        value.steps = std::max(value.steps, current.steps);
    }

    Json report(const std::string& mode) const {
        if (!value.bodies || !value.steps) {
            throw std::runtime_error("Capacity fixture did not exercise a World: " + mode);
        }
        return {{"configuration", mode},  {"bodies", value.bodies},
                {"shapes", value.shapes}, {"joints", value.joints},
                {"steps", value.steps},   {"bodyStateBytes", value.bodies * sizeof(cybercanvas::physics::BodyState)}};
    }
};
}  // namespace

int main(int argc, char** argv) try {
    if (argc != 3) {
        throw std::runtime_error("Usage: model-capacity-tests built-plugin-root report.json");
    }
    const auto packages = cdsdk::utf8Path(argv[1]);
    Json report{{"status", "measured"}, {"mode", "synchronous model capacity reference; no UI isolation claim"}};
    for (const bool compact : {false, true}) {
        for (const auto style : {beatmachine::GeometryStyle::Classic, beatmachine::GeometryStyle::Illustrated,
                                 beatmachine::GeometryStyle::Pixels}) {
            beatmachine::Model model(48, compact, style);
            Peak peak;
            for (unsigned frame = 0; frame < 240; ++frame) {
                beatmachine::Signal signal;
                signal.beat = frame / 30;
                signal.bass = signal.strength = .7;
                model.advance(1. / 60, signal);
                peak.sample(model.stats());
            }
            report["beat-machine"].push_back(
                peak.report("48 balls/style=" + std::to_string(int(style)) + "/compact=" + std::to_string(compact)));
        }
        for (const auto style : {marbleclock::GeometryStyle::Classic, marbleclock::GeometryStyle::Illustrated,
                                 marbleclock::GeometryStyle::ExtendedCompact, marbleclock::GeometryStyle::Pixels}) {
            marbleclock::Model model(compact, style);
            Peak peak;
            model.setTime(12, 0, 0, false);
            for (unsigned marble = 0; marble < marbleclock::kMaximumMarbles; ++marble) {
                model.release();
            }
            peak.sample(model.stats());
            for (unsigned frame = 0; frame < 7200; ++frame) {
                model.setTime(12, int(frame / 3600), int(frame / 60 % 60), true);
                model.advance(1. / 60);
                peak.sample(model.stats());
            }
            report["marble-clock"].push_back(
                peak.report("120 seconds/style=" + std::to_string(int(style)) + "/compact=" + std::to_string(compact)));
        }
        for (const auto kind : {toys::Kind::Chime, toys::Kind::Clock}) {
            for (const bool mechanical : {false, true}) {
                toys::Model model(kind, compact, mechanical);
                model.setWindSeed(721);
                Peak peak;
                for (unsigned frame = 0; frame < 240; ++frame) {
                    model.advance(1. / 60, 1, 2);
                    peak.sample(model.stats());
                }
                report["physics-toys"].push_back(peak.report("kind=" + std::to_string(int(kind)) +
                                                             "/compact=" + std::to_string(compact) +
                                                             "/mechanical=" + std::to_string(mechanical)));
            }
        }
        deep_motion::BeatScene scene;
        scene.resetBeat({0, 0, compact ? 320. : 480., 200}, 48, compact);
        Peak whale;
        for (unsigned frame = 0; frame < 240; ++frame) {
            beatmachine::Signal signal;
            signal.beat = frame / 30;
            signal.bass = signal.strength = .7;
            scene.stepBeat(1. / 60, signal);
            whale.sample(scene.world->stats());
        }
        report["deep-whale-beat"].push_back(whale.report("48 pearls/compact=" + std::to_string(compact)));
    }
    hourglassplugin::Model timer;
    hourglassplugin::PhysicalSand sand;
    Peak grains;
    for (unsigned frame = 0; frame < 2400; ++frame) {
        timer.advance(1. / 60, true);
        sand.advance(1. / 60, timer);
        grains.sample(sand.stats());
    }
    report["physical-sand"].push_back(grains.report("all " + std::to_string(sand.count()) + " grains; full flip"));
    brickclock::Model brick;
    brick.setChipCount(20);
    brick.setTime(19, 59, 59, false);
    brick.setTime(20, 0, 0, true);
    Peak chips;
    for (unsigned frame = 0; frame < 240; ++frame) {
        brick.advance(1. / 60);
        chips.sample(brick.stats());
    }
    report["brick-clock"].push_back(chips.report("20 chips per digit; all six digits carving"));
    for (int kind = 0; kind < 4; ++kind) {
        redline::KineticModel model(kind, {480, 200});
        Peak peak;
        for (unsigned frame = 0; frame < 240; ++frame) {
            if (frame % 30 == 0) {
                model.strike(frame / 30);
            }
            model.advance(1. / 60);
            peak.sample(model.stats());
        }
        report["redline-kinetics"].push_back(peak.report("kind=" + std::to_string(kind)));
    }
    for (const auto* kind : {"beat-machine", "clock.marble"}) {
        for (const auto* size : {"4x2", "6x2"}) {
            const auto id = std::string("com.cyberdesktop.streetlink.") + kind + "." + size;
            const auto encodedDirectory = (packages / id).u8string();
            const std::string directory(reinterpret_cast<const char*>(encodedDirectory.data()),
                                        encodedDirectory.size());
            cdsdk::MockHost host;
            host.baseUnixMs = 1793404800000ull;
            CdCreateContext context{
                sizeof(context), CD_PLUGIN_ABI_VERSION, id.c_str(), "capacity", directory.c_str(), &host.api, &host};
            streetlink::Physics model(context);
            if (std::string(kind) == "beat-machine") {
                const CdSettingValue maximum{sizeof(maximum), CD_SETTING_CHOICE, "balls", 0, 0, "48"};
                if (model.applySettings(&maximum, 1) != CD_OK) {
                    throw std::runtime_error("Could not select maximum Streetlink density");
                }
            } else {
                std::vector<uint8_t> pixels(320 * 200 * 4);
                const CdFrame image{sizeof(image), 320, 200, 320 * 4, pixels.size(), pixels.data(), 1};
                if (model.render(image) != CD_OK) {
                    throw std::runtime_error("Cannot arrange Streetlink maximum-input fixture");
                }
                for (unsigned bead = 0; bead < 80; ++bead) {
                    const CdInput press{sizeof(press), CD_INPUT_PRESS, CD_BUTTON_LEFT, 0, 1, 1, 0};
                    const CdInput release{sizeof(release), CD_INPUT_RELEASE, 0, 0, 1, 1, 0};
                    model.input(press);
                    model.input(release);
                }
            }
            Peak peak;
            peak.sample(model.physicsStats());
            for (unsigned frame = 0; frame < 7200; ++frame) {
                host.tick(double(frame) / 60);
                peak.sample(model.physicsStats());
            }
            report["streetlink-physics"].push_back(peak.report(id + "/maximum official density"));
        }
    }
    std::ofstream output(cdsdk::utf8Path(argv[2]));
    output << report.dump(2) << '\n';
    if (!output) {
        throw std::runtime_error("Cannot write model capacity report");
    }
    std::cout << "All eight model capacity profiles measured\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
