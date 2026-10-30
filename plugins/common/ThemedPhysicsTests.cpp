#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>

#include "plugins/mecha-desk/controllers/toys/Toys.h"
#include "plugins/mechanical-desk/controllers/toys/Toys.h"
#include "plugins/redline-desk/controllers/beat/Machine.h"
#include "plugins/redline-desk/controllers/marble/Clock.h"
#include "plugins/redline-desk/controllers/toys/Toys.h"
#include "plugins/songyun-desk/controllers/beat/Machine.h"
#include "plugins/songyun-desk/controllers/marble/Clock.h"
#include "plugins/songyun-desk/controllers/toys/Toys.h"
#include "plugins/vampire-alice/controllers/beat/Machine.h"
#include "plugins/vampire-alice/controllers/marble/Clock.h"
#include "plugins/vampire-alice/controllers/toys/Toys.h"

namespace {
using Json = nlohmann::json;
std::vector<double> samples;

void record(const cybercanvas::physics::BodyState& state) {
    for (double value : {state.position.x, state.position.y, state.velocity.x, state.velocity.y, state.angle}) {
        if (!std::isfinite(value)) {
            throw std::runtime_error("non-finite model state");
        }
        samples.push_back(value);
    }
}

template <class Model>
void beat(const std::filesystem::path& assets, bool compact, bool registered) {
    Model model(36, compact);
    if constexpr (requires { model.configure(assets); }) {
        if (registered) {
            model.configure(assets);
        }
    }
    beatmachine::Signal signal;
    signal.bass = .7;
    signal.middle = .35;
    for (int frame = 0; frame < 240; ++frame) {
        if (frame % 30 == 0) {
            ++signal.beat;
            model.strike(unsigned(frame / 30) % 5);
        }
        if (frame == 40) {
            const auto& ball = model.balls().front();
            const auto p = model.state(ball.body).position;
            model.beginDrag(ball.token, {p.x * 500, p.y * 500});
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
    }
    model.reset(24);
    samples.push_back(double(model.balls().size()));
}

template <class Model>
void marble(const std::filesystem::path& assets, bool compact, bool registered) {
    Model model(compact);
    if constexpr (requires { model.configure(assets); }) {
        if (registered) {
            model.configure(assets);
        }
    }
    model.setTime(12, 34, 25, false);
    for (int frame = 0; frame < 240; ++frame) {
        if (frame == 30) {
            const auto& ball = model.marbles().front();
            const auto p = model.state(ball).position;
            model.beginDrag(ball.token, {p.x * 500, p.y * 500});
        }
        if (frame == 35) {
            model.drag({200, 140});
        }
        if (frame == 45) {
            model.cancelDrag();
        }
        if (frame == 90) {
            model.setTime(12, 35, 0, true);
        }
        if (frame == 180) {
            model.quiet();
        }
        model.advance(frame == 160 ? 3. : 1. / 60);
        samples.push_back(double(model.marbles().size()));
        for (const auto& ball : model.marbles()) {
            record(model.state(ball));
        }
    }
    model.setTime(8, 0, 10, false);
    samples.push_back(double(model.marbles().size()));
}

template <class Model>
void toy(const std::filesystem::path& assets, bool compact, bool registered, toys::Kind kind, bool fitted = false) {
    Model model(kind, compact, true);
    if constexpr (requires { model.configure(assets); }) {
        if (registered) {
            model.configure(assets);
        }
    }
    if constexpr (requires {
                      model.fit(cybercanvas::Size{}, cybercanvas::Rect{},
                                std::function<cybercanvas::Point(cybercanvas::Point)>{});
                  }) {
        if (fitted) {
            model.fit({360, 400}, {5, 5, 350, 390}, [](cybercanvas::Point point) {
                return cybercanvas::Point{point.x + 2, point.y + 3};
            });
        }
    }
    model.setWindSeed(721);
    model.setTime(12, 34, 56, false, false);
    for (int frame = 0; frame < 240; ++frame) {
        if (frame == 30) {
            const auto p = model.state(0).position;
            model.beginDrag(0, p);
        }
        if (frame == 35) {
            model.drag({1.5, 1.2});
        }
        if (frame == 45) {
            model.cancelDrag();
        }
        if (frame == 100) {
            model.pluck();
        }
        model.advance(frame == 160 ? 3. : 1. / 60, .3, 1.);
        for (size_t i = 0; i < model.pieces().size(); ++i) {
            record(model.state(i));
        }
    }
    model.reset(compact);
    samples.push_back(double(model.pieces().size()));
}
}  // namespace

int main(int argc, char** argv) try {
    if (argc != 4) {
        throw std::runtime_error("usage: physics-tests source-root --write|--compare result.json");
    }
    const std::filesystem::path root = argv[1];
    for (bool compact : {false, true}) {
        beat<beatmachine::Model>({}, compact, false);
        std::cout << "beatmachine::Model " << compact << " sample end " << samples.size() << '\n';
        marble<marbleclock::Model>({}, compact, false);
        std::cout << "marbleclock::Model " << compact << " sample end " << samples.size() << '\n';
        toy<toys::Model>({}, compact, false, toys::Kind::Clock);
        std::cout << "toys::Model " << compact << " sample end " << samples.size() << '\n';
        toy<toys::Model>({}, compact, false, toys::Kind::Chime);
        std::cout << "toys::Model " << compact << " sample end " << samples.size() << '\n';
        beat<alice_beat::Model>(root / "plugins/vampire-alice/assets/beat-machine-6x2", compact, false);
        std::cout << "alice_beat::Model " << compact << " sample end " << samples.size() << '\n';
        marble<alice_marble::Model>(root / "plugins/vampire-alice/assets/marble-clock-6x2", compact, false);
        std::cout << "alice_marble::Model " << compact << " sample end " << samples.size() << '\n';
        beat<redline_beat::Model>(root / "plugins/redline-desk/assets/beat-machine-6x2", compact, false);
        std::cout << "redline_beat::Model " << compact << " sample end " << samples.size() << '\n';
        marble<redline_marble::Model>(root / "plugins/redline-desk/assets/marble-clock-6x2", compact, false);
        std::cout << "redline_marble::Model " << compact << " sample end " << samples.size() << '\n';
        beat<songyun_beat::Model>(root / "plugins/songyun-desk/assets/beat-machine-6x2", compact, true);
        std::cout << "songyun_beat::Model " << compact << " sample end " << samples.size() << '\n';
        marble<songyun_marble::Model>(root / "plugins/songyun-desk/assets/marble-clock-6x2", compact, true);
        std::cout << "songyun_marble::Model " << compact << " sample end " << samples.size() << '\n';
        toy<alice_toys::Model>(root / "plugins/vampire-alice/assets/wind-chime-2x2", compact, false, toys::Kind::Chime);
        std::cout << "alice_toys::Model " << compact << " sample end " << samples.size() << '\n';
        toy<alice_toys::Model>(root / "plugins/vampire-alice/assets/spring-clock-4x1", compact, false,
                               toys::Kind::Clock);
        std::cout << "alice_toys::Model " << compact << " sample end " << samples.size() << '\n';
        toy<redline_toys::Model>(root / "plugins/redline-desk/assets/wind-chime-2x2", compact, true, toys::Kind::Chime);
        std::cout << "redline_toys::Model " << compact << " sample end " << samples.size() << '\n';
        toy<redline_toys::Model>(root / "plugins/redline-desk/assets/spring-clock-4x1", compact, true,
                                 toys::Kind::Clock);
        std::cout << "redline_toys::Model " << compact << " sample end " << samples.size() << '\n';
        toy<songyun_toys::Model>(root / "plugins/songyun-desk/assets/wind-chime-2x2", compact, true, toys::Kind::Chime);
        std::cout << "songyun_toys::Model " << compact << " sample end " << samples.size() << '\n';
        toy<songyun_toys::Model>(root / "plugins/songyun-desk/assets/spring-clock-4x1", compact, true,
                                 toys::Kind::Clock);
        std::cout << "songyun_toys::Model " << compact << " sample end " << samples.size() << '\n';
        toy<mechanicaldesk_toys::Model>(root / "plugins/mechanical-desk/assets/chime-2x3", compact, true,
                                        toys::Kind::Chime);
        std::cout << "mechanicaldesk_toys::Model " << compact << " sample end " << samples.size() << '\n';
        toy<mechanicaldesk_toys::Model>(root / "plugins/mechanical-desk/assets/spring-clock-4x1", compact, false,
                                        toys::Kind::Clock);
        std::cout << "mechanicaldesk_toys::Model " << compact << " sample end " << samples.size() << '\n';
        toy<aeris_toys::Model>(root / "plugins/mecha-desk/assets/chime-2x3", compact, true, toys::Kind::Chime, true);
        std::cout << "aeris_toys::Model " << compact << " sample end " << samples.size() << '\n';
        toy<aeris_toys::Model>(root / "plugins/mecha-desk/assets/spring-clock-4x1", compact, true, toys::Kind::Clock,
                               true);
        std::cout << "aeris_toys::Model " << compact << " sample end " << samples.size() << '\n';
    }
    if (std::string(argv[2]) == "--write") {
        std::ofstream out(argv[3]);
        out << Json(samples);
    } else if (std::string(argv[2]) == "--compare") {
        std::ifstream in(argv[3]);
        const auto baseline = Json::parse(in).get<std::vector<double>>();
        if (samples.size() != baseline.size()) {
            throw std::runtime_error("trajectory sample count changed");
        }
        double maximum = 0;
        for (size_t i = 0; i < samples.size(); ++i) {
            maximum = std::max(maximum, std::abs(samples[i] - baseline[i]));
        }
        std::cout << "maximum trajectory difference: " << maximum << '\n';
        if (maximum > .000001) {
            throw std::runtime_error("trajectory changed");
        }
    } else {
        throw std::runtime_error("unknown comparison mode");
    }
    std::cout << "PASS: 40 classic/themed physics scenarios, " << samples.size() << " finite state samples\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
