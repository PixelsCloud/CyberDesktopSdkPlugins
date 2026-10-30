#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>

#include "plugins/clock-suite/Clock.h"
#include "sdk/services/Environment.h"

namespace {
void check(bool value, const char* text) {
    if (!value) {
        std::cerr << "FAIL: " << text << '\n';
        std::exit(1);
    }
}

struct Host {
    uint64_t next = 1;
    bool fail = false;
    int redraws = 0;
    CdEnvironmentSnapshot environment{sizeof(environment), 1, 1709208000000ull, 0, 480, CD_ENV_THEME_LIGHT, "en-US"};

    static CdResult CD_CALL query(void* context, const char* id, uint32_t version, void* output, uint32_t size) {
        if (std::strcmp(id, CD_SERVICE_ENVIRONMENT) || version != 1 || size < sizeof(CdEnvironmentApi)) {
            return CD_ERROR_UNSUPPORTED;
        }
        *static_cast<CdEnvironmentApi*>(output) = {sizeof(CdEnvironmentApi), 1,
                                                   [](void* context, CdEnvironmentSnapshot* snapshot) -> CdResult {
                                                       *snapshot = static_cast<Host*>(context)->environment;
                                                       return CD_OK;
                                                   }};
        return CD_OK;
    }

    struct Timer {
        uint32_t interval;
        CdTimerCallback callback;
        void* user;
    };

    std::map<uint64_t, Timer> timers;

    static void CD_CALL redraw(void* context) {
        ++static_cast<Host*>(context)->redraws;
    }

    static CdResult CD_CALL subscribe(void* context, uint32_t interval, CdTimerCallback callback, void* user,
                                      uint64_t* token) {
        auto& host = *static_cast<Host*>(context);
        if (host.fail)
            return CD_ERROR_INTERNAL;
        *token = host.next++;
        host.timers[*token] = {interval, callback, user};
        return CD_OK;
    }

    static void CD_CALL unsubscribe(void* context, uint64_t token) {
        static_cast<Host*>(context)->timers.erase(token);
    }

    CdHostApi api{sizeof(api), CD_PLUGIN_ABI_VERSION, redraw, nullptr, nullptr, nullptr, unsubscribe, subscribe};
};

CdSettingValue integer(const char* key, uint32_t type, int64_t value) {
    return {sizeof(CdSettingValue), type, key, value, 0, nullptr};
}

CdSettingValue choice(const char* key, const char* text) {
    return {sizeof(CdSettingValue), CD_SETTING_CHOICE, key, 0, 0, text};
}
}  // namespace

