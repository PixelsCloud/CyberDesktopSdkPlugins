#include <Windows.h>
#include <d2d1.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "plugins/koi-suite/Pond.h"

namespace {
void check(bool passed, const char* message) {
    if (!passed) {
        throw std::runtime_error(message);
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
    int redraws = 0;
    bool fail = false;

    static void CD_CALL redraw(void* pointer) {
        ++static_cast<Host*>(pointer)->redraws;
    }

    static CdResult CD_CALL subscribe(void* pointer, uint32_t interval, CdTimerCallback callback, void* user,
                                      uint64_t* token) {
        auto& self = *static_cast<Host*>(pointer);
        if (self.fail) {
            return CD_ERROR_INTERNAL;
        }
        *token = self.next++;
        self.timers.emplace(*token, Timer{callback, user, interval});
        return CD_OK;
    }

    static void CD_CALL stop(void* pointer, uint64_t token) {
        check(static_cast<Host*>(pointer)->timers.erase(token) == 1, "Duplicate unsubscribe");
    }

    CdHostApi api{sizeof(api), CD_PLUGIN_ABI_VERSION, redraw, nullptr, nullptr, nullptr, stop, subscribe};

    void advance(int milliseconds) {
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

    explicit Plugin(koiplugin::Spec spec) {
        const CdCreateContext context{
            sizeof(context), CD_PLUGIN_ABI_VERSION, spec.id, "test-instance", ".", &host.api, &host};
        check(koiplugin::create(spec, &context, &api, sizeof(api)) == CD_OK, "Creation failed");
    }

    ~Plugin() {
        api.destroy(api.instance);
        if (!host.timers.empty()) {
            std::terminate();
        }
    }

    void setting(const char* id, uint32_t type, int64_t integer = 0, double number = 0, const char* text = nullptr) {
        const CdSettingValue value{sizeof(value), type, id, integer, number, text};
        check(api.apply_settings(api.instance, &value, 1) == CD_OK, "Settings failed");
    }

    void layout(double w, double h, bool night, double dpr = 1) {
        const uint8_t text = night ? 235 : 30;
        const CdLayout value{sizeof(value),      0, 0, 0, nullptr, w, h, dpr, {0, 0, w, h}, {text, text, text, 255},
                             {80, 170, 130, 255}};
        api.layout_changed(api.instance, &value);
    }
};

void closedHabitat() {
    using namespace koiplugin;
    for (const auto dimensions : {Vec{196, 101}, Vec{180, 235}, Vec{310, 140}}) {
        for (uint32_t seed : {7u, 71u, 913u}) {
            Scene scene;
            const auto inside = [dimensions](Vec p, double clearance) {
                const double cut = std::min(dimensions.x, dimensions.y) * .19;
                const double diagonal = cut + clearance * std::sqrt(2.);
                return p.x >= clearance && p.y >= clearance && p.x <= dimensions.x - clearance &&
                       p.y <= dimensions.y - clearance && p.x + p.y >= diagonal &&
                       dimensions.x - p.x + p.y >= diagonal && p.x + dimensions.y - p.y >= diagonal &&
                       dimensions.x - p.x + dimensions.y - p.y >= diagonal;
            };
            scene.setBoundary(inside);
            scene.configure(dimensions.x, dimensions.y, 5, seed, fishLengthForGrid(dimensions.x, dimensions.y, 2, 4));
            scene.setMouseBehavior(MouseBehavior::Follow);
            std::vector<Vec> low, high;
            for (const auto& fish : scene.fish()) {
                low.push_back(fish.joints[0]);
                high.push_back(fish.joints[0]);
            }
            for (unsigned step = 0; step < 14400; ++step) {
                if (step < 7200) {
                    const unsigned corner = step / 1800;
                    scene.pointer({corner % 2 ? dimensions.x : 0, corner / 2 ? dimensions.y : 0});
                } else {
                    scene.clearPointer();
                }
                scene.advance(1. / 60, 1);
                for (size_t i = 0; i < scene.fish().size(); ++i) {
                    const auto& fish = scene.fish()[i];
                    for (const auto joint : fish.joints) {
                        check(inside(joint, fish.link * 2.6 + 1), "Closed-habitat fish escaped the water");
                    }
                    const auto head = fish.joints[0];
                    low[i] = {std::min(low[i].x, head.x), std::min(low[i].y, head.y)};
                    high[i] = {std::max(high[i].x, head.x), std::max(high[i].y, head.y)};
                    if (step % 600 == 599) {
                        if ((high[i] - low[i]).length() < fish.link * 2) {
                            std::cerr << "STALLED seed=" << seed << " size=" << dimensions.x << ',' << dimensions.y
                                      << " fish=" << i << " time=" << step / 60. << '\n';
                            check(false, "Closed-habitat fish remained trapped in a corner for ten seconds");
                        }
                        low[i] = high[i] = head;
                    }
                }
            }
        }
    }
    std::cout << "PASS closed habitats: corner following and free swimming, 9 seeded 240s runs\n";
}

void simulation() {
    using namespace koiplugin;
    for (const auto dimensions : {Vec{280, 136}, Vec{280, 280}, Vec{424, 208}, Vec{280, 592}, Vec{24, 24}}) {
        for (double speed : {.3, 1., 2.}) {
            Scene scene;
            scene.configure(dimensions.x, dimensions.y, 12, 71);
            for (int step = 0; step < 7200; ++step) {
                scene.advance(1.0 / 60, speed);
                for (const auto& fish : scene.fish()) {
                    const double margin = fish.link * 24 + 20;
                    check(fish.joints[0].x > -margin && fish.joints[0].x < dimensions.x + margin &&
                              fish.joints[0].y > -margin && fish.joints[0].y < dimensions.y + margin,
                          "Fish wandered indefinitely offscreen");
                    for (size_t i = 1; i < fish.joints.size(); ++i) {
                        check(std::isfinite(fish.joints[i].x) && std::isfinite(fish.joints[i].y),
                              "Nonfinite fish geometry");
                        check(std::abs((fish.joints[i] - fish.joints[i - 1]).length() - fish.link) < .000001,
                              "Upstream chain link length changed");
                        check(std::abs(angleDifference(fish.angles[i], fish.angles[i - 1])) <= kPi / 8 + .000001,
                              "Upstream joint-angle constraint broken");
                    }
                }
            }
            check(scene.reentries() > 0, "Fish never left and re-entered the pond");
        }
    }
    Scene a, b;
    a.configure(400, 200, 5, 7);
    b.configure(400, 200, 5, 7);
    for (int i = 0; i < 120; ++i) {
        a.advance(1.0 / 30, 1);
        b.advance(1.0 / 60, 1);
        b.advance(1.0 / 60, 1);
    }
    check((a.fish()[0].joints[0] - b.fish()[0].joints[0]).length() < .000001, "Frame-rate-dependent movement");
    a.advance(3600, 1);
    check(a.time() < 4.16, "Unbounded catch-up after sleep");
    for (int i = 0; i < 30; ++i) {
        a.ripple({100, 100});
    }
    check(a.ripples().size() == 8, "Unbounded ripple queue");
    for (int i = 0; i < 90; ++i) {
        a.advance(1.0 / 30, 1);
    }
    check(a.ripples().empty(), "Expired ripples retained");
}

void finsAndLeaves() {
    using namespace koiplugin;
    Scene scene;
    scene.configure(500, 300, 1, 7);
    const Fish source = scene.fish().front();
    for (int joint : {3, 7}) {
        for (int side : {-1, 1}) {
            const auto pose = finPose(source, joint, side);
            check((pose.center - source.edge(joint, side * (joint == 3 ? kPi / 3 : kPi / 2))).length() < .000001,
                  "Fin attachment differs from upstream reference");
            check(std::abs(angleDifference(pose.angle, source.angles[joint - 1]) + side * kPi / 4) < .000001 &&
                      std::abs(pose.radiusX - (joint == 3 ? 80 : 48) * source.link / 64) < .000001 &&
                      std::abs(pose.radiusY - (joint == 3 ? 32 : 16) * source.link / 64) < .000001,
                  "Fin angle or proportions differ from upstream reference");
            Fish rotated = source;
            for (size_t i = 0; i < rotated.joints.size(); ++i) {
                rotated.joints[i] = {-source.joints[i].y, source.joints[i].x};
                rotated.angles[i] += kPi / 2;
            }
            const auto turned = finPose(rotated, joint, side);
            check((turned.center - Vec{-pose.center.y, pose.center.x}).length() < .000001 &&
                      std::abs(angleDifference(turned.angle, pose.angle) - kPi / 2) < .000001,
                  "Fin pivot or direction failed to follow body rotation");
            Fish steering = source;
            steering.angles[joint - 1] += .2;
            const auto turning = finPose(steering, joint, side);
            check(std::abs(angleDifference(turning.angle, pose.angle)) > .1, "Fin did not respond to turning");
            check((turning.center - pose.center).length() < .000001 && turning.radiusX == pose.radiusX,
                  "Fin was detached or stretched instead of following its spine");
        }
    }
    for (Vec size : {Vec{280, 136}, Vec{280, 280}, Vec{424, 208}, Vec{280, 592}}) {
        const auto pads = lilyPads(size.x, size.y);
        int notched = 0;
        double largestNotch = 0, smallestFull = std::numeric_limits<double>::max();
        for (const auto& pad : pads) {
            if (pad.notched) {
                ++notched;
                largestNotch = std::max(largestNotch, pad.radius);
            } else {
                smallestFull = std::min(smallestFull, pad.radius);
            }
        }
        check(notched == 2 && largestNotch <= smallestFull, "Leaves other than the smallest two were notched");
    }
}

void tailArticulation() {
    using namespace koiplugin;
    Scene scene;
    scene.configure(280, 136, 1, 7, fishLengthForGrid(280, 136, 2, 4));
    Fish fish = scene.fish().front();
    for (size_t i = 0; i < fish.joints.size(); ++i) {
        fish.joints[i] = Vec{140, 68} - Vec{fish.link * i, 0};
        fish.angles[i] = 0;
    }
    double minimumWidth = std::numeric_limits<double>::max(), maximumWidth = 0;
    for (double phase : {0., kPi / 2, kPi, 3 * kPi / 2}) {
        fish.finPhase = phase;
        double referenceWidth = 0;
        for (double angle : {0., kPi / 4, kPi / 2, kPi, -kPi / 2}) {
            for (size_t i = 0; i < fish.joints.size(); ++i) {
                fish.angles[i] = angle;
                fish.joints[i] = Vec{140, 68} - direction(angle) * (fish.link * i);
            }
            const auto straight = tailOutline(fish);
            const double width = (straight[3] - straight[4]).length();
            check(width > fish.link * .33 && width < fish.link * .47,
                  "Straight tail lost its subtle visible tilt or became a broad fan");
            if (referenceWidth) {
                check(std::abs(width - referenceWidth) < .000001, "Straight tail tilt depended on screen rotation");
            }
            referenceWidth = width;
            minimumWidth = std::min(minimumWidth, width);
            maximumWidth = std::max(maximumWidth, width);
        }
    }
    check(maximumWidth > minimumWidth * 1.25, "Straight tail tilt did not sway with the swimming phase");
    for (double bend : {-1.5, -1.3, -1.1, 0., 1.1, 1.3, 1.5}) {
        for (double phase : {0., kPi / 2, 3 * kPi / 2}) {
            fish.finPhase = phase;
            for (size_t i = 0; i < fish.joints.size(); ++i) {
                fish.angles[i] = kPi / 2 - bend * i / 11;
                fish.joints[i] = i ? fish.joints[i - 1] - direction(fish.angles[i]) * fish.link : Vec{140, 100};
            }
            const auto outline = tailOutline(fish);
            check((outline[3] - outline[4]).length() > fish.link * .33,
                  "Turning cancelled the resting tail projection into a line");
        }
    }
    fish.finPhase = 0;
    double referenceArea = 0;
    for (double angle : {0., kPi / 4, kPi / 2, 3 * kPi / 4, kPi, -kPi / 2}) {
        for (size_t i = 0; i < fish.joints.size(); ++i) {
            fish.angles[i] = angle + i * .08;
            fish.joints[i] = i ? fish.joints[i - 1] - direction(fish.angles[i]) * fish.link : Vec{140, 68};
        }
        const auto outline = tailOutline(fish);
        double area = 0;
        for (size_t i = 0; i < outline.size(); ++i) {
            const auto a = outline[i], b = outline[(i + 1) % outline.size()];
            area += a.x * b.y - b.x * a.y;
        }
        area = std::abs(area) / 2;
        check(area > fish.link * fish.link * .02 && (outline[3] - outline[4]).length() > fish.link * .05,
              "Tail did not open with spine bending");
        if (referenceArea) {
            check(std::abs(area - referenceArea) < .000001, "Tail articulation depended on screen angle");
        }
        referenceArea = area;
    }
}

void edgeCrossings() {
    using namespace koiplugin;
    Scene scene;
    scene.configure(400, 240, 1, 23);
    bool headLeftWhileTailVisible = false, reappeared = false;
    uint64_t entries = 0;
    for (int frame = 0; frame < 18000; ++frame) {
        scene.advance(1.0 / 60, 2);
        const auto& fish = scene.fish().front();
        const auto bounds = fishBounds(fish);
        const auto outside = [](CdRect area) {
            return area.x + area.width < 0 || area.x > 400 || area.y + area.height < 0 || area.y > 240;
        };
        const auto head = fish.joints[0];
        if (fish.leaving && !outside(bounds)) {
            headLeftWhileTailVisible |= head.x < 0 || head.x > 400 || head.y < 0 || head.y > 240;
        }
        if (scene.reentries() != entries) {
            check(outside(scene.lastExitBounds()), "Fish teleported before its complete silhouette left the frame");
            check(fish.entering && outside(bounds), "Fish re-entry popped into visible water");
            const auto old = scene.lastExitBounds();
            const bool opposite = old.x + old.width < 0    ? bounds.x > 400
                                  : old.x > 400            ? bounds.x + bounds.width < 0
                                  : old.y + old.height < 0 ? bounds.y > 240
                                                           : bounds.y + bounds.height < 0;
            check(opposite, "Fish did not re-enter from the opposite side");
            entries = scene.reentries();
        } else if (entries > 0 && fish.entering && !outside(bounds)) {
            reappeared = true;
        }
        if (entries >= 4 && reappeared && headLeftWhileTailVisible) {
            break;
        }
    }
    check(entries >= 4 && reappeared && headLeftWhileTailVisible, "Exit and gradual re-entry lifecycle failed");
}

void mouseBehavior() {
    using namespace koiplugin;
    const Vec center{300, 180};
    Scene idle, startled, following;
    for (auto* scene : {&idle, &startled, &following}) {
        scene->configure(600, 360, 5, 7);
    }
    following.setMouseBehavior(MouseBehavior::Follow);
    following.pointer(center);
    startled.click(center);
    const auto distance = [&](const Scene& scene) {
        double sum = 0;
        for (const auto& fish : scene.fish()) {
            sum += (fish.joints[0] - center).length();
        }
        return sum / scene.fish().size();
    };
    const double initialDistance = distance(idle);
    bool accelerated = false;
    for (int i = 0; i < 90; ++i) {
        idle.advance(1.0 / 60, 1);
        startled.advance(1.0 / 60, 1);
        for (const auto& fish : startled.fish()) {
            accelerated |= fish.alarm > 0;
        }
        following.advance(1.0 / 60, 1);
    }
    check(distance(startled) > distance(idle) + 10, "Click failed to drive fish away");
    check(accelerated, "Scare reaction did not accelerate fish");
    // Random startup headings need time to turn or finish an edge crossing before
    // approaching the pointer; the school no longer starts facing the center.
    for (int i = 0; i < 360; ++i) {
        following.advance(1.0 / 60, 1);
    }
    check(distance(following) < initialDistance - 20, "Fish failed to approach pointer");
    Scene held = following;
    following.clearPointer();
    for (int i = 0; i < 90; ++i) {
        following.advance(1.0 / 60, 1);
        held.advance(1.0 / 60, 1);
        startled.advance(1.0 / 60, 1);
    }
    double roamingDifference = 0;
    // A fish crossing the edge must finish that crossing before reacting to the
    // pointer. Verify the school instead of assuming its first fish is following.
    for (size_t index = 0; index < following.fish().size(); ++index) {
        roamingDifference =
            std::max(roamingDifference, (following.fish()[index].joints[0] - held.fish()[index].joints[0]).length());
    }
    check(roamingDifference > 2, "Leaving pointer did not resume roaming");
    check(startled.fish()[0].alarm == 0, "Fish remained permanently frightened");
    following.configure(600, 360, 12, 7);
    check(following.fish().size() == 12, "Fish count increase failed");
    following.configure(600, 360, 1, 7);
    check(following.fish().size() == 1, "Fish count reduction failed");
}

void lifecycle() {
    using namespace koiplugin;
    const Spec catalog[]{{"2x4", 2, 4, 5}, {"4x4", 4, 4, 8}, {"3x6", 3, 6, 9}};
    for (const auto& spec : catalog) {
        Plugin plugin(spec), second(spec);
        plugin.layout(spec.columns * 72, spec.rows * 72, false);
        check(plugin.host.timers.size() == 1 && plugin.host.timers.begin()->second.interval == 17,
              "Smooth blessing transitions must share one 60 fps timer");
        CdSettingValue behavior{};
        check(plugin.api.get_setting(plugin.api.instance, "mouse_behavior", &behavior, sizeof(behavior)) == CD_OK &&
                  std::string(behavior.text) == "flee",
              "Default mouse mode is not flee");
        const uint32_t width = spec.columns * 72, height = spec.rows * 72;
        std::vector<uint8_t> pixels(size_t(width) * height * 4);
        CdFrame frame{sizeof(frame), width, height, width * 4, pixels.size(), pixels.data(), 1};
        check(plugin.api.render(plugin.api.instance, &frame) == CD_OK, "Initial render failed");
        auto before = pixels;
        plugin.host.advance(200);
        check(plugin.api.render(plugin.api.instance, &frame) == CD_OK && pixels != before, "Fish did not animate");

        plugin.api.set_paused(plugin.api.instance, 1);
        plugin.api.set_paused(plugin.api.instance, 1);
        check(plugin.host.timers.empty(), "Paused timer retained");
        before = pixels;
        const int redraws = plugin.host.redraws;
        plugin.host.advance(3600000);
        check(plugin.api.render(plugin.api.instance, &frame) == CD_OK && pixels == before, "Paused pond changed");
        check(plugin.host.redraws == redraws, "Paused pond requested frames");
        plugin.api.set_paused(plugin.api.instance, 0);
        plugin.api.set_paused(plugin.api.instance, 0);
        check(plugin.host.timers.size() == 1, "Resume duplicated timers");

        plugin.setting("animation", CD_SETTING_TOGGLE, 0);
        check(plugin.host.timers.empty(), "Still pond kept a timer");
        plugin.layout(width, height, true);
        check(plugin.api.render(plugin.api.instance, &frame) == CD_OK && pixels != before,
              "Dark theme did not switch to night");
        const auto night = pixels;
        plugin.setting("lighting", CD_SETTING_CHOICE, 0, 0, "day");
        check(plugin.api.render(plugin.api.instance, &frame) == CD_OK && pixels != night, "Manual day override failed");
        plugin.layout(width, height, true);
        check(plugin.api.render(plugin.api.instance, &frame) == CD_OK && pixels != night, "Theme overwrote manual day");

        const CdSettingValue invalid[]{{sizeof(CdSettingValue), CD_SETTING_INTEGER, "fish_count", 2},
                                       {sizeof(CdSettingValue), CD_SETTING_INTEGER, "seed", 0}};
        check(plugin.api.apply_settings(plugin.api.instance, invalid, 2) == CD_ERROR_ARGUMENT,
              "Invalid settings accepted");
        CdSettingValue stored{};
        plugin.api.get_setting(plugin.api.instance, "fish_count", &stored, sizeof(stored));
        check(stored.integer == spec.fishCount, "Settings transaction was not atomic");
        plugin.setting("fish_count", CD_SETTING_INTEGER, 2);
        plugin.setting("mouse_behavior", CD_SETTING_CHOICE, 0, 0, "follow");
        plugin.setting("show_background", CD_SETTING_TOGGLE, 0);
        check(plugin.api.render(plugin.api.instance, &frame) == CD_OK, "Transparent plugin render failed");
        second.api.get_setting(second.api.instance, "fish_count", &stored, sizeof(stored));
        check(stored.integer == spec.fishCount, "Instance settings leaked");
        plugin.host.fail = true;
        const CdSettingValue enable{sizeof(enable), CD_SETTING_TOGGLE, "animation", 1};
        check(plugin.api.apply_settings(plugin.api.instance, &enable, 1) == CD_ERROR_INTERNAL,
              "Timer failure was hidden");
        plugin.api.get_setting(plugin.api.instance, "animation", &stored, sizeof(stored));
        check(stored.integer == 0 && plugin.host.timers.empty(), "Failed timer changed settings");
        plugin.host.fail = false;
        plugin.setting("animation", CD_SETTING_TOGGLE, 1);
        plugin.setting("frame_rate", CD_SETTING_CHOICE, 0, 0, "30");
        check(plugin.host.timers.size() == 1 && plugin.host.timers.begin()->second.interval == 17,
              "Frame rate change leaked subscription");
        plugin.setting("blessings", CD_SETTING_TOGGLE, 0);
        check(plugin.host.timers.size() == 1 && plugin.host.timers.begin()->second.interval == 34,
              "Disabling blessings did not restore the selected pond frame rate");
        plugin.setting("blessings", CD_SETTING_TOGGLE, 1);
        check(plugin.host.timers.size() == 1 && plugin.host.timers.begin()->second.interval == 17,
              "Enabling blessings did not restore smooth composition");
        check(plugin.api.get_setting(plugin.api.instance, "seed", &stored, sizeof(stored) - 1) == CD_ERROR_BUFFER,
              "Short settings buffer accepted");
        uint64_t size = 100;
        check(plugin.api.save_state(plugin.api.instance, nullptr, 0, &size) == CD_OK && size == 0,
              "Unexpected mutable package state");
        check(plugin.api.restore_state(plugin.api.instance, nullptr, 0) == CD_OK, "State restoration failed");
        frame.capacity = 1;
        check(plugin.api.render(plugin.api.instance, &frame) == CD_ERROR_ARGUMENT, "Short frame buffer accepted");
        frame.capacity = pixels.size();
        frame.device_pixel_ratio = std::numeric_limits<double>::quiet_NaN();
        check(plugin.api.render(plugin.api.instance, &frame) == CD_ERROR_ARGUMENT, "NaN DPI accepted");
    }
}

void sizingAndStartup() {
    using namespace koiplugin;
    for (Vec cell : {Vec{72, 72}, Vec{96, 96}, Vec{72, 150}}) {
        const double length = fishLengthForGrid(cell.x * 4 - 8, cell.y * 2 - 8, 2, 4);
        for (Vec footprint : {Vec{2, 4}, Vec{4, 4}, Vec{3, 6}}) {
            const double w = cell.x * footprint.y - 8, h = cell.y * footprint.x - 8;
            const double normalized = fishLengthForGrid(w, h, int(footprint.x), int(footprint.y));
            check(std::abs(normalized - length) < .000001, "Larger footprint enlarged its fish");
            Scene scene;
            scene.configure(w, h, 8, 71, normalized);
            for (const auto& fish : scene.fish()) {
                check(fish.link * 11 >= length * .80 && fish.link * 11 <= length * 1.16,
                      "Fish size escaped the 2x4 reference range");
            }
        }
    }

    Scene scene;
    scene.configure(500, 300, 12, 7);
    double minimum = std::numeric_limits<double>::max(), maximum = 0;
    bool headingsDiffer = false;
    for (const auto& fish : scene.fish()) {
        const double radius = std::hypot((fish.joints[0].x - 250) / 500, (fish.joints[0].y - 150) / 300);
        minimum = std::min(minimum, radius);
        maximum = std::max(maximum, radius);
        headingsDiffer |= std::abs(angleDifference(fish.heading, scene.fish().front().heading)) > .5;
    }
    check(maximum - minimum > .12 && headingsDiffer, "Startup school retained a regular ring");
    Plugin first({"startup-a", 2, 4, 5}), second({"startup-b", 2, 4, 5});
    first.layout(280, 136, false);
    second.layout(280, 136, false);
    std::vector<uint8_t> pixels(280 * 136 * 4);
    CdFrame frame{sizeof(frame), 280, 136, 280 * 4, pixels.size(), pixels.data(), 1};
    check(first.api.render(first.api.instance, &frame) == CD_OK, "First startup render failed");
    const auto original = pixels;
    check(first.api.render(first.api.instance, &frame) == CD_OK && pixels == original,
          "Pond reshuffled on every render");
    check(second.api.render(second.api.instance, &frame) == CD_OK && pixels != original,
          "New instances reused the same startup school");
}

void interaction() {
    using namespace koiplugin;
    Plugin plugin({"input", 2, 4, 5});
    plugin.layout(280, 136, false);
    std::vector<uint8_t> pixels(280 * 136 * 4);
    CdFrame frame{sizeof(frame), 280, 136, 280 * 4, pixels.size(), pixels.data(), 1};
    check(plugin.api.render(plugin.api.instance, &frame) == CD_OK, "Interaction render failed");
    const auto before = pixels;
    plugin.host.redraws = 0;
    CdInput event{sizeof(event), CD_INPUT_PRESS, CD_BUTTON_LEFT, 0, 140, 70, 0};
    plugin.api.input(plugin.api.instance, &event);
    event.kind = CD_INPUT_MOVE;
    event.x += 20;
    plugin.api.input(plugin.api.instance, &event);
    event.kind = CD_INPUT_RELEASE;
    event.buttons = 0;
    event.x -= 20;
    plugin.api.input(plugin.api.instance, &event);
    check(plugin.host.redraws == 0 && plugin.api.render(plugin.api.instance, &frame) == CD_OK && pixels == before,
          "Dragging changed the pond");

    event.kind = CD_INPUT_PRESS;
    event.buttons = CD_BUTTON_LEFT;
    plugin.api.input(plugin.api.instance, &event);
    event.kind = CD_INPUT_RELEASE;
    event.buttons = 0;
    plugin.api.input(plugin.api.instance, &event);
    check(plugin.host.redraws == 1, "Real left release did not trigger the click reaction");
}

void blessingWords() {
    using namespace koiplugin;
    const std::set<std::wstring_view> vocabulary(kBlessingWords.begin(), kBlessingWords.end());
    check(vocabulary.size() == 50, "Blessing vocabulary must contain 50 distinct words");
    for (const auto word : vocabulary) {
        check(!word.empty() && word.size() <= 4, "Blessing length does not fit the pond");
        for (const auto character : word) {
            check(character >= L'\u4e00' && character <= L'\u9fff', "Blessing included non-Chinese text");
        }
    }
    for (Vec dimensions : {Vec{70, 70}, Vec{280, 136}, Vec{280, 280}, Vec{424, 208}}) {
        Scene scene;
        scene.configure(dimensions.x, dimensions.y, 3, 79);
        std::set<int> seen;
        int previous = -1, appearances = 0;
        bool faded = false, gap = false;
        for (int frame = 0; frame < 24000 && appearances < 51; ++frame) {
            const auto& blessing = scene.blessing();
            const double opacity = blessing.opacity();
            check(opacity >= 0 && opacity <= 1, "Blessing fade escaped opacity range");
            faded |= opacity > 0 && opacity < .99;
            gap |= blessing.word < 0;
            if (blessing.word >= 0 && blessing.word != previous) {
                const auto word = kBlessingWords[blessing.word];
                const double font = blessingFontSize(dimensions.x, dimensions.y, word.size());
                const double halfWidth = (font + 2) * word.size() * .5;
                check(blessing.center.x - halfWidth >= 11.9 && blessing.center.x + halfWidth <= dimensions.x - 11.9,
                      "Blessing text extended beyond pond margins");
                if (appearances < 50) {
                    check(seen.insert(blessing.word).second, "Blessings repeated before the 50-word round finished");
                }
                previous = blessing.word;
                ++appearances;
            }
            scene.advance(1.0 / 60, 1);
        }
        check(appearances == 51 && seen.size() == 50 && faded && gap, "Blessing cycle, fade or quiet gap missing");
    }

    Scene scene;
    scene.configure(280, 136, 1, 7);
    Renderer renderer;
    std::vector<uint8_t> pixels(280 * 136 * 4);
    CdFrame frame{sizeof(frame), 280, 136, 280 * 4, pixels.size(), pixels.data(), 1};
    Appearance appearance;
    appearance.background = appearance.leaves = appearance.flowers = false;
    appearance.shimmer = 0;
    appearance.blessings = false;
    check(renderer.render(scene, appearance, frame, {}), "Blessing-free rendering failed");
    const auto quiet = pixels;
    appearance.blessings = true;
    check(renderer.render(scene, appearance, frame, {}) && pixels != quiet, "Blessing text did not render");
    const auto shown = pixels;
    check(renderer.render(scene, appearance, frame, {}) && pixels == shown, "Rendering reshuffled blessing text");
    appearance.blessings = false;
    check(renderer.render(scene, appearance, frame, {}) && pixels == quiet, "Blessing toggle left stale text");
    Plugin plugin({"blessings", 2, 4, 5});
    plugin.layout(280, 136, false);
    plugin.setting("blessings", CD_SETTING_TOGGLE, 0);
    CdSettingValue stored{};
    check(plugin.api.get_setting(plugin.api.instance, "blessings", &stored, sizeof(stored)) == CD_OK &&
              stored.integer == 0 && plugin.host.timers.size() == 1,
          "Blessing setting failed or added a timer");
}

void markingClipping() {
    using namespace koiplugin;
    using Microsoft::WRL::ComPtr;
    ComPtr<ID2D1Factory> factory;
    check(SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf())),
          "Clipping test factory failed");
    Renderer renderer;
    Scene scene;
    scene.configure(300, 180, 5, 41);
    for (int pose = 0; pose < 3; ++pose) {
        scene.advance(.15, 1);
        std::vector<ComPtr<ID2D1PathGeometry>> bodies;
        for (const auto& fish : scene.fish()) {
            ComPtr<ID2D1PathGeometry> body;
            check(SUCCEEDED(factory->CreatePathGeometry(body.GetAddressOf())), "Clipping test body failed");
            ComPtr<ID2D1GeometrySink> sink;
            check(SUCCEEDED(body->Open(sink.GetAddressOf())), "Clipping test sink failed");
            const auto edges = bodyOutline(fish);
            const auto point = [](Vec value) {
                return D2D1::Point2F(float(value.x), float(value.y));
            };
            sink->BeginFigure(point(edges.front()), D2D1_FIGURE_BEGIN_FILLED);
            for (size_t i = 0; i < edges.size(); ++i) {
                const auto p0 = edges[(i + edges.size() - 1) % edges.size()];
                const auto p1 = edges[i], p2 = edges[(i + 1) % edges.size()], p3 = edges[(i + 2) % edges.size()];
                sink->AddBezier(D2D1::BezierSegment(point(p1 + (p2 - p0) * (1.0 / 6)),
                                                    point(p2 - (p3 - p1) * (1.0 / 6)), point(p2)));
            }
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            check(SUCCEEDED(sink->Close()), "Clipping test path failed");
            bodies.push_back(body);
        }
        for (double dpi : {1., 2.}) {
            const uint32_t width = uint32_t(326 * dpi), height = uint32_t(198 * dpi);
            std::vector<uint8_t> pixels(size_t(width) * height * 4);
            CdFrame frame{sizeof(frame), width, height, width * 4, pixels.size(), pixels.data(), dpi};
            for (double night : {0., 1.}) {
                Appearance appearance;
                appearance.background = appearance.leaves = appearance.flowers = false;
                appearance.shimmer = 0;
                appearance.night = night;
                appearance.koiColor = 0xF020CFFF;
                const CdRect safe{13, 9, 300, 180};
                check(renderer.render(scene, appearance, frame, safe), "First marking render failed");
                const auto first = pixels;
                appearance.koiColor = 0x10DDF0FF;
                check(renderer.render(scene, appearance, frame, safe), "Second marking render failed");
                size_t changed = 0;
                for (uint32_t y = 0; y < height; ++y) {
                    for (uint32_t x = 0; x < width; ++x) {
                        const size_t offset = (size_t(y) * width + x) * 4;
                        if (std::abs(int(pixels[offset]) - first[offset]) < 6 &&
                            std::abs(int(pixels[offset + 1]) - first[offset + 1]) < 6 &&
                            std::abs(int(pixels[offset + 2]) - first[offset + 2]) < 6) {
                            continue;
                        }
                        ++changed;
                        const auto sample =
                            D2D1::Point2F(float((x + .5) / dpi - safe.x), float((y + .5) / dpi - safe.y));
                        bool contained = false;
                        for (const auto& body : bodies) {
                            BOOL inside = FALSE, edge = FALSE;
                            check(SUCCEEDED(body->FillContainsPoint(sample, nullptr, &inside)),
                                  "Body containment query failed");
                            check(SUCCEEDED(body->StrokeContainsPoint(sample, float(2 / dpi), nullptr, nullptr, &edge)),
                                  "Body antialias boundary query failed");
                            contained |= inside || edge;
                        }
                        check(contained, "Koi markings escaped the body onto fins or background");
                    }
                }
                check(changed > 50, "Custom marking color did not affect the fish");
            }
        }
    }
}

double smoothRendering() {
    using namespace koiplugin;
    Plugin plugin({"motion-settings", 2, 4, 5});
    plugin.layout(280, 136, false);
    CdSettingValue speed{};
    check(plugin.api.get_setting(plugin.api.instance, "motion_speed", &speed, sizeof(speed)) == CD_OK &&
              speed.number == .75,
          "Motion speed must default to 0.75x");
    plugin.setting("blessings", CD_SETTING_TOGGLE, 0);
    std::vector<uint8_t> pluginPixels(280 * 136 * 4);
    CdFrame pluginFrame{sizeof(pluginFrame), 280, 136, 280 * 4, pluginPixels.size(), pluginPixels.data(), 1};
    plugin.host.advance(100);
    check(plugin.api.render(plugin.api.instance, &pluginFrame) == CD_OK, "Motion settings render failed");
    const auto beforeSpeedChange = pluginPixels;
    plugin.setting("motion_speed", CD_SETTING_NUMBER, 0, .5);
    check(plugin.api.render(plugin.api.instance, &pluginFrame) == CD_OK && pluginPixels == beforeSpeedChange,
          "Changing motion speed jumped the current animation pose");
    const CdSettingValue invalid{sizeof(invalid), CD_SETTING_NUMBER, "motion_speed", 0, 0};
    check(plugin.api.apply_settings(plugin.api.instance, &invalid, 1) == CD_ERROR_ARGUMENT,
          "Invalid motion speed accepted");
    check(plugin.api.get_setting(plugin.api.instance, "motion_speed", &speed, sizeof(speed)) == CD_OK &&
              speed.number == .5,
          "Invalid motion speed changed the saved setting");

    Renderer renderer;
    double elapsed = 0;
    int rendered = 0;
    for (double dpr : {1., 1.5, 2.}) {
        Scene scene;
        scene.configure(280, 136, 5, 7);
        const uint32_t width = uint32_t(280 * dpr), height = uint32_t(136 * dpr);
        std::vector<uint8_t> pixels(size_t(width) * height * 4);
        CdFrame frame{sizeof(frame), width, height, width * 4, pixels.size(), pixels.data(), dpr};
        Appearance appearance;
        appearance.blessings = false;
        check(renderer.render(scene, appearance, frame, {}), "Pond layer warmup failed");
        const auto base = pixels;
        appearance.redrawPond = false;
        scene.advance(.05, 1);
        check(renderer.render(scene, appearance, frame, {}) && pixels == base,
              "Overlay frame repainted the pond layer");
        appearance.blessings = true;
        check(renderer.render(scene, appearance, frame, {}) && pixels != base, "Cached pond lost the blessing overlay");
        const auto shown = pixels;
        check(renderer.render(scene, appearance, frame, {}) && pixels == shown, "Cached text was not deterministic");
        const uint32_t paddedWidth = uint32_t(296 * dpr), paddedHeight = uint32_t(152 * dpr);
        std::vector<uint8_t> paddedPixels(size_t(paddedWidth) * paddedHeight * 4);
        CdFrame paddedFrame{sizeof(paddedFrame), paddedWidth,         paddedHeight, paddedWidth * 4,
                            paddedPixels.size(), paddedPixels.data(), dpr};
        appearance.redrawPond = true;
        check(renderer.render(scene, appearance, frame, {}), "Padding reference render failed");
        const auto paddingReference = pixels;
        check(renderer.render(scene, appearance, paddedFrame, {8, 8, 280, 136}), "Padded cached render failed");
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                const size_t source = (size_t(y) * width + x) * 4;
                const size_t destination = (size_t(y + uint32_t(8 * dpr)) * paddedWidth + x + uint32_t(8 * dpr)) * 4;
                for (int channel = 0; channel < 4; ++channel) {
                    check(std::abs(int(paddingReference[source + channel]) - paddedPixels[destination + channel]) <= 2,
                          "Cached text or pond shifted with host content padding");
                }
            }
        }
        appearance.blessings = false;
        check(renderer.render(scene, appearance, frame, {}), "Restoring the pond surface failed");
        const auto restoredBase = pixels;
        appearance.redrawPond = false;
        appearance.blessings = true;
        int changingFrames = 0;
        for (int index = 0; index < 60; ++index) {
            const auto previous = pixels;
            scene.advance(1.0 / 60, 1);
            const auto started = std::chrono::steady_clock::now();
            check(renderer.render(scene, appearance, frame, {}), "Smooth overlay render failed");
            elapsed += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
            ++rendered;
            changingFrames += pixels != previous;
        }
        check(changingFrames >= 50, "Text animation stalled between 60 fps samples");
        appearance.blessings = false;
        check(renderer.render(scene, appearance, frame, {}) && pixels == restoredBase,
              "Text fade left stale glyphs on the pond");
        appearance.redrawPond = true;
        check(renderer.render(scene, appearance, frame, {}) && pixels != restoredBase,
              "Pond layer stopped updating after composition");
        const auto moved = pixels;
        appearance.motionTime = 20;
        check(renderer.render(scene, appearance, frame, {}) && pixels != moved,
              "Independent motion clock did not reach the renderer");
    }
    return elapsed / rendered;
}

