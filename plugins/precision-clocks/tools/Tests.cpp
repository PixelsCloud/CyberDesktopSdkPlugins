#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <thread>

#include "Clock.h"
#include "plugins/precision-clocks/tools/TestHost.h"

namespace {
using namespace precisionclock;
unsigned checks = 0;
bool passed = true;

void check(bool value, const char* message) {
    ++checks;
    passed &= value;
    std::cout << (value ? "PASS " : "FAIL ") << message << '\n';
}

uint64_t utc(int year, unsigned month, unsigned day, int hour = 0, int minute = 0, int second = 0, int ms = 0) {
    using namespace std::chrono;
    return uint64_t(
        duration_cast<milliseconds>(sys_days{std::chrono::year{year} / month / day}.time_since_epoch()).count() +
        ((hour * 60ll + minute) * 60 + second) * 1000 + ms);
}

CdEnvironmentSnapshot environment(uint64_t ms, int offset = 0) {
    return {sizeof(CdEnvironmentSnapshot), 1, ms, 1000, offset, CD_ENV_THEME_DARK, "zh-CN"};
}

void timeTests() {
    Options value;
    Model model(Kind::Green);
    model.synchronize(environment(utc(2026, 10, 20, 10, 10, 36, 249)), value);
    check(std::abs(model.state().second - 216.75) < 1e-9, "eight-beat mode quantizes seconds to eighths");
    value.motion = Motion::Sweep;
    model.synchronize(environment(utc(2026, 10, 20, 10, 10, 36, 249)), value);
    check(std::abs(model.state().second - 217.494) < 1e-9, "sweep hands derive their position from milliseconds");
    value.motion = Motion::Tick;
    model.synchronize(environment(utc(2026, 10, 20, 10, 10, 36, 999)), value);
    check(model.state().second == 216, "tick mode stays at the current whole second");
    model.synchronize(environment(utc(2024, 2, 28, 23, 59, 59)), value);
    model.synchronize(environment(utc(2024, 2, 29)), value);
    check(model.state().day == 29 && model.state().hour == 0 && model.state().minute == 0,
          "leap-day midnight resets hands and updates the date");
    model.synchronize(environment(utc(2024, 2, 29, 23, 59, 59)), value);
    model.synchronize(environment(utc(2024, 3, 1)), value);
    check(model.state().day == 1 && model.state().month == 3, "leap month end advances to March first");
    model.synchronize(environment(utc(2026, 12, 31, 23, 59, 59)), value);
    model.synchronize(environment(utc(2027, 1, 1)), value);
    check(model.state().year == 2027 && model.state().day == 1 && model.state().second == 0,
          "year rollover never interpolates through an obsolete day");
    model.synchronize(environment(utc(2026, 10, 20, 18), 480), value);
    check(model.state().day == 21 && model.state().hour == 60, "host time zone applies to both hands and date");
    model.synchronize(environment(utc(2026, 10, 20, 18), -840), value);
    check(model.state().day == 20 && model.state().hour == 120, "changing the host UTC offset applies immediately");
    model.synchronize(environment(utc(2025, 1, 1, 3, 20)), value);
    check(model.state().year == 2025 && std::abs(model.state().minute - 120) < 1e-9,
          "backward system time jumps reposition directly without catch-up animation");
    const auto state = model.state();
    check(!model.synchronize(environment(utc(2025, 1, 1, 3, 20)), value) && state == model.state(),
          "unchanged wall time does not request a new frame");
    Model skeleton(Kind::Skeleton);
    value.reduced = true;
    skeleton.synchronize(environment(utc(2026, 10, 20, 10, 10, 36, 430)), value);
    check(skeleton.state().mechanismSeconds == 0 && skeleton.state().balance == 0 && skeleton.state().second == 216,
          "reduced motion freezes decorative movement while the clock keeps time");
    value.reduced = false;
    value.motion = Motion::Beat;
    check(cadence(Kind::Green, value, 1001, false) == 124 && cadence(Kind::Skeleton, value, 1001, false) == 33,
          "ordinary beat clocks use aligned 125ms wakes; active mechanical movement uses 33ms");
    value.animation = false;
    check(cadence(Kind::Skeleton, value, 1001, false) == 999 && cadence(Kind::Green, value, 1001, true) == 33,
          "disabled motion uses one-second wakes except while background assets load");
    check(cadence(Kind::Dress, value, 1999, false) == 16 && cadence(Kind::Green, Options{}, 1124, false) == 16,
          "wakes just before second and beat boundaries respect the SDK 16ms minimum");
    bool validIntervals = true;
    for (const auto kind : {Kind::Skeleton, Kind::Green, Kind::Dress}) {
        for (const auto motion : {Motion::Sweep, Motion::Beat, Motion::Tick}) {
            for (unsigned flags = 0; flags < 32; ++flags) {
                Options timing;
                timing.motion = motion;
                timing.animation = (flags & 1) != 0;
                timing.reduced = (flags & 2) != 0;
                timing.seconds = (flags & 4) != 0;
                timing.detail = (flags & 8) != 0;
                for (uint64_t ms = 0; ms < 1000; ++ms) {
                    const auto interval = cadence(kind, timing, ms, (flags & 16) != 0);
                    validIntervals &= interval >= 16 && interval <= 1000;
                }
            }
        }
    }
    check(validIntervals, "288000 timing combinations remain inside the SDK timer contract");
    check(std::abs(std::remainder(gearAngle(10, 26, true) + 2 * gearAngle(10, 52, false), 360.)) < 1e-9 &&
              gearAngle(1, 26, true) < 0,
          "intermeshing wheel directions and tooth-count ratios agree");
    bool stable = true;
    for (int i = 0; i < 20000; ++i) {
        for (unsigned teeth : {14u, 20u, 24u, 34u, 44u, 52u}) {
            const auto angle = gearAngle(30000000000. + i * .031, teeth, i % 2 != 0);
            stable &= std::isfinite(angle) && std::abs(angle) <= 360;
        }
    }
    check(stable, "120000 long-epoch gear samples retain bounded finite angles");
    check(std::abs(gearAngle(1, 52, false) - 18) < 1e-9,
          "the display drive advances eighteen degrees per second so small gears visibly rotate");
    bool smoothBalance = true;
    double previousBalance = 0, excursion = 0;
    Model balance(Kind::Skeleton);
    Options balanceOptions;
    for (int i = 0; i < 180; ++i) {
        balance.synchronize(environment(utc(2026, 10, 20) + uint64_t(i) * 33), balanceOptions);
        const auto angle = balance.state().balance;
        smoothBalance &= std::abs(angle) <= 10 && (!i || std::abs(angle - previousBalance) < 2.7);
        excursion = std::max(excursion, std::abs(angle));
        previousBalance = angle;
    }
    check(smoothBalance && excursion > 9.8, "the fixed-axis balance remains smooth and restrained at thirty FPS");
    Model one(Kind::Skeleton), two(Kind::Skeleton);
    value.animation = true;
    one.synchronize(environment(utc(2026, 10, 20, 10, 10, 36, 210)), value);
    two.synchronize(environment(utc(2026, 10, 20, 10, 10, 36, 210)), value);
    check(one.state() == two.state(), "mechanical phase is independent of frame count and refresh rate");
}

void moonTests() {
    // HKO Almanac 2026, October moon phases. Published times are Hong Kong (UTC+8).
    // https://www.hko.gov.hk/en/gts/astron2026/files/HKO_almanac_2026.pdf
    const auto closePhase = [](double a, double b) {
        const double difference = std::abs(a - b);
        return std::min(difference, 1 - difference) < .0003;
    };
    check(closePhase(moonPhase(utc(2026, 10, 10, 15, 50)), 0), "HKO 2026-10-10 15:50 UTC new moon fixture");
    check(closePhase(moonPhase(utc(2026, 10, 18, 16, 13)), .25), "HKO 2026-10-18 16:13 UTC first-quarter fixture");
    check(closePhase(moonPhase(utc(2026, 10, 26, 4, 12)), .5), "HKO 2026-10-26 04:12 UTC full moon fixture");
    check(closePhase(moonPhase(utc(2026, 10, 3, 13, 25)), .75), "HKO 2026-10-03 13:25 UTC last-quarter fixture");
    Model model(Kind::Dress);
    Options value;
    const auto stamp = utc(2026, 10, 20, 10);
    model.synchronize(environment(stamp, 480), value);
    const auto phase = model.state().lunarPhase;
    model.synchronize(environment(stamp, -840), value);
    check(model.state().lunarPhase == phase, "moon phase depends on UTC and is invariant under time-zone changes");
    bool bounded = true;
    for (int day = 0; day < 366; ++day) {
        const double currentPhase = moonPhase(utc(2026, 1, 1) + uint64_t(day) * 86400000);
        bounded &= std::isfinite(currentPhase) && currentPhase >= 0 && currentPhase < 1;
    }
    check(bounded, "an entire year of moon phase values remains normalized");
}

void lifecycleTests() {
    for (auto kind : {Kind::Skeleton, Kind::Green, Kind::Dress}) {
        Host host(kind);
        {
            Instance instance(host.context(kind));
            std::vector<uint8_t> bytes(400 * 400 * 4);
            const CdFrame frame{sizeof(CdFrame), 400, 400, 1600, bytes.size(), bytes.data(), 2};
            check(instance.render(frame) == CD_OK, "native instance renders its two-by-two frame");
            const CdSettingValue stop{sizeof(CdSettingValue), CD_SETTING_TOGGLE, "animation", 0};
            check(instance.applySettings(&stop, 1) == CD_OK, "animation settings can be applied independently");
            for (int i = 0; i < 100; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(4));
                host.advance(33);
                instance.render(frame);
            }
            check(host.timers.size() == 1 && host.timers.begin()->second.interval > 33,
                  "quiet clocks keep one low-frequency wake without redundant frame timers");
            const auto image = bytes;
            instance.render(frame);
            check(image == bytes, "rendering twice does not advance hands or mechanical time");
            instance.setPaused(true);
            const auto redraws = host.redraws;
            host.advance(25 * 3600000ull);
            check(host.timers.empty() && host.redraws == redraws,
                  "hidden instances have no callbacks or redraw requests");
            instance.setPaused(false);
            instance.render(frame);
            check(host.timers.size() == 1 && bytes != image,
                  "resume jumps directly to the current system date and time");
            const CdSettingValue start{sizeof(CdSettingValue), CD_SETTING_TOGGLE, "animation", 1};
            host.allowTimers = false;
            check(instance.applySettings(&start, 1) != CD_OK && !instance.settings.at("animation").integer,
                  "failed subscription preserves previously accepted settings and timer");
            host.allowTimers = true;
            const CdSettingValue invalid{sizeof(CdSettingValue), CD_SETTING_CHOICE, "motion", 0, 0, "invalid"};
            check(instance.applySettings(&invalid, 1) == CD_ERROR_ARGUMENT &&
                      instance.settings.at("motion").text == "beat",
                  "invalid enum input is rejected atomically");
            const CdLayout bad{sizeof(CdLayout), 0, 0, 0, nullptr, std::numeric_limits<double>::quiet_NaN(), 0};
            instance.layoutChanged(bad);
            check(instance.render(frame) == CD_OK, "invalid layout metadata cannot poison rendering geometry");
        }
        check(host.timers.empty(), "instance destruction releases its last timer");
    }
    Host shared(Kind::Green);
    {
        Instance first(shared.context(Kind::Green)), second(shared.context(Kind::Green));
        first.setPaused(true);
        const CdSettingValue hide{sizeof(CdSettingValue), CD_SETTING_TOGGLE, "seconds", 0};
        first.applySettings(&hide, 1);
        check(
            shared.timers.size() == 1 && second.settings.at("seconds").integer && !first.settings.at("seconds").integer,
            "pausing or editing one instance never changes another instance's timer or settings");
        std::vector<uint8_t> pixels(400 * 400 * 4);
        const CdFrame frame{sizeof(CdFrame), 400, 400, 1600, pixels.size(), pixels.data(), 2};
        CdLayout layout{};
        layout.struct_size = sizeof(CdLayout);
        layout.logical_width = layout.logical_height = 200;
        layout.safe_content = {20, 30, 160, 100};
        second.layoutChanged(layout);
        second.render(frame);
        bool contained = true;
        size_t visible = 0;
        for (unsigned y = 0; y < 400; ++y) {
            for (unsigned x = 0; x < 400; ++x) {
                if (pixels[(y * 400 + x) * 4 + 3]) {
                    ++visible;
                    contained &= x >= 40 && x < 360 && y >= 60 && y < 260;
                }
            }
        }
        check(visible > 10000 && contained,
              "freestanding watch stays inside the offset safe area with transparent margins");
    }
    check(shared.timers.empty(), "destroying shared-host instances releases both independent subscriptions");
}

