#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <vector>

#ifdef CD_PIXEL_PHYSICS_BASELINE
#include "plugins/beat-machine/Machine.h"
#include "plugins/physics-toys/Toys.h"
#else
#include "plugins/beat-machine/Model.h"
#include "plugins/physics-toys/Model.h"
#endif
#include "plugins/brick-clock/Clock.h"

namespace {
using Json = nlohmann::json;
std::vector<double> samples;

void record(const cybercanvas::physics::BodyState& state) {
    for (double value : {state.position.x, state.position.y, state.velocity.x, state.velocity.y, state.angle}) {
        if (!std::isfinite(value)) {
            throw std::runtime_error("non-finite Pixel physics state");
        }
        samples.push_back(value);
    }
}

void beat(bool compact) {
#ifdef CD_PIXEL_PHYSICS_BASELINE
    beatmachine::Model model(compact ? 12 : 36, compact);
#else
    beatmachine::Model model(compact ? 12 : 36, compact, beatmachine::GeometryStyle::Pixels);
#endif
    samples.push_back(model.width());
    samples.push_back(double(model.balls().size()));
    for (const auto& rail : model.rails()) {
        samples.insert(samples.end(), {rail.first.x, rail.first.y, rail.second.x, rail.second.y, rail.radius});
    }
    for (const auto& point : model.bumpers()) {
        samples.insert(samples.end(), {point.x, point.y});
    }
    beatmachine::Signal signal;
    signal.bass = .7;
    signal.middle = .35;
    for (unsigned frame = 0; frame < 360; ++frame) {
        if (frame % 30 == 0) {
            ++signal.beat;
            model.strike(frame / 30 % 5);
        }
        if (frame == 40) {
            const auto& first = model.balls().front();
            const auto state = model.state(first.body);
            model.beginDrag(first.token, {state.position.x * 500, state.position.y * 500});
        }
        if (frame == 42) {
            model.drag({200, 100});
        }
        if (frame == 50) {
            model.cancelDrag();
        }
        if (frame == 100) {
            model.quiet();
        }
        model.advance(frame == 160 ? 3. : 1. / 60, signal);
        for (const auto& ball : model.balls()) {
            record(model.state(ball.body));
        }
        for (const auto& actuator : model.pistons()) {
            record(model.state(actuator.body));
        }
        for (const auto& actuator : model.paddles()) {
            record(model.state(actuator.body));
        }
    }
    model.reset(24);
    samples.push_back(double(model.balls().size()));
}

void toy(bool compact, toys::Kind kind) {
#ifdef CD_PIXEL_PHYSICS_BASELINE
    toys::Model model(kind, compact, true);
#else
    toys::Model model(kind, compact, true, {.pixels = true, .separateEnclosure = true});
#endif
    model.setWindSeed(42);
    model.setTime(11, 22, 33, false, false);
    samples.insert(samples.end(), {model.size().width, model.size().height});
    for (const auto& piece : model.pieces()) {
        samples.insert(samples.end(), {piece.anchor.x, piece.anchor.y, piece.attachment.x, piece.attachment.y,
                                       piece.half.width, piece.half.height, double(piece.art)});
    }
    for (unsigned frame = 0; frame < 360; ++frame) {
        if (frame == 30) {
            model.pluck();
        }
        if (frame == 40) {
            model.beginDrag(0, {.8, 1.2});
        }
        if (frame == 42) {
            model.drag({1, 1.4});
        }
        if (frame == 50) {
            model.cancelDrag();
        }
        model.advance(frame == 160 ? 3. : 1. / 60, .3, 1.);
        for (size_t index = 0; index < model.pieces().size(); ++index) {
            record(model.state(index));
        }
    }
    model.reset(compact);
    samples.push_back(double(model.pieces().size()));
}

void brick() {
#ifdef CD_PIXEL_PHYSICS_BASELINE
    brickclock::Model model;
#else
    brickclock::Model model(brickclock::EnclosureGeometry{8.85, -3.85, 13.85});
#endif
    model.setChipCount(12);
    model.setTime(12, 34, 58, false);
    for (unsigned frame = 0; frame < 600; ++frame) {
        if (frame % 60 == 0) {
            const unsigned second = 58 + frame / 60;
            model.setTime(12, 34 + second / 60, second % 60, true);
        }
        model.advance(1. / 60);
        for (const auto& carving : model.carvings()) {
            samples.insert(samples.end(), {double(carving.previous), carving.elapsed, double(carving.emitted)});
        }
        for (const auto& chip : model.chips()) {
            record(model.state(chip));
        }
    }
}
}  // namespace

int main(int argc, char** argv) try {
    if (argc != 3) {
        throw std::runtime_error("usage: pixel-physics --write|--compare result.json");
    }
    for (bool compact : {false, true}) {
        beat(compact);
        toy(compact, toys::Kind::Clock);
        toy(compact, toys::Kind::Chime);
    }
    brick();
    if (std::string(argv[1]) == "--write") {
        std::ofstream(argv[2]) << Json(samples);
    } else {
        std::ifstream input(argv[2]);
        const auto baseline = Json::parse(input).get<std::vector<double>>();
        if (baseline.size() != samples.size()) {
            throw std::runtime_error("Pixel physics sample count changed");
        }
        double maximum = 0;
        for (size_t index = 0; index < samples.size(); ++index) {
            maximum = std::max(maximum, std::abs(samples[index] - baseline[index]));
        }
        std::cout << "Maximum Pixel beat/toys trajectory/geometry difference: " << maximum << '\n';
        if (maximum > .000001) {
            throw std::runtime_error("Pixel beat/toys trajectory/geometry changed");
        }
    }
    std::cout << "PASS: seven Pixel beat/toys/brick scenarios, " << samples.size() << " finite samples\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