void blessingCache() {
    using namespace koiplugin;
    Renderer reused;
    for (double dpi : {1., 1.25, 1.5, 2.}) {
        for (size_t word = 0; word < kBlessingWords.size(); ++word) {
            const double w = word % 2 ? 280 : 284, h = 136;
            Scene scene;
            scene.configure(w, h, 1, 7);
            scene.advance(.25, 1);
            auto& blessing = const_cast<Blessing&>(scene.blessing());
            blessing.word = int(word);
            blessing.age = word % 2 ? .6 : kBlessingDuration - .4;
            const uint32_t pw = uint32_t(w * dpi), ph = uint32_t(h * dpi);
            std::vector<uint8_t> pixels(size_t(pw) * ph * 4), reference(pixels.size());
            CdFrame frame{sizeof(frame), pw, ph, pw * 4, pixels.size(), pixels.data(), dpi};
            auto expected = frame;
            expected.rgba = reference.data();
            Appearance appearance;
            appearance.night = double(word % 2);
            appearance.background = word % 3 != 0;
            Renderer fresh;
            check(fresh.render(scene, appearance, expected, {}), "Fresh blessing reference failed");
            check(reused.render(scene, appearance, frame, {}) && pixels == reference,
                  "Blessing cache changed font, spacing or size after word/DPI/theme switch");
            appearance.redrawPond = false;
            check(reused.render(scene, appearance, frame, {}) && pixels == reference,
                  "Cached blessing overlay differs from complete render");
        }
    }
}