void renderingTests() {
    bool guards = true, corners = true, readable = true;
    for (auto kind : {Kind::Skeleton, Kind::Green, Kind::Dress}) {
        cdsdk::Canvas canvas;
        Renderer renderer(kind, std::filesystem::path(CD_PRECISION_ROOT) / "assets" / specification(kind).folder);
        for (int i = 0; renderer.pending() && i < 500; ++i) {
            renderer.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        }
        renderer.poll();
        check(renderer.assets().failed == 0 && renderer.assets().ready >= 1 &&
                  renderer.assets().referencedBytes < 10 * 1024 * 1024,
              "production material decodes asynchronously within the bounded image budget");
        for (double zoom : {.6, 1., 1.6}) {
            for (double dpi : {1., 1.5, 2.}) {
                for (bool background : {false, true}) {
                    const auto edge = uint32_t(200 * zoom * dpi);
                    std::vector<uint8_t> bytes(size_t(edge) * edge * 4 + 32, 0xa5);
                    const CdFrame frame{sizeof(CdFrame), edge, edge, edge * 4, bytes.size() - 32, bytes.data(), dpi};
                    Model model(kind);
                    Options value;
                    value.background = background;
                    for (int pose = 0; pose < 3; ++pose) {
                        model.synchronize(environment(utc(2026, 10, 20, 10, 10, 36, pose * 170)), value);
                        canvas.render(frame, {}, [&](auto& c, auto) {
                            c.scale(zoom / 5, zoom / 5);
                            renderer.draw(c, model.state(), value, zoom / 5);
                        });
                        guards &= std::all_of(bytes.end() - 32, bytes.end(), [](auto byte) {
                            return byte == 0xa5;
                        });
                        corners &= bytes[3] == 0 && bytes[(edge - 1) * 4 + 3] == 0;
                        size_t opaque = 0;
                        for (size_t i = 3; i < frame.capacity; i += 4) {
                            opaque += bytes[i] > 0;
                        }
                        readable &= opaque > edge * edge * .35 && opaque < edge * edge * .85;
                    }
                }
            }
        }
        if (kind == Kind::Dress) {
            std::array<unsigned, 4> illuminated{};
            for (size_t quarter = 0; quarter < illuminated.size(); ++quarter) {
                State state;
                state.lunarPhase = quarter / 4.;
                Options value;
                std::vector<uint8_t> bytes(400 * 400 * 4);
                const CdFrame frame{sizeof(CdFrame), 400, 400, 1600, bytes.size(), bytes.data(), 2};
                canvas.render(frame, {}, [&](auto& c, auto) {
                    c.scale(.2, .2);
                    renderer.draw(c, state, value, .2);
                });
                for (int y = 236; y <= 264; ++y) {
                    for (int x = 185; x <= 213; ++x) {
                        if (std::hypot(x - 198.4, y - 249.6) <= 14) {
                            const auto index = size_t(y * 400 + x) * 4;
                            illuminated[quarter] += bytes[index] > 100 && bytes[index] > bytes[index + 2] * 1.5;
                        }
                    }
                }
            }
            check(illuminated[0] < illuminated[2] / 10 && illuminated[2] > 400 &&
                      illuminated[1] > illuminated[2] * .3 && illuminated[1] < illuminated[2] * .7 &&
                      illuminated[3] > illuminated[2] * .3 && illuminated[3] < illuminated[2] * .7,
                  "native new, quarter and full moon images show the expected illuminated proportions");
        }
        bool exactPixels = true;
        for (const auto [logical, dpi] : {std::pair{185., 1.25}, std::pair{187., 1.5}, std::pair{189., 1.75}}) {
            const auto edge = uint32_t(std::round(logical * dpi));
            std::vector<uint8_t> bytes(size_t(edge) * edge * 4);
            const CdFrame frame{sizeof(CdFrame), edge, edge, edge * 4, bytes.size(), bytes.data(), dpi};
            const auto paint = [&] {
                canvas.render(frame, {}, [&](auto& c, auto area) {
                    const double scale = area.width / kExtent;
                    c.scale(scale, scale);
                    renderer.draw(c, State{}, Options{}, scale);
                });
            };
            paint();
            for (int i = 0; renderer.pending() && i < 500; ++i) {
                renderer.poll();
                std::this_thread::sleep_for(std::chrono::milliseconds(4));
            }
            paint();
            const uint64_t brass = kind == Kind::Skeleton ? 64 * 64 * 4 : 0;
            exactPixels &= !renderer.pending() && renderer.assets().failed == 0 &&
                           renderer.assets().referencedBytes == uint64_t(edge) * edge * 4 + brass;
        }
        check(exactPixels, "fractional-DPI dial art decodes to its exact final pixel size without a second shrink");
    }
    check(guards, "162 native style/DPI/zoom/background/pose renders preserve memory guards");
    check(corners, "case and round-dial modes both retain genuinely transparent corners");
    check(readable, "all material renders preserve readable dials and intentional transparent space");
    cdsdk::Canvas canvas;
    Renderer fallback(Kind::Green);
    Model model(Kind::Green);
    Options value;
    std::vector<uint8_t> bytes(200 * 200 * 4);
    const CdFrame frame{sizeof(CdFrame), 200, 200, 800, bytes.size(), bytes.data(), 1};
    const auto paint = [&](int hour) {
        model.synchronize(environment(utc(2026, 10, 20, hour, 10, 36)), value);
        canvas.render(frame, {}, [&](auto& c, auto) {
            c.scale(.2, .2);
            fallback.draw(c, model.state(), value, .2);
        });
    };
    paint(10);
    const auto before = bytes;
    paint(11);
    check(bytes != before && bytes[(100 * 200 + 100) * 4 + 3] == 255,
          "missing art still produces an opaque readable dial with live moving hands");
}

