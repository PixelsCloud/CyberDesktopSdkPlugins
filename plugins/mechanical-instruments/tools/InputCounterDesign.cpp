#include <Windows.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <limits>
#include <thread>

#include "plugins/widget-suite/InputCounter.h"
#include "plugins/widget-suite/Widget.h"

namespace {
constexpr const char* kId = "com.cyberdesktop.input.stats.mechanical.1x4";

void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

void modelChecks() {
    using namespace widgetplugin;
    require(counterDigits(0) == std::array<unsigned, 5>{0, 0, 0, 0, 0}, "Five-digit zero");
    require(counterDigits(14208) == std::array<unsigned, 5>{1, 4, 2, 0, 8}, "Exact count digits");
    for (const auto value : {uint64_t(99999), uint64_t(100000), std::numeric_limits<uint64_t>::max()}) {
        require(counterDigits(value) == std::array<unsigned, 5>{9, 9, 9, 9, 9}, "Saturate instead of wrapping");
    }
    CdInputStats stats{sizeof(stats), CD_INPUT_STATS_ACTIVE, 20261022, 0, 9999, 3156};
    CounterDrums drums;
    require(drums.retarget(stats, 1000, true) && !drums.active(1000), "Initial subscription snaps to current counts");
    stats.keyboard_today = 10000;
    require(drums.retarget(stats, 1100, true) && drums.active(1100), "Carry animates affected drums");
    const auto halfway = drums.sample(1180);
    for (size_t i = 5; i < 10; ++i) {
        require(halfway[i] == drums.sample(1100)[i], "Unchanged mouse drums remain still");
    }
    stats.keyboard_today = 10001;
    drums.retarget(stats, 1180, true);
    require(drums.sample(1180) == halfway, "Replacement continues from its current pose");
    require(!drums.active(1340), "Rolling animation has a finite endpoint");
    drums.finish();
    const auto complete = drums.sample(1340);
    for (size_t i = 0; i < 5; ++i) {
        require(unsigned(complete[i]) % 10 == counterDigits(10001)[i], "Carry finishes at exact count");
    }
    stats.keyboard_today = stats.mouse_today = 100000;
    drums.retarget(stats, 1400, false);
    ++stats.keyboard_today;
    require(!drums.retarget(stats, 1500, true) && !drums.active(1500), "Above-cap updates cause no visual work");
    ++stats.day;
    stats.keyboard_today = stats.mouse_today = 0;
    require(drums.retarget(stats, 1600, true) && !drums.active(1600), "Midnight resets immediately");
    stats.status = 0;
    require(drums.retarget(stats, 1700, true) && !drums.active(1700), "Unavailable statistics end visual work");
    drums.reset();
    require(!drums.initialized(), "Pause drops animation history");
    std::cout << "PASS cap, uint64 overflow boundary, carry, independent drums, replacement, midnight and pause\n";
}

struct Host {
    struct Timer {
        uint64_t token;
        CdTimerCallback callback;
        void* user;
    };

    std::vector<Timer> timers;
    CdInputStatsCallback stats = nullptr;
    void* statsUser = nullptr;
    uint64_t statsToken = 0, next = 1;
    unsigned redraws = 0;

    static void CD_CALL redraw(void* context) {
        ++static_cast<Host*>(context)->redraws;
    }

    static CdResult CD_CALL timer(void* context, uint32_t, CdTimerCallback callback, void* user, uint64_t* token) {
        auto& self = *static_cast<Host*>(context);
        *token = self.next++;
        self.timers.push_back({*token, callback, user});
        return CD_OK;
    }

    static CdResult CD_CALL subscribe(void* context, CdInputStatsCallback callback, void* user, uint64_t* token) {
        auto& self = *static_cast<Host*>(context);
        self.stats = callback;
        self.statsUser = user;
        *token = self.statsToken = self.next++;
        return CD_OK;
    }

    static void CD_CALL unsubscribe(void* context, uint64_t token) {
        auto& self = *static_cast<Host*>(context);
        std::erase_if(self.timers, [&](const auto& timer) {
            return timer.token == token;
        });
        if (token == self.statsToken) {
            self.stats = nullptr;
            self.statsToken = 0;
        }
    }

