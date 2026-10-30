#include <Windows.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <set>

#include "plugins/hourglass-suite/Hourglass.h"
#include "plugins/hourglass-suite/PhysicalSand.h"

namespace {
void check(bool passed, const char* message) {
    if (!passed) {
        throw std::runtime_error(message);
    }
}

void simulation() {
    using namespace hourglassplugin;
    for (double duration : {5., 30., 3600.}) {
        Model model;
        model.reset(duration);
        check(model.capacity() == 105 && model.sample().upper.size() == model.capacity(), "Initial sand missing");
        size_t previousUpper = model.capacity();
        bool flight = false;
        for (int i = 0; i < 1200; ++i) {
            const auto sand = model.sample();
            check(sand.upper.size() + sand.lower.size() + sand.falling.size() == model.capacity(),
                  "Sand was lost or duplicated");
            check(sand.upper.size() <= previousUpper, "Upper sand grew while draining");
            previousUpper = sand.upper.size();
            std::set<std::pair<int, int>> settled;
            for (const auto cell : sand.lower) {
                check(settled.emplace(cell.x, cell.y).second && cell.y >= 16, "Sand pile overlapped itself");
            }
            for (const auto* cells : {&sand.upper, &sand.lower, &sand.falling}) {
                for (const auto cell : *cells) {
                    check(cell.y >= 2 && cell.y <= 26 && std::abs(cell.x - 9) <= chamberHalfWidth(cell.y),
                          "Sand escaped the glass");
                }
            }
            flight |= !sand.falling.empty();
            model.advance(duration / 1200, false);
        }
        check(flight && model.remaining() == 0 && model.phase() == Phase::Finished &&
                  model.sample().lower.size() == model.capacity(),
              "Sand failed to settle at countdown completion");
        model.advance(100, false);
        check(model.phase() == Phase::Finished && model.remaining() == 0, "One-shot countdown repeated");
    }

    Model flipping;
    flipping.reset(5);
    flipping.advance(5.5, true);
    check(flipping.phase() == Phase::Turning && std::abs(flipping.angle() - 90) < .000001 &&
              flipping.sample().lower.size() == flipping.capacity(),
          "Flip did not carry the full lower chamber");
    flipping.advance(.5, true);
    check(flipping.phase() == Phase::Flowing && flipping.cycles() == 1 && flipping.remaining() == 5 &&
              flipping.sample().upper.size() == flipping.capacity(),
          "Automatic flip did not refill the upper chamber");
    flipping.advance(5.5, true);
    flipping.advance(0, false);
    check(flipping.phase() == Phase::Finished && flipping.remaining() == 0 &&
              flipping.sample().lower.size() == flipping.capacity(),
          "Disabling repeat during a flip started an extra countdown");
    Model a, b;
    a.reset(8);
    b.reset(8);
    for (int i = 0; i < 847; ++i) {
        a.advance(.05, true);
        b.advance(.025, true);
        b.advance(.025, true);
    }
    check(a.cycles() == b.cycles() && a.remaining() == b.remaining() &&
              std::abs(a.progress() - b.progress()) < .000001 && a.sample().upper == b.sample().upper &&
              a.sample().falling == b.sample().falling,
          "Sand flow depended on frame rate");
}

void physicalSimulation() {
    using namespace hourglassplugin;
    for (double fps : {15., 20., 30.}) {
        Model timer;
        timer.reset(5);
        PhysicalSand sand;
        check(sand.count() == 105, "Physical grain count changed");
        for (int i = 0; i < int(fps * 24); ++i) {
            const auto previousCycle = timer.cycles();
            timer.advance(1 / fps, true);
            sand.advance(1 / fps, timer);
            const auto points = sand.sample();
            check(points.size() == 105, "Physical grains lost");
            if (timer.cycles() != previousCycle) {
                check(std::count_if(points.begin(), points.end(),
                                    [](auto p) {
                                        return p.y < 14;
                                    }) >= 100,
                      "Flip must refill upper chamber for the next countdown");
            }
            for (auto p : points) {
                if (!std::isfinite(p.x) || !std::isfinite(p.y) || p.x < .8 || p.x > 18.1 || p.y < 1.5 || p.y > 27.1) {
                    std::cerr << "escaped grain " << p.x << ',' << p.y << " frame=" << i << " fps=" << fps << '\n';
                    check(false, "Physical sand escaped during flow/flip");
                }
            }
            check(sand.stats().bodies == 106 && sand.stats().shapes < 200, "Physical resource count grew");
        }
        sand.reset();
        timer.reset(5);
        for (int i = 0; i < int(fps * 5); ++i) {
            timer.advance(1 / fps, false);
            sand.advance(1 / fps, timer);
        }
        check(timer.phase() == Phase::Finished && timer.remaining() == 0 && sand.released() == 105,
              "Metered sand failed to complete on time");
        const auto points = sand.sample();
        check(std::count_if(points.begin(), points.end(),
                            [](auto p) {
                                return p.y > 14;
                            }) >= 100,
              "Physical sand failed to reach lower chamber");
    }
}

struct Host {
    struct Timer {
        CdTimerCallback callback;
        void* user;
        uint32_t interval;
    };