int main() {
    using namespace clockplugin;
    int frames = 0;
    for (bool analog : {false, true})
        for (auto [rows, columns] : {std::pair{1, 1}, std::pair{1, 2}, std::pair{1, 4}, std::pair{2, 2}}) {
            Spec spec{"test.clock", analog, rows, columns};
            Host host;
            CdInstanceApi api{}, other{};
            CdCreateContext context{sizeof(context), 1, spec.id, "instance-1", "", &host.api, &host};
            check(create(spec, &context, &api, sizeof(api)) == CD_OK, "create independent clock");
            context.instance_id = "instance-2";
            check(create(spec, &context, &other, sizeof(other)) == CD_OK, "create second clock");
            check(host.timers.size() == 2, "one timer per instance");
            CdSettingValue original{};
            check(api.get_setting(api.instance, "format", &original, sizeof(original)) == CD_OK, "read format");
            CdSettingValue sentinel;
            std::memset(&sentinel, 0xcd, sizeof(sentinel));
            const auto before = sentinel;
            check(api.get_setting(api.instance, "format", &sentinel, sizeof(sentinel) - 1) == CD_ERROR_BUFFER &&
                      std::memcmp(&sentinel, &before, sizeof(sentinel)) == 0,
                  "short settings buffer is untouched");
            auto update = choice("format", "12");
            check(api.apply_settings(api.instance, &update, 1) == CD_OK, "apply time format");
            check(other.get_setting(other.instance, "format", &original, sizeof(original)) == CD_OK &&
                      std::strcmp(original.text, "24") == 0,
                  "settings are instance-local");
            CdSettingValue invalid[]{integer("date", CD_SETTING_TOGGLE, 0), choice("format", "invalid")};
            check(api.apply_settings(api.instance, invalid, 2) == CD_ERROR_ARGUMENT,
                  "invalid settings transaction rejected");
            check(api.get_setting(api.instance, "date", &original, sizeof(original)) == CD_OK && original.integer == 1,
                  "transaction did not partially apply");
            if (analog) {
                host.fail = true;
                update = choice("seconds_motion", "smooth");
                check(api.apply_settings(api.instance, &update, 1) == CD_ERROR_INTERNAL,
                      "timer replacement failure reported");
                host.fail = false;
                check(api.get_setting(api.instance, "seconds_motion", &original, sizeof(original)) == CD_OK &&
                          std::strcmp(original.text, "tick") == 0 && host.timers.size() == 2,
                      "timer failure rolls back config");
                check(api.apply_settings(api.instance, &update, 1) == CD_OK && host.timers.size() == 2,
                      "sweep uses one replacement timer");
            }
            api.set_paused(api.instance, 1);
            api.set_paused(api.instance, 1);
            check(host.timers.size() == 1, "paused clock unsubscribes timer");
            api.set_paused(api.instance, 0);
            api.set_paused(api.instance, 0);
            check(host.timers.size() == 2, "resume does not duplicate timer");
            for (int cell : {72, 96, 160})
                for (int height : {100, 125, 150, 200})
                    for (double dpr : {1.0, 1.5, 2.0}) {
                        const double w = cell * columns - 24.0, h = cell * rows * height / 100.0 - 24.0;
                        const uint32_t pw = uint32_t(std::lround(w * dpr)), ph = uint32_t(std::lround(h * dpr));
                        std::vector<uint8_t> pixels(size_t(pw) * ph * 4 + 64, 0xCD);
                        CdCell position{0, 0};
                        CdLayout layout{
                            sizeof(layout),     0, 0, 1, &position, w, h, dpr, {0, 0, w, h}, {43, 41, 36, 255},
                            {201, 110, 55, 255}};
                        api.layout_changed(api.instance, &layout);
                        CdFrame frame{sizeof(frame), pw, ph, pw * 4, uint64_t(pixels.size() - 64), pixels.data(), dpr};
                        check(api.render(api.instance, &frame) == CD_OK, "real clock render at supported size/DPI");
                        check(std::all_of(pixels.end() - 64, pixels.end(),
                                          [](uint8_t v) {
                                              return v == 0xCD;
                                          }),
                              "render respects buffer capacity");
                        int visible = 0, transparent = 0;
                        for (size_t i = 0; i < pixels.size() - 64; i += 4) {
                            visible += pixels[i + 3] > 0;
                            transparent += pixels[i + 3] == 0;
                            check(pixels[i] <= pixels[i + 3] && pixels[i + 1] <= pixels[i + 3] &&
                                      pixels[i + 2] <= pixels[i + 3],
                                  "RGBA is premultiplied");
                        }
                        check(visible > 12 && transparent > 10, "transparent readable clock content, no painted card");
                        const auto g = arrange(spec, w, h, options(defaults(spec), {43, 41, 36, 255}));
                        if (analog)
                            check(
                                std::abs(g.dial.width - g.dial.height) < .0001 && g.dial.width > 0 && g.dial.width <= w,
                                "analog dial remains circular within short edge");
                        if (!analog && rows == 1 && columns == 4)
                            check(!g.stacked, "wide clock uses horizontal time");
                        ++frames;
                    }
            uint64_t stateSize = 0;
            check(api.save_state(api.instance, nullptr, 0, &stateSize) == CD_OK && stateSize <= 32,
                  "bounded versioned state");
            std::vector<uint8_t> state(stateSize);
            check(api.save_state(api.instance, state.data(), state.size(), &stateSize) == CD_OK &&
                      api.restore_state(api.instance, state.data(), state.size()) == CD_OK,
                  "state roundtrip");
            state[0] ^= 1;
            check(api.restore_state(api.instance, state.data(), state.size()) == CD_ERROR_ARGUMENT,
                  "corrupt state rejected");
            std::vector<uint8_t> tiny(16, 0x7A);
            CdFrame bad{sizeof(bad), 2, 2, 8, 15, tiny.data(), 1};
            check(api.render(api.instance, &bad) == CD_ERROR_ARGUMENT && std::all_of(tiny.begin(), tiny.end(),
                                                                                     [](uint8_t v) {
                                                                                         return v == 0x7A;
                                                                                     }),
                  "short frame rejected before writing");
            api.destroy(api.instance);
            other.destroy(other.instance);
            check(host.timers.empty(), "all clock subscriptions released");
        }
    Renderer renderer;
    Spec spec{"time.boundary", false, 1, 4};
    auto opts = options(defaults(spec), {43, 41, 36, 255});
    const Spec tinySpec{"layout.small", false, 1, 1};
    auto tinyOptions = options(defaults(tinySpec), {43, 41, 36, 255});
    check(arrange(tinySpec, 104, 104, tinyOptions).seconds.height == 0 &&
              arrange(tinySpec, 104, 168, tinyOptions).seconds.height > 0,
          "one-cell seconds follow square/portrait design");
    for (double ratio : {1.0, 1.25, 1.5, 2.0}) {
        const auto digital = arrange(tinySpec, 72, 72 * ratio, tinyOptions);
        check(digital.date.height == 0 && digital.weekday.height == 0,
              "single digital clock never reserves calendar space");
        const Spec analogSpec{"layout.dial", true, 1, 1};
        const auto analog = arrange(analogSpec, 72, 72 * ratio, tinyOptions);
        check(analog.dial.width > 68 && analog.dial.width == analog.dial.height,
              "single analog dial fills width without stretching");
        check(analog.time.height == 0 && analog.date.height == 0 && analog.weekday.height == 0,
              "single analog has no text footer");
    }
    const auto enlarged = arrange(tinySpec, 72, 108, tinyOptions);
    check(enlarged.stacked && enlarged.time.height > 76, "portrait digital reserves most height for hours and minutes");
    const Spec wideAnalog{"layout.wide", true, 1, 4};
    auto wideOptions = options(defaults(wideAnalog), {43, 41, 36, 255});
    for (double ratio : {1.0, 1.25, 1.5, 2.0}) {
        const auto balanced = arrange(wideAnalog, 276, 72 * ratio - 12, wideOptions);
        check(balanced.dial.width == balanced.dial.height && balanced.time.height > (72 * ratio - 12) * .59,
              "wide analog has round dial and large time");
        check(balanced.dial.x + balanced.dial.width < balanced.time.x && balanced.time.x + balanced.time.width <= 276,
              "wide analog columns do not overlap or overflow");
        check(balanced.date.y >= balanced.time.y + balanced.time.height &&
                  balanced.date.y + balanced.date.height <= 72 * ratio - 12,
              "wide calendar stays below time within canvas");
    }
    {
        const int w = 276, h = 96;
        std::vector<uint8_t> flat(w * h * 4), raised(flat.size());
        CdFrame image{sizeof(image), w, h, w * 4, uint64_t(flat.size()), flat.data(), 1};
        wideOptions.reliefPercent = 0;
        check(renderer.render(wideAnalog, wideOptions, {2026, 10, 12, 1, 10, 8, 30, 0}, image),
              "render flat reference");
        image.rgba = raised.data();
        wideOptions.reliefPercent = 65;
        std::string error;
        const bool rendered =
            renderer.render(wideAnalog, wideOptions, {2026, 10, 12, 1, 10, 8, 30, 0}, image, {}, &error);
        if (!rendered)
            std::cerr << error << '\n';
        check(rendered && flat != raised, "relief setting changes actual rendered pixels");
        check(raised[3] == 0 && raised[(w * h - 1) * 4 + 3] == 0, "relief keeps outer canvas transparent");
    }
    std::vector<uint8_t> a(360 * 120 * 4), b(a.size());
    CdFrame first{sizeof(first), 360, 120, 1440, uint64_t(a.size()), a.data(), 1};
    auto second = first;
    second.rgba = b.data();
    check(renderer.render(spec, opts, {2026, 10, 12, 1, 10, 8, 30, 0}, first), "reference minute");
    check(renderer.render(spec, opts, {2026, 10, 12, 1, 10, 9, 31, 0}, second) && a != b,
          "time updates, not static design bitmap");
    opts.twelveHour = true;
    check(renderer.render(spec, opts, {2026, 10, 12, 1, 22, 8, 30, 0}, second) && a == b,
          "12-hour formatting matches correct hour");
    auto values = defaults(spec);
    auto utc = choice("timezone", "utc");
    check(values.apply(&utc, 1) == CD_OK, "UTC setting");
    const auto utcNow = currentTime(options(values, {43, 41, 36, 255}));
    SYSTEMTIME os{};
    GetSystemTime(&os);
    check(utcNow.year == os.wYear && utcNow.month == os.wMonth && utcNow.day == os.wDay && utcNow.hour == os.wHour,
          "real UTC source");
    {
        Host host;
        CdServiceHostApi services{};
        services.input.media.base = host.api;
        services.input.media.base.struct_size = sizeof(services);
        services.query_service = Host::query;
        Spec fixedSpec{"test.fixed.clock", false, 1, 4};
        CdCreateContext context{sizeof(context), 1, fixedSpec.id, "fixed", "", &services.input.media.base, &host};
        CdInstanceApi api{};
        check(create(fixedSpec, &context, &api, sizeof(api)) == CD_OK, "create environment-driven clock");
        CdCell cell{0, 0};
        CdLayout layout{sizeof(layout), 0, 0, 1, &cell, 360, 120, 1, {0, 0, 360, 120}, {43, 41, 36, 255}, {}};
        api.layout_changed(api.instance, &layout);
        const auto fixedOptions = options(defaults(fixedSpec), layout.text_color);
        check(api.render(api.instance, &first) == CD_OK &&
                  renderer.render(fixedSpec, fixedOptions, {2024, 2, 29, 4, 20, 0, 0, 0}, second) && a == b,
              "fixed time and local offset drive actual clock pixels");
        host.redraws = 0;
        for (int i = 0; i < 4; ++i) {
            host.environment.monotonic_ms += 1000;
            for (const auto& [id, timer] : host.timers) {
                timer.callback(timer.user, host.environment.monotonic_ms);
            }
        }
        check(host.redraws == 0, "unchanged visible time does not request frames");
        host.environment.unix_ms += 60000;
        for (const auto& [id, timer] : host.timers) {
            timer.callback(timer.user, host.environment.monotonic_ms);
        }
        check(host.redraws == 1 && api.render(api.instance, &first) == CD_OK && a != b,
              "changed time requests a local update");
        auto timezone = choice("timezone", "utc");
        check(api.apply_settings(api.instance, &timezone, 1) == CD_OK && api.render(api.instance, &first) == CD_OK &&
                  renderer.render(fixedSpec, fixedOptions, {2024, 2, 29, 4, 12, 1, 0, 0}, second) && a == b,
              "UTC preference overrides the simulated local offset");
        api.set_paused(api.instance, 1);
        host.environment.unix_ms += 60000;
        check(host.timers.empty() && api.render(api.instance, &first) == CD_OK && a == b,
              "hidden clock preserves its last time");
        api.set_paused(api.instance, 0);
        check(host.timers.size() == 1 && api.render(api.instance, &first) == CD_OK && a != b,
              "resumed clock follows current environment time");
        api.destroy(api.instance);
        check(host.timers.empty(), "owned clock timer is released before instance destruction returns");
    }
    std::cout << "Clock release: 8 plugins, " << frames << " size/DPI frames, settings/timers/state/time PASS\n";
}
