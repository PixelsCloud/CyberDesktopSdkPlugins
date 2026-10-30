#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <vector>

#ifdef CD_PIXEL_MARBLE_BASELINE
#include "plugins/pixel-desk/MarbleClock.h"
#else
#include "plugins/marble-clock/Model.h"
#endif

namespace {
using Json = nlohmann::json;
std::vector<double> samples;

void record(marbleclock::Model& model) {
    samples.insert(samples.end(), {double(model.phase()), double(model.pending()), double(model.oldMarbles()),
                                   double(model.contacts()), double(model.recycled()), double(model.recoveries()),
                                   double(model.releases()), model.flapAngle()});
    for (const auto& marble : model.marbles()) {
        const auto state = model.state(marble);
        for (double value : {state.position.x, state.position.y, state.velocity.x, state.velocity.y, state.angle}) {
            if (!std::isfinite(value)) {
                throw std::runtime_error("non-finite Pixel state");
            }
            samples.push_back(value);
        }
    }
}

void scenario(bool compact) {
#ifdef CD_PIXEL_MARBLE_BASELINE
    marbleclock::Model model(compact);
#else
    marbleclock::Model model(compact, marbleclock::GeometryStyle::Pixels);
#endif
    const auto& shape = model.shape();
    samples.insert(samples.end(), {shape.width, shape.left, shape.pivot, marbleclock::floorHeight(shape, shape.left)});
    for (const auto* group : {&shape.track, &shape.enclosure, &shape.returning}) {
        for (const auto& segment : *group) {
            samples.insert(samples.end(),
                           {segment.first.x, segment.first.y, segment.second.x, segment.second.y, segment.radius});
        }
    }
    model.setTime(10, 20, 58, false);
    record(model);
    for (unsigned frame = 0; frame < 2100; ++frame) {
        if (frame == 120) {
            model.setTime(10, 20, 59, true);
        }
        if (frame == 240) {
            model.setTime(10, 21, 0, true);
        }
        if (frame >= 360 && frame < 900 && frame % 120 == 0) {
            model.release();
        }
        if (frame == 650 && !model.marbles().empty()) {
            const auto& first = model.marbles().front();
            const auto state = model.state(first);
            const cybercanvas::Point point{state.position.x * marbleclock::kPixelsPerMeter,
                                           state.position.y * marbleclock::kPixelsPerMeter};
            model.beginDrag(first.token, point);
            model.drag({point.x + 35, point.y - 30});
        }
        if (frame == 900) {
            model.cancelDrag();
        }
        if (frame == 1300) {
            model.setTime(10, 21, 15, false);
        }
        if (frame == 1600) {
            model.drain();
        }
        model.advance(frame == 1900 ? 2. : 1. / 120);
        record(model);
    }
}
}  // namespace

int main(int argc, char** argv) try {
    if (argc != 3) {
        throw std::runtime_error("usage: pixel-model --write|--compare result.json");
    }
    scenario(false);
    scenario(true);
    if (std::string(argv[1]) == "--write") {
        std::ofstream(argv[2]) << Json(samples);
    } else {
        std::ifstream input(argv[2]);
        const auto baseline = Json::parse(input).get<std::vector<double>>();
        if (baseline.size() != samples.size()) {
            throw std::runtime_error("Pixel sample count changed");
        }
        double maximum = 0;
        for (size_t index = 0; index < samples.size(); ++index) {
            maximum = std::max(maximum, std::abs(samples[index] - baseline[index]));
        }
        std::cout << "Maximum Pixel trajectory/geometry difference: " << maximum << '\n';
        if (maximum > .000001) {
            throw std::runtime_error("Pixel trajectory/geometry changed");
        }
    }
    std::cout << "PASS: two Pixel sizes, " << samples.size() << " finite geometry/trajectory samples\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