    void tick() {
        for (const auto timer : std::vector(timers)) {
            timer.callback(timer.user, GetTickCount64());
        }
    }
};

void instanceChecks() {
    Host host;
    CdInputStatsHostApi api{};
    api.media.base = {sizeof(api), CD_PLUGIN_ABI_VERSION, Host::redraw, nullptr, nullptr,
                      nullptr,     Host::unsubscribe,     Host::timer};
    api.subscribe_input_stats = Host::subscribe;
    const CdCreateContext context{sizeof(context), CD_PLUGIN_ABI_VERSION, kId, "lifecycle", "", &api.media.base, &host};
    CdInstanceApi instance{};
    require(widgetplugin::create({kId, widgetplugin::Kind::InputStats, 1, 4, true}, &context, &instance,
                                 sizeof(instance)) == CD_OK,
            "Create independent mechanical counter");

    struct Guard {
        CdInstanceApi& instance;

        ~Guard() {
            instance.destroy(instance.instance);
        }
    } guard{instance};

    require(host.stats && host.timers.empty(), "Shared count subscription, no permanent timer");
    CdInputStats stats{sizeof(stats), CD_INPUT_STATS_ACTIVE, 20261022, 0, 14208, 3156};
    host.stats(host.statsUser, &stats);
    require(host.timers.empty(), "Initial current value appears without animation");
    ++stats.keyboard_today;
    host.stats(host.statsUser, &stats);
    std::this_thread::sleep_for(std::chrono::milliseconds(180));
    host.tick();
    require(host.timers.empty(), "Completed drum animation releases its timer");
    const auto stable = host.redraws;
    host.stats(host.statsUser, &stats);
    require(host.redraws == stable, "Unchanged statistics do not redraw");
    const CdSettingValue reduced{sizeof(reduced), CD_SETTING_TOGGLE, "reduced_motion", 1};
    require(instance.apply_settings(instance.instance, &reduced, 1) == CD_OK, "Reduced motion setting");
    stats.keyboard_today = stats.mouse_today = 99999;
    host.stats(host.statsUser, &stats);
    require(host.timers.empty(), "Reduced motion snaps to accurate counts");
    const auto saturated = host.redraws;
    stats.keyboard_today = std::numeric_limits<uint64_t>::max();
    stats.mouse_today = 100000;
    host.stats(host.statsUser, &stats);
    require(host.redraws == saturated && host.timers.empty(), "Saturated counters stay visually idle");
    instance.set_paused(instance.instance, 1);
    require(!host.stats && host.timers.empty(), "Pause releases subscription and animation");
    instance.set_paused(instance.instance, 0);
    require(host.stats && host.timers.empty(), "Resume subscribes without replaying hidden input");
    std::cout << "PASS instance subscription, finite timer, stable/capped idle, reduced motion and pause/resume\n";
}
}  // namespace

int wmain(int argc, wchar_t** argv) try {
    require(argc == 3, "Expected repository and fixed output directory");
    modelChecks();
    instanceChecks();
    const std::filesystem::path root(argv[1]), output(argv[2]);
    const auto module = (std::filesystem::absolute(argv[0]).parent_path() / "plugins" / kId).u8string();
    std::filesystem::create_directories(output);
    CdHostApi host{};
    host.struct_size = sizeof(host);
    host.abi_version = CD_PLUGIN_ABI_VERSION;
    host.request_redraw = [](void*) {
    };
    const CdCreateContext context{sizeof(context),
                                  CD_PLUGIN_ABI_VERSION,
                                  kId,
                                  "native-design",
                                  reinterpret_cast<const char*>(module.c_str()),
                                  &host};
    widgetplugin::Canvas canvas;
    canvas.bind(context, [](uint32_t) {
    });
    const auto settings = widgetplugin::defaults(widgetplugin::Kind::InputStats, true);
    for (unsigned cell : {80u, 96u, 112u, 128u, 160u}) {
        for (double dpi : {1., 1.5, 2.}) {
            const double logicalWidth = 4 * cell - 6., logicalHeight = cell * 1.25 - 6.;
            const auto width = uint32_t(std::ceil(logicalWidth * dpi)),
                       height = uint32_t(std::ceil(logicalHeight * dpi));
            const uint32_t stride = width * 4 + 16;
            std::vector<uint8_t> pixels(size_t(stride) * height + 32, 0xa5);
            const CdFrame frame{sizeof(frame), width, height, stride, size_t(stride) * height, pixels.data(), dpi};
            CdInputStats stats{sizeof(stats), CD_INPUT_STATS_ACTIVE, 20261022, 0, 14208, 3156};
            const auto paint = [&] {
                require(canvas.render({kId, widgetplugin::Kind::InputStats, 1, 4, true}, settings, {}, {}, {}, 1, {}, 0,
                                      frame, {}, {}, 0, stats),
                        "Native input counter render");
            };
            paint();
            for (unsigned attempt = 0; canvas.pendingImages() && attempt < 1000; ++attempt) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                paint();
            }
            require(!canvas.pendingImages(), "Counter material finishes decoding");
            stats.keyboard_today = stats.mouse_today = 99999;
            paint();
            const auto capped = pixels;
            stats.keyboard_today = std::numeric_limits<uint64_t>::max();
            stats.mouse_today = 100000;
            paint();
            require(pixels == capped, "Rendered 99999 and overflow counts must look identical");
            stats.status = 0;
            paint();
            require(pixels != capped, "Unavailable data displays dashes rather than fabricated counts");
            stats = {sizeof(stats), CD_INPUT_STATS_ACTIVE, 20261022, 0, 14208, 3156};
            paint();
            for (uint32_t y = 0; y < height; ++y) {
                for (uint32_t x = 0; x < width; ++x) {
                    const auto* p = pixels.data() + size_t(y) * stride + x * 4;
                    require(p[0] <= p[3] && p[1] <= p[3] && p[2] <= p[3], "Premultiplied alpha");
                    if (x < 2 || y < 2 || x + 2 >= width || y + 2 >= height) {
                        require(!p[3], "Transparent outer spacing");
                    }
                }
                for (uint32_t x = width * 4; x < stride; ++x) {
                    require(pixels[size_t(y) * stride + x] == 0xa5, "Row guard");
                }
            }
            for (size_t i = size_t(stride) * height; i < pixels.size(); ++i) {
                require(pixels[i] == 0xa5, "Capacity guard");
            }
            const auto file =
                output / ("input-1x4-w" + std::to_string(cell) + "-dpi" + std::to_string(int(dpi * 100)) + ".png");
            require(canvas.savePng(file.wstring(), frame), "Save native counter preview");
            if (cell == 96 && dpi == 2) {
                std::filesystem::copy_file(file, root / "plugins/mechanical-instruments/packages/input-1x4/preview.png",
                                           std::filesystem::copy_options::overwrite_existing);
            }
        }
    }
    std::cout << "PASS 15 native grid/DPI captures, rendered saturation, unavailable data, alpha and guards\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
