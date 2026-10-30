#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>

#include "plugins/brick-clock/Clock.h"

namespace {
using namespace brickclock;
unsigned checks = 0;

void check(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
    ++checks;
    std::cout << "PASS " << message << '\n';
}

void step(Model& model, unsigned frames) {
    for (unsigned i = 0; i < frames; ++i) {
        model.advance(1. / 60);
    }
}

struct Host {
    struct Timer {
        uint32_t interval;
        uint64_t due;
        CdTimerCallback callback;
        void* user;
    };

    std::map<uint64_t, Timer> timers;
    uint64_t token = 0, now = GetTickCount64(), redraws = 0;
    CdEnvironmentSnapshot environment{
        sizeof(CdEnvironmentSnapshot), 1, 1760000000000ull, now, 480, CD_ENV_THEME_LIGHT, "zh-CN"};
    CdServiceHostApi api{};

    Host() {
        auto& base = api.input.media.base;
        base.struct_size = sizeof(api);
        base.abi_version = 1;
        base.request_redraw = [](void* host) {
            ++static_cast<Host*>(host)->redraws;
        };
        base.subscribe_timer = [](void* host, uint32_t interval, CdTimerCallback callback, void* user,
                                  uint64_t* output) -> CdResult {
            auto& self = *static_cast<Host*>(host);
            *output = ++self.token;
            self.timers.emplace(*output, Timer{interval, self.now + interval, callback, user});
            return CD_OK;
        };
        base.unsubscribe = [](void* host, uint64_t value) {
            static_cast<Host*>(host)->timers.erase(value);
        };
        api.query_service = [](void*, const char* name, uint32_t version, void* output, uint32_t size) -> CdResult {
            if (std::string_view(name) != CD_SERVICE_ENVIRONMENT || version != 1 || size < sizeof(CdEnvironmentApi)) {
                return CD_ERROR_UNSUPPORTED;
            }
            *static_cast<CdEnvironmentApi*>(output) = {sizeof(CdEnvironmentApi), 1,
                                                       [](void* host, CdEnvironmentSnapshot* value) -> CdResult {
                                                           *value = static_cast<Host*>(host)->environment;
                                                           return CD_OK;
                                                       }};
            return CD_OK;
        };
    }

    void advance(uint32_t milliseconds) {
        now += milliseconds;
        environment.monotonic_ms = now;
        environment.unix_ms += milliseconds;
        const auto copy = timers;
        for (const auto& [id, timer] : copy) {
            if (timers.contains(id) && now >= timer.due) {
                timers.at(id).due = now + timer.interval;
                timer.callback(timer.user, now);
            }
        }
    }