void spacingTests() {
    for (auto kind : {Kind::Green, Kind::Dress, Kind::Skeleton}) {
        Host host(kind);
        Instance instance(host.context(kind));
        bool margins = true, visible = true;
        for (unsigned cell : {80u, 96u, 112u, 128u, 160u}) {
            for (double dpi : {1., 1.5, 2.}) {
                const unsigned logicalWidth = cell * 2 - 6;
                const unsigned logicalHeight = cell * 5 / 2 - 6;
                const unsigned width = unsigned(std::round(logicalWidth * dpi));
                const unsigned height = unsigned(std::round(logicalHeight * dpi));
                std::vector<uint8_t> bytes(size_t(width) * height * 4);
                const CdFrame frame{sizeof(CdFrame), width, height, width * 4, bytes.size(), bytes.data(), dpi};
                CdLayout layout{};
                layout.struct_size = sizeof(CdLayout);
                layout.logical_width = logicalWidth;
                layout.logical_height = logicalHeight;
                layout.safe_content = {0, 0, double(logicalWidth), double(logicalHeight)};
                instance.layoutChanged(layout);
                for (int warmup = 0; warmup < 20; ++warmup) {
                    host.advance(33);
                    instance.render(frame);
                    std::this_thread::sleep_for(std::chrono::milliseconds(3));
                }
                size_t ink = 0;
                for (unsigned y = 0; y < height; ++y) {
                    for (unsigned x = 0; x < width; ++x) {
                        if (bytes[(size_t(y) * width + x) * 4 + 3] > 32) {
                            ++ink;
                            margins &= x >= width * .04 && x < width * .96 && y >= height * .08 && y < height * .92;
                        }
                    }
                }
                visible &= ink > size_t(width) * height * .25 && ink < size_t(width) * height * .65;
            }
        }
        check(margins, "watch silhouette leaves visible gaps in all five grid sizes and three DPIs");
        check(visible, "complete watch remains readable without filling the tile or collapsing into a tiny dial");
    }
}
}  // namespace

int main() try {
    std::cout << std::unitbuf;
    timeTests();
    moonTests();
    lifecycleTests();
    renderingTests();
    spacingTests();
    std::cout << checks << " precision clock checks " << (passed ? "passed" : "FAILED") << '\n';
    return passed ? 0 : 1;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 2;
}
