#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <map>

#include "Monitor.h"

namespace {
void check(bool ok, const char* label) {
    if (!ok) {
        std::cerr << "FAIL: " << label << '\n';
        std::exit(1);
    }
}

struct Host {
    uint64_t next = 1;
    bool fail = false;
    int redraws = 0;

    struct Subscription {
        CdSystemCallback data = nullptr;
        CdTimerCallback timer = nullptr;
        void* user = nullptr;
        uint32_t interval = 0;
    };

    std::map<uint64_t, Subscription> subscriptions;

    static void CD_CALL redraw(void* pointer) {
        ++static_cast<Host*>(pointer)->redraws;
    }

    static CdResult CD_CALL system(void* pointer, uint32_t interval, CdSystemCallback callback, void* user,
                                   uint64_t* token) {
        auto& self = *static_cast<Host*>(pointer);
        if (self.fail)
            return CD_ERROR_INTERNAL;
        *token = self.next++;
        self.subscriptions[*token] = {callback, nullptr, user, interval};
        return CD_OK;
    }

    static CdResult CD_CALL timer(void* pointer, uint32_t interval, CdTimerCallback callback, void* user,
                                  uint64_t* token) {
        auto& self = *static_cast<Host*>(pointer);
        *token = self.next++;
        self.subscriptions[*token] = {nullptr, callback, user, interval};
        return CD_OK;
    }

    static void CD_CALL stop(void* pointer, uint64_t token) {
        static_cast<Host*>(pointer)->subscriptions.erase(token);
    }

    CdHostApi api{sizeof(api), 1, redraw, nullptr, system, nullptr, stop, timer};

    void send(const CdSystemInfo& value) {
        const auto saved = subscriptions;
        for (const auto& [token, subscription] : saved)
            if (subscription.data && subscriptions.count(token))
                subscription.data(subscription.user, &value);
    }
};

CdSettingValue choice(const char* id, const char* value) {
    return {sizeof(CdSettingValue), CD_SETTING_CHOICE, id, 0, 0, value};
}
}  // namespace

