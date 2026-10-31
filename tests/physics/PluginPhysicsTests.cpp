#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "sdk/support/Package.h"
#include "sdk/tools/Runtime.h"

namespace {
using Json = nlohmann::json;

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void requireSdk(bool condition, const cdsdk::Error& error, const char* stage) {
    require(condition, std::string(stage) + ": " + cdsdk::errorText(error));
}

void toggle(cdsdk::NativeInstance& instance, const char* id, int64_t enabled) {
    CdSettingValue value{};
    if (instance.api.get_setting(instance.api.instance, id, &value, sizeof(value)) == CD_OK &&
        value.type == CD_SETTING_TOGGLE) {
        value.integer = enabled;
        require(instance.api.apply_settings(instance.api.instance, &value, 1) == CD_OK,
                std::string("Apply setting: ") + id);
    }
}

std::array<uint64_t, 4> physics(cdsdk::NativeInstance& instance) {
    std::vector<cdsdk::DiagnosticSample> samples;
    cdsdk::DiagnosticError error;
    require(instance.host.diagnostics.read(&samples, &error), error.message);
    std::array<uint64_t, 4> values{};
    for (const auto& sample : samples) {
        require(sample.stats.values[CD_DIAG_IMAGE_FAILED] == 0, "An image layer failed");
        values[0] += sample.stats.values[CD_DIAG_PHYSICS_STEPS];
        values[1] += sample.stats.values[CD_DIAG_PHYSICS_NS];
        values[2] += sample.stats.values[CD_DIAG_PHYSICS_AWAKE];
        values[3] = std::max(values[3], sample.stats.values[CD_DIAG_PHYSICS_MAX_NS]);
    }
    return values;
}

Json run(const std::filesystem::path& root, const Json& spec, double seconds) {
    const auto started = std::chrono::steady_clock::now();
    const auto id = spec.at("id").get<std::string>();
    const auto package = root / id;
    cdsdk::Error error;
    cdsdk::pb::PluginDescriptor descriptor;
    requireSdk(cdsdk::loadDirectoryDescriptor(package, &descriptor, &error), error, "descriptor");
    cdsdk::NativeModule module;
    requireSdk(module.open(package, descriptor, &error), error, "module");
    std::vector<std::string> errors;
    cdsdk::NativeInstance instance;
    instance.host.onLog = [&](uint32_t level, std::string_view message) {
        if (level >= 3 && errors.size() < 16) {
            errors.emplace_back(message);
        }
    };
    instance.host.baseUnixMs = 1793404800000ull;
    instance.host.utcOffsetMinutes = 0;
    instance.host.onAudio = [](double seconds, std::span<float> samples) {
        std::fill(samples.begin(), samples.end(), std::fmod(seconds, .5) < .1 ? .7F : .01F);
    };
    requireSdk(instance.create(module, package, "physics-contract", &error), error, "create");
    toggle(instance, "running", 1);
    toggle(instance, "reduced_motion", 0);
    toggle(instance, "physics", 1);

    require(descriptor.layouts_size() > 0, "Missing declared footprint");
    const auto* footprint = &descriptor.layouts(0);
    for (const auto& candidate : descriptor.layouts()) {
        if (candidate.is_default()) {
            footprint = &candidate;
        }
    }
    int minX = 0, minY = 0, maxX = 0, maxY = 0;
    std::vector<CdCell> cells;
    for (const auto& cell : footprint->cells()) {
        minX = std::min(minX, cell.a());
        minY = std::min(minY, cell.b());
        maxX = std::max(maxX, cell.a());
        maxY = std::max(maxY, cell.b());
        cells.push_back({cell.a(), cell.b()});
    }
    const uint32_t width = uint32_t(maxX - minX + 1) * 80;
    const uint32_t height = uint32_t(maxY - minY + 1) * 100;
    CdLayout layout{sizeof(CdLayout),
                    0,
                    0,
                    uint32_t(cells.size()),
                    cells.data(),
                    double(width),
                    double(height),
                    1,
                    {0, 0, double(width), double(height)},
                    {235, 240, 245, 255},
                    {70, 200, 170, 255}};
    instance.api.layout_changed(instance.api.instance, &layout);
    std::vector<uint8_t> pixels;
    // Let asynchronous image layers finish without repeatedly changing layout.
    for (unsigned warm = 0; warm < 30; ++warm) {
        instance.host.tick(double(warm) / 60);
        requireSdk(instance.render(width, height, 1, &pixels, &error), error, "warm render");
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    unsigned missedTimers = 0;
    auto previousPhysics = physics(instance);
    const auto frames = uint64_t(seconds * 60);
    for (uint64_t frame = 30; frame < frames; ++frame) {
        const double now = double(frame) / 60;
        instance.host.tick(now);
        require(errors.empty(), errors.empty() ? "" : errors.front());
        if (spec.value("continuousMusic", false) && now > 2) {
            const bool timer = std::any_of(instance.host.subscriptions.begin(), instance.host.subscriptions.end(),
                                           [](const auto& entry) {
                                               return entry.second.timer != nullptr;
                                           });
            if (!timer) {
                require(++missedTimers <= 4, "Music-driven physics lost its update subscription at " +
                                                 std::to_string(now) + " simulated seconds");
            }
        }
        // Release redraw coalescing and exercise the actual render adapter.
        // Dense frame-rate/Present tests run separately through the native host.
        if (frame % 3600 == 0) {
            requireSdk(instance.render(width, height, 1, &pixels, &error), error, "running render");
            const auto current = physics(instance);
            if (spec.value("continuousMusic", false) && spec.value("box2d", true)) {
                require(current[0] > previousPhysics[0], "Music-driven world stopped advancing");
            }
            previousPhysics = current;
        }
    }
    const auto beforePause = physics(instance);
    if (spec.value("box2d", true)) {
        require(beforePause[0] > 0, "No actual Box2D steps were observed");
    }
    instance.pause(true);
    const auto paused = physics(instance);
    instance.host.tick(seconds + 60);
    require(physics(instance)[0] == paused[0], "Hidden physics continued stepping");
    instance.pause(false);
    for (unsigned frame = 0; frame < 60; ++frame) {
        instance.host.tick(seconds + 60 + double(frame) / 60);
    }
    requireSdk(instance.render(width, height, 1, &pixels, &error), error, "resumed render");
    require(errors.empty(), errors.empty() ? "" : errors.front());
    size_t visible = 0;
    for (size_t offset = 3; offset < pixels.size(); offset += 4) {
        visible += pixels[offset] != 0;
    }
    require(visible > 0, "Plugin produced an empty frame");
    const auto completed = physics(instance);
    if (spec.value("continuousMusic", false) && spec.value("box2d", true)) {
        require(completed[0] > paused[0], "Music-driven world failed to resume");
    }
    layout.device_pixel_ratio = 1.5;
    instance.api.layout_changed(instance.api.instance, &layout);
    for (unsigned frame = 0; frame < 30; ++frame) {
        instance.host.tick(seconds + 61 + double(frame) / 60);
        requireSdk(instance.render(width * 3 / 2, height * 3 / 2, 1.5, &pixels, &error), error, "DPI render");
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    physics(instance);
    instance.api.destroy(instance.api.instance);
    instance.api.instance = nullptr;
    require(instance.host.subscriptions.empty(), "Destroy left a data/timer subscription alive");
    require(instance.host.diagnostics.empty(), "Destroy left a diagnostic callback alive");
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return {{"id", id},
            {"model", spec.at("model")},
            {"status", "passed"},
            {"simulatedSeconds", seconds},
            {"wallSeconds", elapsed},
            {"missedTimers", missedTimers},
            {"physicsSteps", beforePause[0]},
            {"physicsAdvanceNs", beforePause[1]},
            {"physicsMaximumAdvanceNs", beforePause[3]},
            {"physicsProfiled", beforePause[3] != 0},
            {"nontransparentPixels", visible}};
}
}  // namespace

int main(int argc, char** argv) try {
    require(argc == 5 || argc == 6, "Usage: plugin-root inventory.json report.json simulated-seconds [workers]");
    const auto root = cdsdk::utf8Path(argv[1]);
    std::ifstream input(cdsdk::utf8Path(argv[2]));
    const auto inventory = Json::parse(input);
    const double seconds = std::stod(argv[4]);
    require(seconds >= 10 && seconds <= 86400, "Invalid simulation duration");
    const unsigned workers = argc == 6 ? unsigned(std::stoul(argv[5])) : 1;
    require(workers == 1 || workers == 2 || workers == 4, "Workers must be 1, 2 or 4");
    if (argc == 6) {
        // Experimental headless execution lanes. Each DLL instance and World is
        // created, advanced and destroyed on one fixed thread. This measures the
        // cost of independent plugin/model work, not production UI isolation.
        const auto& specs = inventory.at("packages");
        std::vector<Json> results(specs.size());
        std::atomic<size_t> next{0};
        const auto started = std::chrono::steady_clock::now();
        {
            std::vector<std::jthread> lanes;
            for (unsigned lane = 0; lane < workers; ++lane) {
                lanes.emplace_back([&] {
                    for (auto index = next.fetch_add(1); index < specs.size(); index = next.fetch_add(1)) {
                        try {
                            results[index] = run(root, specs[index], seconds);
                        } catch (const std::exception& error) {
                            results[index] = {
                                {"id", specs[index].at("id")}, {"status", "failed"}, {"error", error.what()}};
                        }
                    }
                });
            }
        }
        const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        const auto failures = std::count_if(results.begin(), results.end(), [](const Json& value) {
            return value.at("status") != "passed";
        });
        std::ofstream report(cdsdk::utf8Path(argv[3]));
        report << Json{{"status", failures ? "failed" : "passed"},
                       {"workers", workers},
                       {"wallSeconds", elapsed},
                       {"failures", failures},
                       {"cases", results}}
                      .dump(2)
               << '\n';
        return failures ? 1 : 0;
    }
    Json cases = Json::array();
    unsigned failed = 0;
    for (const auto& spec : inventory.at("packages")) {
        try {
            cases.push_back(run(root, spec, seconds));
            std::cout << "PASS " << spec.at("id").get<std::string>() << std::endl;
        } catch (const std::exception& error) {
            ++failed;
            cases.push_back({{"id", spec.at("id")}, {"status", "failed"}, {"error", error.what()}});
            std::cout << "FAIL " << spec.at("id").get<std::string>() << ": " << error.what() << std::endl;
        }
        std::ofstream report(cdsdk::utf8Path(argv[3]));
        report << Json{{"status", "running"}, {"failures", failed}, {"cases", cases}}.dump(2) << '\n';
    }
    std::ofstream report(cdsdk::utf8Path(argv[3]));
    report << Json{{"status", failed ? "failed" : "passed"}, {"failures", failed}, {"cases", cases}}.dump(2) << '\n';
    require(!cases.empty(), "No physics cases selected");
    return failed ? 1 : 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