    std::map<uint64_t, Timer> timers;
    uint64_t next = 1, now = GetTickCount64();
    bool fail = false;
    int redraws = 0;

    static void CD_CALL redraw(void* context) {
        ++static_cast<Host*>(context)->redraws;
    }

    static CdResult CD_CALL subscribe(void* context, uint32_t interval, CdTimerCallback callback, void* user,
                                      uint64_t* token) {
        auto& self = *static_cast<Host*>(context);
        if (self.fail) {
            return CD_ERROR_INTERNAL;
        }
        *token = self.next++;
        self.timers.emplace(*token, Timer{callback, user, interval});
        return CD_OK;
    }

    static void CD_CALL stop(void* context, uint64_t token) {
        check(static_cast<Host*>(context)->timers.erase(token) == 1, "Duplicate unsubscribe");
    }

    CdHostApi api{sizeof(api), CD_PLUGIN_ABI_VERSION, redraw, nullptr, nullptr, nullptr, stop, subscribe};

    void advance(uint64_t milliseconds) {
        now += milliseconds;
        const auto copy = timers;
        for (const auto& [token, timer] : copy) {
            if (timers.contains(token)) {
                timer.callback(timer.user, now);
            }
        }
    }
};

struct Plugin {
    Host host;
    CdInstanceApi api{};
    std::vector<uint8_t> pixels;
    CdFrame frame{};

    explicit Plugin(hourglassplugin::Spec spec = {}, uint32_t width = 184, uint32_t height = 88)
        : pixels(size_t(width) * height * 4),
          frame{sizeof(frame), width, height, width * 4, pixels.size(), pixels.data(), 1} {
        using namespace hourglassplugin;
        const CdCreateContext context{sizeof(context), CD_PLUGIN_ABI_VERSION, spec.id, "test", ".", &host.api, &host};
        check(create(spec, &context, &api, sizeof(api)) == CD_OK, "Creation failed");
        const CdLayout layout{sizeof(layout),
                              0,
                              0,
                              0,
                              nullptr,
                              double(width),
                              double(height),
                              1,
                              {0, 0, double(width), double(height)},
                              {235, 235, 235, 255},
                              {220, 170, 80, 255}};
        api.layout_changed(api.instance, &layout);
    }

    ~Plugin() {
        api.destroy(api.instance);
        if (!host.timers.empty()) {
            std::terminate();
        }
    }

    std::vector<uint8_t> image() {
        check(api.render(api.instance, &frame) == CD_OK, "Instance rendering failed");
        return pixels;
    }

    CdResult setting(const char* id, uint32_t type, int64_t integer, const char* text = nullptr) {
        const CdSettingValue value{sizeof(value), type, id, integer, 0, text};
        return api.apply_settings(api.instance, &value, 1);
    }

    int64_t integer(const char* id) {
        CdSettingValue value{};
        check(api.get_setting(api.instance, id, &value, sizeof(value)) == CD_OK, "Setting read failed");
        return value.integer;
    }