int main() {
    using namespace monitorplugin;
    int frames = 0;
    CdSystemInfoEx sample{{sizeof(sample), 23, 3, 75, 100, 0, 0},
                          CD_SYSTEM_CPU_VALID | CD_SYSTEM_MEMORY_VALID | CD_SYSTEM_GPU_VALID,
                          0,
                          0,
                          GetTickCount64()};
    auto decoded = decode(sample.base);
    check(decoded.cpu == 23 && decoded.ram == 75 && decoded.gpu == 3 && decoded.temperature == 0 &&
              !(decoded.valid & CD_SYSTEM_CPU_TEMPERATURE_VALID),
          "missing CPU temperature does not become GPU temperature");
    sample.cpu_temperature_celsius = 62.5;
    sample.valid_fields |= CD_SYSTEM_CPU_TEMPERATURE_VALID;
    check(decode(sample.base).temperature == 62.5, "real CPU temperature extension");
    CdSystemInfo base{sizeof(base), 23, 3, 75, 100, 0, 0};
    check(decode(base).cpu == 23 && decode(base).temperature == 0, "original ABI prefix remains readable");
    for (auto [rows, columns] : {std::pair{1, 1}, std::pair{1, 2}, std::pair{1, 4}, std::pair{2, 2}}) {
        Spec spec{"test.monitor", rows, columns};
        Host host;
        CdInstanceApi api{}, second{};
        CdCreateContext context{sizeof(context), 1, spec.id, "one", "", &host.api, &host};
        check(create(spec, &context, &api, sizeof(api)) == CD_OK, "create monitor");
        context.instance_id = "two";
        check(create(spec, &context, &second, sizeof(second)) == CD_OK && host.subscriptions.size() == 4,
              "two independent scoped subscriptions");
        auto update = choice("palette", "light");
        check(api.apply_settings(api.instance, &update, 1) == CD_OK, "live palette");
        CdSettingValue value{};
        check(second.get_setting(second.instance, "palette", &value, sizeof(value)) == CD_OK &&
                  std::strcmp(value.text, "theme") == 0,
              "settings isolation");
        const CdSettingValue invalid[]{choice("palette", "dark"), choice("primary", "bad")};
        check(api.apply_settings(api.instance, invalid, 2) == CD_ERROR_ARGUMENT, "atomic invalid settings");
        check(api.get_setting(api.instance, "palette", &value, sizeof(value)) == CD_OK &&
                  std::strcmp(value.text, "light") == 0,
              "atomic rollback");
        CdSettingValue interval{sizeof(interval), CD_SETTING_INTEGER, "interval", 500};
        host.fail = true;
        check(api.apply_settings(api.instance, &interval, 1) == CD_ERROR_INTERNAL && host.subscriptions.size() == 4,
              "subscription failure does not leak");
        host.fail = false;
        check(api.get_setting(api.instance, "interval", &value, sizeof(value)) == CD_OK && value.integer == 1000,
              "failed retune rolls back");
        check(api.apply_settings(api.instance, &interval, 1) == CD_OK && host.subscriptions.size() == 4,
              "one replacement system subscription");
        const int beforeData = host.redraws;
        host.send(sample.base);
        check(host.redraws == beforeData + 2, "new readings repaint each subscribed instance");
        const int afterData = host.redraws;
        host.send(sample.base);
        check(host.redraws == afterData, "unchanged readings do not repaint idle instances");
        for (int unit : {72, 96})
            for (int ratio : {100, 150})
                for (double dpr : {1.0, 2.0}) {
                    const double width = unit * columns - 24.0, height = unit * rows * ratio / 100.0 - 24.0;
                    const uint32_t w = uint32_t(width * dpr), h = uint32_t(height * dpr);
                    std::vector<uint8_t> buffer(size_t(w) * h * 4 + 32, 0xCD);
                    CdLayout layout{sizeof(layout),
                                    0,
                                    0,
                                    0,
                                    nullptr,
                                    width,
                                    height,
                                    dpr,
                                    {0, 0, width, height},
                                    {227, 241, 245, 255},
                                    {83, 214, 223, 255}};
                    api.layout_changed(api.instance, &layout);
                    CdFrame frame{sizeof(frame), w, h, w * 4, uint64_t(buffer.size() - 32), buffer.data(), dpr};
                    check(api.render(api.instance, &frame) == CD_OK, "native rendering at real small grid / DPI");
                    check(std::all_of(buffer.end() - 32, buffer.end(),
                                      [](auto value) {
                                          return value == 0xCD;
                                      }),
                          "render buffer boundary");
                    int visible = 0, empty = 0;
                    for (size_t i = 0; i < buffer.size() - 32; i += 4) {
                        visible += buffer[i + 3] > 0;
                        empty += buffer[i + 3] == 0;
                        check(buffer[i] <= buffer[i + 3] && buffer[i + 1] <= buffer[i + 3] &&
                                  buffer[i + 2] <= buffer[i + 3],
                              "premultiplied transparent content");
                    }
                    check(visible > 50 && empty > 50, "actual glyphs and segment graphics, no panel");
                    const auto positions = arrange(spec, width, height, {});
                    check(positions.size() == size_t(rows == 2 ? 4 : columns), "metric count matches design");
                    for (auto p : positions)
                        check(p.scale == 1 && p.x >= 0 && p.y >= 0 && p.x + 48 <= width + .001 &&
                                  p.y + 48 <= height + .001,
                              "same 48px gauge across supported footprints");
                    ++frames;
                }
        api.set_paused(api.instance, 1);
        api.set_paused(api.instance, 1);
        check(host.subscriptions.size() == 2, "pause stops both services");
        api.set_paused(api.instance, 0);
        api.set_paused(api.instance, 0);
        check(host.subscriptions.size() == 4, "resume has no duplicate services");
        const auto mechanical = choice("appearance", "mechanical");
        check(api.apply_settings(api.instance, &mechanical, 1) == CD_ERROR_ARGUMENT,
              "classic plugin cannot switch appearance");
        api.destroy(api.instance);
        auto mechanicalSpec = spec;
        mechanicalSpec.mechanical = true;
        check(create(mechanicalSpec, &context, &api, sizeof(api)) == CD_OK, "create fixed mechanical plugin");
        host.send(sample.base);
        sample.base.cpu_percent += 10;
        host.send(sample.base);
        BOOL animations = TRUE;
        SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
        check(host.subscriptions.size() == (animations ? 5u : 4u),
              "changed mechanical readings schedule bounded motion");
        if (animations) {
            Sleep(620);
            const auto callbacks = host.subscriptions;
            for (const auto& [token, subscription] : callbacks) {
                if (subscription.timer && subscription.interval == 33 && host.subscriptions.count(token)) {
                    subscription.timer(subscription.user, GetTickCount64());
                }
            }
            check(host.subscriptions.size() == 4, "settled needles release the animation timer");
        }
        const CdSettingValue reduced{sizeof(reduced), CD_SETTING_TOGGLE, "reduced_motion", 1};
        check(api.apply_settings(api.instance, &reduced, 1) == CD_OK, "reduced motion setting applies");
        sample.base.cpu_percent += 1;
        host.send(sample.base);
        check(host.subscriptions.size() == 4, "reduced motion keeps exact readings without an animation subscription");
        check(second.get_setting(second.instance, "appearance", &value, sizeof(value)) == CD_ERROR_ARGUMENT,
              "classic plugin has no appearance setting");
        uint64_t size = 0;
        check(api.save_state(api.instance, nullptr, 0, &size) == CD_OK && size == 8, "small versioned state");
        std::vector<uint8_t> state(size);
        check(api.save_state(api.instance, state.data(), size, &size) == CD_OK &&
                  api.restore_state(api.instance, state.data(), size) == CD_OK,
              "state lifecycle");
        state[0] ^= 1;
        check(api.restore_state(api.instance, state.data(), size) == CD_ERROR_ARGUMENT, "corrupt state rejected");
        api.destroy(api.instance);
        second.destroy(second.instance);
        check(host.subscriptions.empty(), "destroy cancels all callbacks");
    }
    Renderer renderer;
    Spec spec{"render.logic", 1, 1};
    Options options;
    options.palette = "dark";
    check(arrange(spec, 46, 46, options).front().scale == 1,
          "screen-fitted minimum grid preserves the common 14px font");
    std::vector<uint8_t> a(48 * 48 * 4), b(a.size());
    CdFrame frame{sizeof(frame), 48, 48, 192, uint64_t(a.size()), a.data(), 1};
    Values values{23, 75, 3, 0, 7};
    check(renderer.render(spec, options, values, frame), "reference rendering");
    auto changed = frame;
    changed.rgba = b.data();
    values.cpu = 75;
    check(renderer.render(spec, options, values, changed) && a != b, "real metrics change segment / number output");
    options.primary = "temperature";
    values.valid = 0;
    check(renderer.render(spec, options, values, frame), "0 degree CPU placeholder");
    values.valid = CD_SYSTEM_CPU_TEMPERATURE_VALID;
    check(renderer.render(spec, options, values, changed) && a == b,
          "placeholder matches explicit zero, not stale GPU value");
    std::cout << "System Info: 4 plugins, " << frames
              << " content frames, live data / temperature / subscriptions / "
                 "settings PASS\n";
}
