#include <Windows.h>
#include <psapi.h>

#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

#include "tools/Runtime.h"

namespace {
struct Memory {
    uint64_t bytes;
    DWORD handles;
};

Memory memory() {
    PROCESS_MEMORY_COUNTERS_EX counters{};
    DWORD handles = 0;
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                              sizeof(counters)) ||
        !GetProcessHandleCount(GetCurrentProcess(), &handles)) {
        throw std::runtime_error("Process accounting failed");
    }
    return {counters.PrivateUsage, handles};
}

void destroy(cdsdk::NativeInstance& instance) {
    instance.pause(true);
    instance.api.destroy(instance.api.instance);
    instance.api.instance = nullptr;
    if (!instance.host.subscriptions.empty() || !instance.host.diagnostics.empty() || instance.host.uiCallback ||
        instance.host.captured) {
        throw std::runtime_error("Plugin teardown left a host registration");
    }
}

int idle(const std::array<std::filesystem::path, 2>& roots, const std::filesystem::path& output, double duration) {
    std::filesystem::create_directories(output);
    std::array<cdsdk::NativeModule, 2> modules;
    cdsdk::Error error;
    for (size_t i = 0; i < modules.size(); ++i) {
        cdsdk::pb::PluginDescriptor descriptor;
        if (!cdsdk::loadDirectoryDescriptor(roots[i], &descriptor, &error) ||
            !modules[i].open(roots[i], descriptor, &error)) {
            throw std::runtime_error(error.message);
        }
    }
    std::array<std::unique_ptr<cdsdk::NativeInstance>, 4> instances;
    std::vector<uint8_t> pixels;
    const auto render = [&](cdsdk::NativeInstance& instance) {
        if (!instance.render(384, 384, 1, &pixels, &error)) {
            throw std::runtime_error(error.message);
        }
        instance.host.dirty = false;
    };
    for (size_t i = 0; i < instances.size(); ++i) {
        instances[i] = std::make_unique<cdsdk::NativeInstance>();
        auto& instance = *instances[i];
        if (!instance.create(modules[i % 2], roots[i % 2], "idle-" + std::to_string(i), &error)) {
            throw std::runtime_error(error.message);
        }
        if (i % 2) {
            const CdSettingValue value{sizeof(value), CD_SETTING_TOGGLE, "animate", 0};
            if (instance.api.apply_settings(instance.api.instance, &value, 1) != CD_OK) {
                throw std::runtime_error("Cannot freeze gallery");
            }
        } else if (i == 2) {
            const CdSettingValue value{sizeof(value), CD_SETTING_TOGGLE, "spring", 1};
            if (instance.api.apply_settings(instance.api.instance, &value, 1) != CD_OK) {
                throw std::runtime_error("Cannot enable spring idle fixture");
            }
        }
        render(instance);
    }
    // Settle the world on a deterministic host clock, then allow real decoder
    // work to finish before observing an uninterrupted wall-clock idle window.
    for (int frame = 0; frame < 3720; ++frame) {
        for (auto& instance : instances) {
            instance->host.tick(frame / 60.);
            if (instance->host.dirty.exchange(false)) {
                render(*instance);
            }
        }
        if (frame > 3600) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    instances[0]->pause(true);
    instances[1]->pause(true);
    for (auto& instance : instances) {
        instance->host.dirty = false;
    }
    std::array<uint64_t, 4> frames{}, timers{}, business{};
    const auto start = std::chrono::steady_clock::now();
    auto next = start;
    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() < duration) {
        const double seconds = 62 + std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        for (size_t i = 0; i < instances.size(); ++i) {
            auto& instance = *instances[i];
            for (const auto& [token, subscription] : instance.host.subscriptions) {
                if (seconds >= subscription.nextTime) {
                    (subscription.timer ? timers[i] : business[i])++;
                }
            }
            instance.host.tick(seconds);
            if (instance.host.dirty.exchange(false)) {
                ++frames[i];
                render(instance);
            }
        }
        next += std::chrono::milliseconds(16);
        std::this_thread::sleep_until(next);
    }
    bool passed = true;
    std::ofstream report(output / "idle.json");
    report << "{\"duration_seconds\":" << duration << ",\"instances\":[";
    for (size_t i = 0; i < instances.size(); ++i) {
        passed &= timers[i] == 0 && (i == 3 || (frames[i] == 0 && business[i] == 0));
        if (i) {
            report << ',';
        }
        report << "{\"index\":" << i << ",\"hidden\":" << (i < 2 ? "true" : "false")
               << ",\"decorative_ticks\":" << timers[i] << ",\"business_ticks\":" << business[i]
               << ",\"redraws\":" << frames[i] << '}';
        destroy(*instances[i]);
    }
    passed &=
        business[3] >= uint64_t(duration * .8) && business[3] <= uint64_t(duration + 2) && frames[3] <= business[3] + 1;
    report << "],\"passed\":" << (passed ? "true" : "false") << '}';
    return passed ? 0 : 1;
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 5 && argc != 6) {
        std::cerr << "Usage: rendering-endurance PHYSICS GALLERY OUTPUT SECONDS [--idle]\n";
        return 2;
    }
    try {
        const double duration = std::stod(argv[4]);
        if (!std::isfinite(duration) || duration < 1 || duration > 86400) {
            throw std::invalid_argument("Invalid duration");
        }
        if (argc == 6) {
            if (std::wstring_view(argv[5]) != L"--idle") {
                throw std::invalid_argument("Unknown endurance mode");
            }
            return idle({argv[1], argv[2]}, argv[3], duration);
        }
        const std::filesystem::path output = argv[3];
        std::filesystem::create_directories(output);
        std::ofstream log(output / "samples.csv");
        log << "seconds,private_bytes,handles,frames,cycles\n";
        std::array<std::filesystem::path, 2> roots{argv[1], argv[2]};
        std::array<cdsdk::NativeModule, 2> modules;
        cdsdk::Error error;
        for (size_t i = 0; i < roots.size(); ++i) {
            cdsdk::pb::PluginDescriptor descriptor;
            if (!cdsdk::loadDirectoryDescriptor(roots[i], &descriptor, &error) ||
                !modules[i].open(roots[i], descriptor, &error)) {
                throw std::runtime_error(error.message);
            }
        }
        CdLayout layout{sizeof(layout), 0, 0, 0, nullptr, 384, 384, 1, {0, 0, 384, 384}};
        std::array<std::unique_ptr<cdsdk::NativeInstance>, 5> instances;
        std::vector<uint8_t> pixels;
        uint64_t cycles = 0, frames = 0;
        const auto create = [&](size_t index) {
            if (instances[index]) {
                destroy(*instances[index]);
            }
            auto instance = std::make_unique<cdsdk::NativeInstance>();
            const size_t kind = index % 2;
            if (!instance->create(modules[kind], roots[kind], "endurance-" + std::to_string(index), &error)) {
                throw std::runtime_error(error.message);
            }
            if (kind == 0 && index == 2) {
                const CdSettingValue value{sizeof(value), CD_SETTING_TOGGLE, "spring", 1};
                if (instance->api.apply_settings(instance->api.instance, &value, 1) != CD_OK) {
                    throw std::runtime_error("Cannot enable spring endurance fixture");
                }
            }
            instance->api.layout_changed(instance->api.instance, &layout);
            if (!instance->render(384, 384, 1, &pixels, &error)) {
                throw std::runtime_error(error.message);
            }
            instances[index] = std::move(instance);
            ++cycles;
        };
        // Bounded lifecycle stress precedes the real wall-clock observation.
        for (int i = 0; i < 500; ++i) {
            create(size_t(i) % instances.size());
        }
        using Clock = std::chrono::steady_clock;
        const auto start = Clock::now();
        auto next = start;
        double sampledAt = -1, switchedAt = 0;
        Memory baseline{}, last{};
        uint64_t peak = 0;
        DWORD peakHandles = 0;
        while (true) {
            const auto now = Clock::now();
            const double elapsed = std::chrono::duration<double>(now - start).count();
            if (elapsed >= duration) {
                break;
            }
            const bool hidden = int(elapsed) % 120 >= 100;
            for (size_t i = 0; i < instances.size(); ++i) {
                auto& instance = *instances[i];
                if (instance.host.paused != hidden) {
                    instance.pause(hidden);
                }
                instance.host.tick(elapsed);
                if (!hidden && instance.host.dirty.exchange(false)) {
                    if (!instance.render(384, 384, 1, &pixels, &error)) {
                        throw std::runtime_error(error.message);
                    }
                    ++frames;
                }
            }
            if (elapsed - switchedAt >= 30 && !hidden) {
                create(size_t(cycles) % instances.size());
                switchedAt = elapsed;
            }
            if (sampledAt < 0 || elapsed - sampledAt >= 10) {
                last = memory();
                peak = std::max(peak, last.bytes);
                peakHandles = std::max(peakHandles, last.handles);
                if (!baseline.bytes && elapsed >= 10) {
                    baseline = last;
                }
                log << elapsed << ',' << last.bytes << ',' << last.handles << ',' << frames << ',' << cycles << '\n';
                log.flush();
                sampledAt = elapsed;
            }
            next += std::chrono::milliseconds(16);
            if (next < Clock::now()) {
                next = Clock::now();
            }
            std::this_thread::sleep_until(next);
        }
        for (auto& instance : instances) {
            destroy(*instance);
            instance.reset();
        }
        const auto released = memory();
        // Report measured growth; the M0 acceptance threshold is evaluated outside
        // this tool, so a short smoke run cannot masquerade as endurance acceptance.
        std::ofstream report(output / "report.json");
        report << "{\"seconds\":" << duration << ",\"instances\":5,\"width\":384,\"height\":384,\"frames\":" << frames
               << ",\"createDestroyCycles\":" << cycles << ",\"baselinePrivateBytes\":" << baseline.bytes
               << ",\"lastPrivateBytes\":" << last.bytes << ",\"peakPrivateBytes\":" << peak
               << ",\"releasedPrivateBytes\":" << released.bytes << ",\"baselineHandles\":" << baseline.handles
               << ",\"lastHandles\":" << last.handles << ",\"peakHandles\":" << peakHandles
               << ",\"releasedHandles\":" << released.handles << ",\"registrationsReleased\":true}\n";
        std::cout << "PASS lifecycle and " << duration << " seconds mixed observation\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