    void click(double x, double y = 44) {
        CdInput value{sizeof(value), CD_INPUT_PRESS, CD_BUTTON_LEFT, 0, x, y, 0};
        api.input(api.instance, &value);
        value.kind = CD_INPUT_RELEASE;
        value.buttons = 0;
        api.input(api.instance, &value);
    }
};

void lifecycle() {
    Plugin first, second;
    check(first.host.timers.size() == 1 && first.host.timers.begin()->second.interval == 50, "Default timer wrong");
    const auto initial = first.image();
    for (int i = 0; i < 50; ++i) {
        first.host.advance(50);
    }
    check(first.image() != initial && second.image() == initial, "Animation failed or leaked between instances");
    first.api.set_paused(first.api.instance, 1);
    check(first.host.timers.empty(), "Hidden plugin retained its timer");
    const auto hidden = first.image();
    first.host.advance(3600000);
    check(first.image() == hidden, "Hidden plugin advanced during sleep");
    first.api.set_paused(first.api.instance, 0);
    check(first.host.timers.size() == 1 && first.image() == hidden, "Resume caught up or leaked a timer");

    first.click(135);
    check(first.integer("running") == 0 && first.host.timers.empty(), "Right-side click did not pause");
    first.host.fail = true;
    check(first.setting("running", CD_SETTING_TOGGLE, 1) == CD_ERROR_INTERNAL && first.integer("running") == 0 &&
              first.host.timers.empty(),
          "Failed subscription changed the paused setting");
    first.host.fail = false;
    first.click(135);
    check(first.integer("running") == 1 && first.host.timers.size() == 1, "Click did not resume");
    first.click(30);
    check(first.image() == initial && first.host.timers.size() == 1, "Glass click did not reset the countdown");

    const auto beforeDrag = first.image();
    CdInput drag{sizeof(drag), CD_INPUT_PRESS, CD_BUTTON_LEFT, 0, 30, 44, 0};
    first.api.input(first.api.instance, &drag);
    drag.kind = CD_INPUT_MOVE;
    drag.x += 25;
    first.api.input(first.api.instance, &drag);
    drag.kind = CD_INPUT_RELEASE;
    drag.buttons = 0;
    drag.x -= 25;
    first.api.input(first.api.instance, &drag);
    check(first.image() == beforeDrag, "Dragging triggered reset");
    check(first.setting("duration", CD_SETTING_INTEGER, 4) == CD_ERROR_ARGUMENT && first.integer("duration") == 30,
          "Invalid duration changed the model");
    check(first.setting("frame_rate", CD_SETTING_CHOICE, 0, "30") == CD_OK && first.host.timers.size() == 1 &&
              first.host.timers.begin()->second.interval == 34,
          "Frame-rate change leaked subscriptions");

    check(first.setting("repeat", CD_SETTING_TOGGLE, 0) == CD_OK &&
              first.setting("duration", CD_SETTING_INTEGER, 5) == CD_OK,
          "One-shot countdown settings failed");
    for (int i = 0; i < 110; ++i) {
        first.host.advance(50);
    }
    check(first.host.timers.empty(), "Completed countdown retained its timer");
    first.click(135);
    check(first.host.timers.size() == 1, "Completed countdown did not restart on click");
    first.frame.capacity = 1;
    check(first.api.render(first.api.instance, &first.frame) == CD_ERROR_ARGUMENT, "Short output buffer accepted");
    first.frame.capacity = first.pixels.size();
    first.frame.device_pixel_ratio = std::numeric_limits<double>::quiet_NaN();
    check(first.api.render(first.api.instance, &first.frame) == CD_ERROR_ARGUMENT, "Invalid DPI accepted");
}

void square() {
    using namespace hourglassplugin;
    Plugin plugin({kSquarePluginId, false}, 88, 88);
    const auto initial = plugin.image();
    Renderer renderer;
    Model model;
    Options options;
    options.showTimer = false;
    PhysicalSand physical;
    options.physicalSand = &physical;
    std::vector<uint8_t> pixels(initial.size());
    CdFrame frame{sizeof(frame), 88, 88, 88 * 4, pixels.size(), pixels.data(), 1};
    check(renderer.render(model, options, frame, {}) && pixels == initial,
          "Square plugin added text, digits, progress or controls to the pure hourglass");
    check(plugin.setting("duration", CD_SETTING_INTEGER, 60) == CD_OK && plugin.image() == initial,
          "Square duration appeared as visible text");
    CdSettingValue output{};
    check(plugin.api.get_setting(plugin.api.instance, "show_timer", &output, sizeof(output)) == CD_ERROR_ARGUMENT &&
              plugin.setting("show_timer", CD_SETTING_TOGGLE, 1) == CD_ERROR_ARGUMENT,
          "Square settings exposed a way to enable text");
    for (int i = 0; i < 50; ++i) {
        plugin.host.advance(50);
    }
    check(plugin.image() != initial, "Square sand failed to flow");
    plugin.click(44, 65);
    check(plugin.integer("running") == 0 && plugin.host.timers.empty(), "Lower-half click did not pause");
    const auto paused = plugin.image();
    plugin.host.advance(100000);
    check(plugin.image() == paused, "Paused square hourglass changed");
    plugin.click(44, 25);
    check(plugin.integer("running") == 1 && plugin.host.timers.size() == 1 && plugin.image() == initial,
          "Upper-half click did not reset the square hourglass");
    check(plugin.setting("physics", CD_SETTING_TOGGLE, 0) == CD_OK && plugin.image() != initial,
          "Classic sand mode cannot be selected");
    check(plugin.setting("physics", CD_SETTING_TOGGLE, 1) == CD_OK && plugin.image() == initial,
          "Physical mode does not restart cleanly");
}

void rendering() {
    using namespace hourglassplugin;
    Renderer renderer;
    Model model;
    model.advance(13.5, true);
    Model physicalTimer;
    PhysicalSand physical;
    for (int i = 0; i < 270; ++i) {
        physicalTimer.advance(.05, true);
        physical.advance(.05, physicalTimer);
    }
    int renders = 0;
    for (const auto dimensions : {std::pair{136., 64.}, std::pair{184., 88.}, std::pair{136., 142.},
                                  std::pair{64., 64.}, std::pair{88., 88.}, std::pair{64., 142.}}) {
        for (double dpi : {.5, 1., 2., 3.}) {
            const uint32_t width = uint32_t(dimensions.first * dpi), height = uint32_t(dimensions.second * dpi);
            std::vector<uint8_t> pixels(size_t(width) * height * 4);
            CdFrame frame{sizeof(frame), width, height, width * 4, pixels.size(), pixels.data(), dpi};
            for (bool dark : {false, true}) {
                for (const auto* palette : {"amber", "mint", "cyan"}) {
                    Options options;
                    options.dark = dark;
                    options.color = palette;
                    options.showTimer = dimensions.first >= 100;
                    options.physicalSand = &physical;
                    check(renderer.render(model, options, frame, {}), "DPI/day-night rendering failed");
                    for (size_t i = 0; i < pixels.size(); i += 4) {
                        check(pixels[i] <= pixels[i + 3] && pixels[i + 1] <= pixels[i + 3] &&
                                  pixels[i + 2] <= pixels[i + 3],
                              "Non-premultiplied RGBA output");
                    }
                    ++renders;
                }
            }
            Options transparent;
            transparent.background = false;
            transparent.showTimer = false;
            transparent.physicalSand = &physical;
            check(renderer.render(model, transparent, frame, {}), "Transparent centered hourglass failed");
            size_t clear = 0;
            for (size_t i = 3; i < pixels.size(); i += 4) {
                clear += pixels[i] == 0;
            }
            check(clear > pixels.size() / 8, "Background toggle did not clear the card");
        }
    }
    check(renders == 144, "Expected 144 rendering combinations");
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        simulation();
        physicalSimulation();
        lifecycle();
        square();
        rendering();
        std::cout
            << "PASS: sand conservation, containment, completion, automatic flip, frame independence, input/drag, "
               "pause/resume, settings rollback, pure square hourglass, 144 DPI/theme/color renders and transparency\n";
        if (argc > 1) {
            const std::filesystem::path path(argv[1]);
            std::filesystem::create_directories(path.parent_path());
            std::ofstream output(path);
            output << "{\"passed\":true,\"sandGrains\":105,\"sandConservation\":true,\"noWallLeaks\":true,"
                      "\"automaticFlip\":true,\"pauseClickResetAndDrag\":true,\"oneShotStopsTimer\":true,"
                      "\"transactionalSettings\":true,\"transparentBackground\":true,\"squareHasNoText\":true,"
                      "\"squareClickPauseAndReset\":true,\"dpiThemeColorRenders\":144,\"physicalGrains\":105,"
                      "\"physicalFlipFps\":[15,20,30],\"physicalModeToggle\":true}\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
