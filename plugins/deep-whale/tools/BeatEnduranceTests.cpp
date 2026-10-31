#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

#include "plugins/beat-machine/Model.h"
#include "plugins/deep-whale/BeatScene.h"

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
}  // namespace

int main(int argc, char** argv) {
    const auto started = std::chrono::steady_clock::now();
    std::string scenario;
    int frame = 0;
    try {
        require(argc == 2, "Pass the Deep Whale assets directory");
        for (const bool compact : {true, false}) {
            const std::string folder = compact ? "beat-4x2" : "beat-6x2";
            std::ifstream input(std::filesystem::path(argv[1]) / folder / "skin.json");
            const auto data = nlohmann::json::parse(input);
            const auto& region = data.at("regions").at("world");
            const cybercanvas::Rect field{region[0], region[1], region[2], region[3]};
            for (const unsigned count : {compact ? 18U : 28U, 48U}) {
                const auto caseStarted = std::chrono::steady_clock::now();
                scenario = folder + "/" + std::to_string(count);
                deep_motion::BeatScene scene;
                scene.resetBeat(field, count, compact);
                beatmachine::AudioMotion audio;
                for (frame = 0; frame < 30 * 60 * 60; ++frame) {
                    // Alternate actual FFT attacks and quiet sections, including
                    // a full minute of silence before the final music restart.
                    std::array<float, 63> bands{};
                    const bool silent = frame % (5 * 60 * 60) >= 4 * 60 * 60;
                    if (!silent && frame % 30 < 3) {
                        bands.fill(.4F);
                    }
                    audio.feed(bands);
                    audio.advance(1. / 60, silent, 3.5);
                    scene.stepBeat(1. / 60, audio.signal());
                    if (frame % 600 == 0) {
                        for (const auto& ball : scene.balls) {
                            const auto state = scene.world->state(ball.body);
                            require(std::isfinite(state.position.x) && std::isfinite(state.position.y),
                                    "A pearl left the finite simulation");
                        }
                    }
                }
                require(scene.world->takeEvents().empty(), "Visual-only collision events remained queued");
                const auto before = scene.world->stats().steps;
                scene.world->paused(true);
                scene.stepBeat(100, audio.signal());
                require(scene.world->stats().steps == before, "Paused scene continued simulation");
                scene.world->paused(false);
                auto signal = audio.signal();
                ++signal.beat;
                signal.strength = 1;
                for (int tick = 0; tick < 60; ++tick) {
                    scene.stepBeat(1. / 60, signal);
                }
                const auto after = scene.world->stats().steps;
                require(after > before, "The scene stopped responding after sustained operation");
                const auto seconds =
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - caseStarted).count();
                std::cout << "PASS " << scenario << " simulatedSeconds=1801 steps=" << scene.world->stats().steps
                          << " elapsedSeconds=" << seconds << " averageUpdateUs=" << seconds * 1e6 / (frame + 61)
                          << std::endl;
            }
        }
        const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        std::cout << "PASS four sustained scenes, elapsedSeconds=" << elapsed << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << scenario << " simulatedSeconds=" << frame / 60. << ": " << error.what() << '\n';
        return 1;
    }
}