    CdCreateContext context() {
        return {sizeof(CdCreateContext), 1,   "com.cyberdesktop.clock.brick.1x4", "test", "",
                &api.input.media.base,   this};
    }
};

void modelTests() {
    for (int digit = 0; digit < 10; ++digit) {
        unsigned area = 0;
        for (int y = 0; y < 100; ++y) {
            for (int x = 0; x < 60; ++x) {
                area += inGlyph(digit, {x + .5, y + .5});
            }
        }
        check(area > 1000 && area < 6000, "digit contours have a bounded nonempty carved area");
    }
    check(!inGlyph(0, {30, 50}) && !inGlyph(8, {30, 30}) && !inGlyph(6, {30, 70}),
          "glyph counters remain intact brick rather than filled holes");
    Model model;
    check(model.setTime(10, 28, 45) && !model.active() && model.chips().empty(),
          "initial time is stable without debris");
    check(!model.setTime(10, 28, 45), "same second does not restart carving");
    model.setTime(10, 28, 46);
    check(model.carvings()[5].elapsed == 0 && model.carvings()[5].previous == 5,
          "changed seconds digit retains its old shape for resurfacing");
    check(std::all_of(model.carvings().begin(), model.carvings().begin() + 5,
                      [](const auto& value) {
                          return value.elapsed == 1;
                      }),
          "unchanged digits do not animate");
    step(model, 3);
    check(model.chips().empty(), "resurfacing precedes debris emission");
    step(model, 18);
    check(model.chips().size() == 12, "one changed digit emits the configured number of chips");
    bool sources = true, gravity = false;
    for (const auto& chip : model.chips()) {
        const auto origin = digitOrigin(chip.digit);
        sources &= chip.digit == 5 && inGlyph(model.digits()[chip.digit], {(chip.source.x - origin.x) / kGlyphScale,
                                                                           (chip.source.y - origin.y) / kGlyphScale});
        gravity |= model.state(chip).velocity.y > .1;
    }
    check(sources, "every chip originates inside an excavated stroke rather than a counter");
    check(gravity, "Box2D gravity accelerates emitted fragments downward");
    step(model, 90);
    bool bounded = true;
    for (const auto& chip : model.chips()) {
        const auto state = model.state(chip);
        bounded &= std::isfinite(state.angle) && std::isfinite(state.position.x) && state.position.x > -.02 &&
                   state.position.x < 10.02 && state.position.y > -.02 && state.position.y < kFloor / 100 + .02;
    }
    check(bounded && model.contacts() > 0, "fragments collide and remain bounded above the ledge");
    step(model, 180);
    check(!model.active() && model.chips().empty() && model.stats().bodies == 1 && model.stats().shapes == 4,
          "expired fragments release all Box2D bodies and shapes");
    model.setTime(23, 59, 59);
    model.setTime(0, 0, 0);
    check(std::all_of(model.digits().begin(), model.digits().end(),
                      [](int value) {
                          return value == 0;
                      }) &&
              std::all_of(model.carvings().begin(), model.carvings().end(),
                          [](const auto& value) {
                              return value.elapsed == 0;
                          }),
          "midnight updates and animates all six changed digits");
    step(model, 25);
    model.setTime(12, 30, 15);
    check(!model.active() && model.chips().empty() && model.digits()[0] == 1 && model.digits()[1] == 2,
          "system time jumps synchronize silently without replaying missed carving");
    model.setTime(12, 30, 16, false);
    check(!model.active(), "motion disabled still advances the displayed time");
    model.replay();
    check(model.active(), "double-click replay can carve the current seconds digit");
    model.settle();
    model.advance(std::numeric_limits<double>::quiet_NaN());
    model.advance(-1);
    check(!model.active() && model.stats().bodies == 1, "invalid deltas and repeated settling are harmless");

    Model budget(12);
    budget.setChipCount(20);
    budget.setTime(0, 0, 0);
    size_t peak = 0;
    // Ten minutes of simulated seconds, with repeated six-digit preview bursts.
    for (int second = 1; second <= 600; ++second) {
        if (second % 20 == 0) {
            budget.setTime(23, 59, 59);
            budget.setTime(0, 0, 0);
        } else {
            budget.setTime(0, second / 60, second % 60);
            budget.replay();
        }
        for (int frame = 0; frame < 60; ++frame) {
            budget.advance(1. / 60);
            peak = std::max(peak, budget.chips().size());
            if (budget.chips().size() > kMaximumChips || budget.stats().bodies > kMaximumChips + 1) {
                throw std::runtime_error("physics budget exceeded");
            }
        }
    }
    check(peak > 50 && peak <= kMaximumChips, "ten-minute repeated rollovers stay within the fragment budget");
    step(budget, 300);
    check(budget.stats().bodies == 1 && !budget.active(), "long-run debris fully settles and releases resources");
}

void lifecycleTests() {
    Host host;
    {
        Instance instance(host.context());
        check(host.timers.size() == 1 && host.timers.begin()->second.interval == 1000,
              "stable clock initially polls the next second with one timer");
        constexpr uint32_t width = 400, height = 110, stride = width * 4 + 16;
        std::vector<uint8_t> bytes(size_t(stride) * height, 0);
        CdFrame frame{sizeof(CdFrame), width, height, stride, bytes.size(), bytes.data(), 1};
        CdLayout layout{};
        layout.struct_size = sizeof(layout);
        layout.logical_width = width;
        layout.logical_height = height;
        layout.safe_content = {5, 5, width - 10., height - 10.};
        instance.layoutChanged(layout);
        check(instance.render(frame) == CD_OK, "plugin renders within a padded-stride safe-content frame");
        const auto initial = bytes;
        const auto redraws = host.redraws;
        for (int i = 0; i < 5; ++i) {
            instance.render(frame);
        }
        check(bytes == initial && host.redraws == redraws, "painting is deterministic and does not advance simulation");
        check(bytes[3] == 0 && bytes[(height - 1) * stride + 3] == 0,
              "drawing respects transparent safe-content margins");
        host.advance(1000);
        check(host.timers.size() == 1 && host.timers.begin()->second.interval == 33,
              "a changed second switches the same subscription to animation cadence");
        for (int i = 0; i < 12; ++i) {
            host.advance(33);
            instance.render(frame);
        }
        check(bytes != initial, "host updates reveal carved digits and physical fragments");
        host.advance(86400000);
        check(host.timers.begin()->second.interval > 33,
              "a full-day time jump clears stale motion even when the clock digits repeat");
        instance.setPaused(true);
        check(host.timers.empty(), "hidden plugin cancels its timer");
        const auto beforeHidden = host.redraws;
        host.advance(3600000);
        check(host.redraws == beforeHidden, "hidden elapsed time generates no redraw work");
        instance.setPaused(false);
        check(host.timers.size() == 1 && host.timers.begin()->second.interval > 33,
              "resume synchronizes current wall time without a physics catch-up burst");
        CdSettingValue reduced{sizeof(CdSettingValue), CD_SETTING_TOGGLE, "reduced_motion", 1};
        check(instance.applySettings(&reduced, 1) == CD_OK, "reduced-motion setting is accepted");
        host.advance(1000);
        check(host.timers.begin()->second.interval > 33, "reduced motion continues time updates without animation");
        instance.render(frame);
        const auto beforePalette = bytes;
        CdSettingValue color{sizeof(CdSettingValue), CD_SETTING_CHOICE, "palette", 0, 0, "slate"};
        check(instance.applySettings(&color, 1) == CD_OK && instance.render(frame) == CD_OK && bytes != beforePalette,
              "palette changes invalidate the cached wall immediately");
        CdSettingValue invalid[]{{sizeof(CdSettingValue), CD_SETTING_CHOICE, "palette", 0, 0, "sand"},
                                 {sizeof(CdSettingValue), CD_SETTING_INTEGER, "debris_count", 999}};
        check(
            instance.applySettings(invalid, 2) == CD_ERROR_ARGUMENT && instance.settings.at("palette").text == "slate",
            "invalid multi-setting updates are rejected transactionally");
        auto invalidFrame = frame;
        invalidFrame.rgba = nullptr;
        check(instance.render(invalidFrame) == CD_ERROR_ARGUMENT, "invalid output buffers are rejected");
        layout.logical_width = std::numeric_limits<double>::quiet_NaN();
        instance.layoutChanged(layout);
        check(instance.render(frame) == CD_OK, "malformed layout notifications preserve the last usable layout");
    }
    check(host.timers.empty(), "destroyed instance releases its final host subscription");
}

void abiTest(const wchar_t* filename) {
    const auto library = LoadLibraryW(filename);
    check(library != nullptr, "packaged plugin DLL loads");
    const auto query = reinterpret_cast<CdResult(CD_CALL*)(uint32_t, CdPluginApi*, uint32_t)>(
        GetProcAddress(library, "cd_plugin_query"));
    check(query != nullptr, "plugin exports the C ABI entry point");
    CdPluginApi api{};
    check(query(99, &api, sizeof(api)) == CD_ERROR_VERSION && query(1, &api, sizeof(api)) == CD_OK &&
              std::string_view(api.id) == "com.cyberdesktop.clock.brick.1x4",
          "plugin advertises the correct identity and ABI version");
    Host host;
    auto context = host.context();
    CdInstanceApi instance{};
    check(api.create(&context, &instance, sizeof(instance)) == CD_OK,
          "host creates an instance through the exported ABI");
    instance.destroy(instance.instance);
    check(host.timers.empty(), "C ABI destruction releases instance timers");
    FreeLibrary(library);
}
}  // namespace

int wmain(int argc, wchar_t** argv) try {
    modelTests();
    lifecycleTests();
    if (argc > 1) {
        abiTest(argv[1]);
    }
    std::cout << checks << " checks passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
}