double rendering() {
    using namespace koiplugin;
    Renderer renderer;
    for (Vec logical : {Vec{280, 136}, Vec{280, 280}, Vec{424, 208}, Vec{280, 592}}) {
        for (double dpi : {.5, 1., 2., 3.}) {
            const uint32_t width = uint32_t(logical.x * dpi), height = uint32_t(logical.y * dpi);
            std::vector<uint8_t> pixels(size_t(width) * height * 4);
            CdFrame frame{sizeof(frame), width, height, width * 4, pixels.size(), pixels.data(), dpi};
            Scene scene;
            scene.configure(logical.x, logical.y, 8, 7);
            for (double night : {0., 1.}) {
                check(renderer.render(scene, {night}, frame, {}), "DPI render failed");
                check(pixels[3] == 0, "Pool corner was not transparent");
                for (size_t i = 0; i < pixels.size(); i += 4) {
                    check(
                        pixels[i] <= pixels[i + 3] && pixels[i + 1] <= pixels[i + 3] && pixels[i + 2] <= pixels[i + 3],
                        "RGBA was not premultiplied");
                }
            }
        }
    }
    Scene transparentScene;
    transparentScene.configure(280, 136, 1, 7);
    std::vector<uint8_t> transparentPixels(280 * 136 * 4);
    CdFrame transparentFrame{sizeof(transparentFrame), 280, 136, 280 * 4, transparentPixels.size(),
                             transparentPixels.data(), 1};
    Appearance transparent;
    transparent.background = false;
    check(renderer.render(transparentScene, transparent, transparentFrame, {}), "Background-free rendering failed");
    size_t clear = 0, visible = 0;
    for (size_t i = 0; i < transparentPixels.size(); i += 4) {
        clear += transparentPixels[i + 3] == 0;
        visible += transparentPixels[i + 3] > 0;
        check(transparentPixels[i] <= transparentPixels[i + 3] &&
                  transparentPixels[i + 1] <= transparentPixels[i + 3] &&
                  transparentPixels[i + 2] <= transparentPixels[i + 3],
              "Transparent frame has non-premultiplied pixels");
    }
    check(clear > 280 * 136 / 2 && visible > 1000, "Background toggle lost transparency or pond content");
    Scene scene;
    scene.configure(640, 320, 9, 7);
    std::vector<uint8_t> pixels(640 * 320 * 4);
    CdFrame frame{sizeof(frame), 640, 320, 640 * 4, pixels.size(), pixels.data(), 1};
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 20; ++i) {
        scene.advance(.05, 1);
        check(renderer.render(scene, {}, frame, {}), "Benchmark render failed");
    }
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / 20;
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 2 && std::wstring_view(argv[1]) == L"--closed") {
            closedHabitat();
            return 0;
        }
        closedHabitat();
        simulation();
        finsAndLeaves();
        tailArticulation();
        edgeCrossings();
        mouseBehavior();
        sizingAndStartup();
        lifecycle();
        interaction();
        markingClipping();
        blessingWords();
        blessingCache();
        const double overlayMilliseconds = smoothRendering();
        const double milliseconds = rendering();
        std::cout << "PASS: body-clipped koi markings, fins, leaf silhouettes, flee/follow, transparent background, "
                     "articulated movement, bounds, "
                     "frame-rate independence, lifecycle, settings, theme, "
                     "32 DPI/day-night renders; 640x320 average "
                  << milliseconds << " ms/frame; smooth overlay " << overlayMilliseconds << " ms/frame\n";
        if (argc > 1) {
            const std::filesystem::path path(argv[1]);
            std::filesystem::create_directories(path.parent_path());
            std::ofstream output(path);
            output << "{\"passed\":true,\"referenceTailSilhouette\":true,\"straightTailSway\":true,"
                      "\"blessingWords\":50,\"blessingShuffleFadeAndToggle\":true,"
                      "\"bodyClippedMarkings\":true,\"markingClipCases\":12,"
                      "\"tailBendAndRotation\":true,"
                      "\"fixed2x4FishScale\":true,"
                      "\"randomStartupSchool\":true,"
                      "\"referenceFinGeometry\":true,\"completeExitAndOppositeReentry\":true,"
                      "\"finRotationAndSteering\":true,\"onlySmallestTwoLeavesNotched\":true,"
                      "\"mouseFleeFollowAndDrag\":true,\"transparentBackground\":true,"
                      "\"independentMotionSpeed\":true,\"smoothBlessingComposition\":true,\"smoothOverlayMs\":"
                   << overlayMilliseconds
                   << ",\"simulationCases\":15,\"simulatedSecondsPerCase\":120,"
                      "\"sizes\":[\"2x4\",\"4x4\",\"3x6\"],\"dpiRenders\":32,\"render640x320Ms\":"
                   << milliseconds << "}\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
