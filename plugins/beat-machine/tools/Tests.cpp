#include <CyberCanvas/PanelLayout.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <thread>

#include "Machine.h"
#include "plugins/beat-machine/tools/TestHost.h"
#include "plugins/mechanical-instruments/PhysicsMaterials.h"

namespace {
using namespace beatmachine;
unsigned checks = 0;
bool passed = true;

void check(bool value, const char* message) {
    ++checks;
    passed &= value;
    std::cout << (value ? "PASS " : "FAIL ") << message << '\n';
}

void audioTests() {
    AudioMotion audio;
    std::array<float, 63> bands{};
    bands.fill(.8f);
    audio.feed(bands);
    for (int i = 0; i < 90; ++i) {
        audio.advance(1. / 30, false);
    }
    check(audio.signal().beat == 1, "sustained tone triggers once rather than every frame");
    for (int beat = 0; beat < 8; ++beat) {
        bands.fill(0);
        audio.feed(bands);
        for (int i = 0; i < 15; ++i) {
            audio.advance(1. / 30, false);
        }
        std::fill_n(bands.begin(), 21, .9f);
        audio.feed(bands);
        for (int i = 0; i < 3; ++i) {
            audio.advance(1. / 30, false);
        }
    }
    check(audio.signal().beat >= 8 && audio.signal().beat <= 10, "separate bass pulses remain separate events");
    const auto beats = audio.signal().beat;
    for (int i = 0; i < 300; ++i) {
        audio.advance(1. / 30, true);
    }
    check(audio.signal().bass == 0 && !audio.active() && audio.signal().beat == beats,
          "stale input decays to silence without generating beats");
    audio.clear();
    bands.fill(std::numeric_limits<float>::quiet_NaN());
    audio.feed(bands);
    audio.advance(.033, false);
    check(audio.signal().bass == 0 && !audio.active(), "invalid spectrum samples cannot energize physics");
    bands.fill(0);
    std::fill(bands.begin() + 42, bands.end(), .8f);
    audio.feed(bands);
    audio.advance(.1, false);
    check(audio.signal().high > .7 && audio.signal().bass == 0 && audio.signal().beat == 0,
          "high frequency input is distinct from bass triggers");
    audio.clear();
    for (int frame = 0; frame < 30 * 10; ++frame) {
        bands.fill(0);
        if (frame % 15 < 3) {
            bands[12] = .015f;
            bands[13] = .03f;
            bands[14] = .015f;
        }
        audio.feed(bands);
        audio.advance(1. / 30, false);
    }
    check(audio.signal().beat >= 16, "quiet narrowband bass is not diluted below a fixed absolute threshold");
    audio.clear();
    bands.fill(.003f);
    audio.feed(bands);
    for (int i = 0; i < 300; ++i) {
        audio.advance(1. / 30, false);
    }
    check(audio.signal().beat == 0 && !audio.active(), "sub-gate spectrum noise does not keep the mechanism awake");
}

void bassMappingTests() {
    const auto spectrum = [](double frequency, float amplitude) {
        std::array<float, 63> bands{};
        const int band = std::clamp(int(std::round(std::log(frequency / 20) / std::log(800.) * 63 - .5)), 0, 62);
        bands[band] = amplitude;
        return bands;
    };
    for (unsigned channel = 0; channel < 4; ++channel) {
        AudioMotion audio;
        Model model(36);
        constexpr std::array<double, 4> frequencies{45, 75, 115, 180};
        for (int frame = 0; frame < 240; ++frame) {
            const auto bands = spectrum(frequencies[channel], std::fmod(frame / 60., .125) < .04 ? .7f : 0.f);
            audio.feed(bands);
            audio.advance(1. / 60, false);
            model.advance(1. / 60, audio.signal());
        }
        const auto& signal = audio.signal();
        check(signal.pistonBeat[channel] >= 30 && signal.pistonBeat[channel] <= 33,
              "eight low-frequency attacks per second remain separate piston events");
        bool separated = true;
        for (unsigned i = 0; i < 4; ++i) {
            separated &= i == channel || (signal.pistonBeat[i] == 0 && model.pistons()[i].strikes == 0);
        }
        check(separated, "each main piston responds only to its own low-frequency subrange");
        check(signal.pistonBeat[4] >= signal.pistonBeat[channel] && model.pistons()[4].strikes >= 30,
              "wide-band launcher follows rapid kicks from each main piston subrange");
        const auto counts = signal.pistonBeat;
        for (int frame = 0; frame < 120; ++frame) {
            audio.feed(spectrum(frequencies[channel], .7f));
            audio.advance(1. / 60, false);
            model.advance(1. / 60, audio.signal());
        }
        check(audio.signal().pistonBeat[channel] <= counts[channel] + 1,
              "sustained bass does not become a repeated automatic drum trigger");
        check(std::all_of(model.pistons().begin(), model.pistons().end(),
                          [&](const auto& piston) {
                              return std::abs(piston.rest.y - model.state(piston.body).position.y * kUnits) < 2;
                          }),
              "all five pistons return to rest between attacks even while bass remains loud");
    }
    for (double frequency : {1000., 6000.}) {
        AudioMotion audio;
        for (int frame = 0; frame < 120; ++frame) {
            audio.feed(spectrum(frequency, frame % 15 < 3 ? .9f : 0.f));
            audio.advance(1. / 60, false);
        }
        check(std::all_of(audio.signal().pistonBeat.begin(), audio.signal().pistonBeat.end(),
                          [](auto count) {
                              return count == 0;
                          }),
              "middle and high frequency pulses do not fire bass pistons");
    }
    AudioMotion quiet;
    quiet.feed(spectrum(240, .005f));
    quiet.advance(1. / 60, false);
    check(quiet.signal().pistonBeat[4] == 1 && quiet.signal().beat == 0,
          "launcher detects a weak attack outside the four narrower main ranges");
}

void assetTests() {
    Renderer renderer(std::filesystem::path(CD_BEAT_TEST_ROOT) / "assets");
    for (int i = 0; renderer.pendingAssets() && i < 500; ++i) {
        renderer.pollAssets();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    renderer.pollAssets();
    check(renderer.assetStats().ready == 7 && renderer.assetStats().failed == 0,
          "chassis and all six independent material layers decode successfully");
    check(renderer.assetStats().referencedBytes < 5 * 1024 * 1024,
          "mechanical PNG layers remain below five MiB decoded");
    size_t bytes = 0;
    for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::path(CD_BEAT_TEST_ROOT) / "assets")) {
        if (entry.path().extension() == ".png") {
            bytes += entry.file_size();
        }
    }
    check(bytes < 12 * 1024 * 1024, "runtime PNG materials stay below twelve MiB on disk");
}

void modelTests(unsigned count, bool compact = false) {
    Model model(count, compact);
    Signal signal;
    bool finite = true, bounded = true;
    double highest = 360;
    for (int i = 0; i < 1800 * 60; ++i) {
        signal.bass = i % 30 < 5 ? .8 : .08;
        signal.middle = .35;
        signal.high = .2;
        if (i % 30 == 0) {
            ++signal.beat;
            signal.strength = .8;
        }
        if (i % 90 == 0) {
            ++signal.accent;
        }
        model.advance(1. / 60, signal);
        for (const auto& ball : model.balls()) {
            const auto state = model.state(ball.body);
            finite &= std::isfinite(state.position.x) && std::isfinite(state.position.y);
            bounded &= state.position.x > .03 && state.position.x < (model.width() - 20) / kUnits &&
                       state.position.y > .1 && state.position.y < .72;
            if (i > 600) {
                highest = std::min(highest, state.position.y * kUnits);
            }
        }
    }
    std::cout << "soak layout=" << (compact ? "4x2" : "6x2") << " balls=" << count << " launches=" << model.launches()
              << " ejections=" << model.ejections() << " returns=" << model.returns() << " hits=" << model.hits()
              << " recoveries=" << model.recoveries() << " highest=" << highest << '\n';
    check(finite && bounded && model.recoveries() == 0, "30 simulated minutes remain bounded without fallback resets");
    check(model.balls().size() == count && model.stats().bodies == count + 8 && model.stats().joints == 7,
          "sustained music preserves the fixed inventory and joint budget");
    check(model.launches() > 500 && model.returns() > 100 && model.hits() > 500 && highest < 220,
          "repeated music physically relaunches balls into the playfield");
    check(model.ejections() > model.launches() / 2,
          "seated balls receive stored energy even without a new impact event");
    model.quiet();
    for (int i = 0; i < 60 * 120; ++i) {
        model.advance(1. / 60);
    }
    std::cout << "silent awake=" << model.stats().awakeBodies << '\n';
    check(!model.active(), "silence allows the complete mechanism to sleep");
    const auto ball = model.balls().front();
    check(model.beginDrag(ball.token, {300, 180}) && model.stats().joints == 8, "drag adds one mouse joint");
    model.drag({-10000, -10000});
    for (int i = 0; i < 30; ++i) {
        model.advance(1. / 60);
    }
    model.cancelDrag();
    check(!model.dragging() && model.stats().joints == 7, "drag cancellation releases its joint");
    const auto before = model.stats().steps;
    model.advance(-1);
    model.advance(std::numeric_limits<double>::quiet_NaN());
    check(before == model.stats().steps, "invalid elapsed time is rejected before the solver");
}

void lifecycleTests(bool compact = false) {
    // Keep shared decoded materials alive; asset arrival is a valid visual change
    // and must not be mistaken for physics advancing during render.
    Renderer readyMaterials(std::filesystem::path(CD_BEAT_TEST_ROOT) / "assets", compact);
    for (int i = 0; readyMaterials.pendingAssets() && i < 500; ++i) {
        readyMaterials.pollAssets();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    Host host;
    {
        Instance instance(host.context(compact ? "com.cyberdesktop.music.beat-machine.4x2" : kId));
        check(host.audio.size() == 1 && host.status.size() == 1, "instance subscribes once to shared FFT and status");
        const uint32_t width = compact ? 400 : 600;
        std::vector<uint8_t> bytes(width * 200 * 4);
        CdFrame frame{sizeof(CdFrame), width, 200, width * 4, bytes.size(), bytes.data(), 1};
        check(instance.render(frame) == CD_OK, "native plugin renderer accepts a real frame");
        for (int i = 0; i < 50; ++i) {
            instance.render(frame);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        const auto reference = bytes;
        instance.render(frame);
        check(reference == bytes, "render does not advance music or physical state");
        Model initial(36, compact);
        const auto point = initial.state(initial.balls().front().body).position;
        const auto viewport = cybercanvas::sceneViewport(mechanicalart::physicsmaterials::sceneBounds(width, 200),
                                                         {initial.width(), kHeight});
        const auto hit = viewport.toPanel({point.x * kUnits, point.y * kUnits});
        instance.input({sizeof(CdInput), CD_INPUT_PRESS, CD_BUTTON_LEFT, 0, hit.x, hit.y, 0});
        check(host.captured, "ball hit testing claims host capture before moving the tile");
        instance.input({sizeof(CdInput), CD_INPUT_MOVE, CD_BUTTON_LEFT, 0, -200, -200, 0});
        instance.input({sizeof(CdInput), CD_INPUT_CANCEL, 0, 0, 0, 0, 0});
        check(!host.captured, "lost capture cancels the physical drag");
        host.send(.8f);
        host.advance(33);
        instance.setPaused(true);
        check(host.audio.empty() && host.status.empty() && host.timers.empty() && !host.captured,
              "hide removes audio, status, timers and capture");
        host.advance(3600000);
        instance.setPaused(false);
        check(host.audio.size() == 1 && host.status.size() == 1, "resume replaces subscriptions without duplicates");
        instance.render(frame);
        const CdSettingValue stopped{sizeof(CdSettingValue), CD_SETTING_TOGGLE, "running", 0};
        check(instance.applySettings(&stopped, 1) == CD_OK && host.audio.empty(), "running switch releases live audio");
        const CdSettingValue invalid{sizeof(CdSettingValue), CD_SETTING_NUMBER, "gain", 0, 100};
        check(instance.applySettings(&invalid, 1) == CD_ERROR_ARGUMENT, "settings reject unsupported gain atomically");
        const CdSettingValue legacy{sizeof(CdSettingValue), CD_SETTING_CHOICE, "balls", 0, 0, "16"};
        check(instance.applySettings(&legacy, 1) == CD_OK && instance.settings.at("balls").text == "36",
              "existing sparse instances upgrade to 36 marbles without losing other settings");
    }
    check(host.audio.empty() && host.status.empty() && host.timers.empty(), "destruction leaves no host callbacks");
}
}  // namespace

void cadenceTests(bool compact = false) {
    using namespace beatmachine;
    std::vector<Point> poses;
    for (int fps : {30, 60, 120, 144}) {
        Model model(36, compact);
        const Signal signal{.5, .2, .3, .8, 1, 1};
        for (int i = 0; i < fps * 6; ++i) {
            model.advance(1. / fps, signal);
        }
        poses.push_back(model.state(model.balls().front().body).position);
    }
    check(std::all_of(poses.begin(), poses.end(),
                      [&](Point p) {
                          return std::hypot(p.x - poses.front().x, p.y - poses.front().y) < .0001;
                      }),
          "30/60/120/144 Hz advance identical fixed physical steps");
}

void circulationTests(bool compact) {
    using namespace beatmachine;
    Model model(36, compact);
    const double boundary = compact ? 220 : 320;
    std::vector<bool> left(36, false), touching(36, false);
    uint64_t arrivals = 0, contacts = 0, total = 0, frames = 0;
    for (int frame = 0; frame < 60 * 90; ++frame) {
        const Signal signal{frame % 30 < 5 ? .8 : .08, .35, .2, .8, uint64_t(frame / 30 + 1), uint64_t(frame / 90 + 1)};
        model.advance(1. / 60, signal);
        const auto hammer = model.state(model.pistons().front().body).position;
        for (size_t i = 0; i < model.balls().size(); ++i) {
            const auto p = model.state(model.balls()[i].body).position;
            const bool inLeft = p.x * kUnits < boundary;
            const bool atHammer = std::abs((p.x - hammer.x) * kUnits) < (compact ? 45 : 61) &&
                                  (hammer.y - p.y) * kUnits > 0 && (hammer.y - p.y) * kUnits < 24;
            if (frame >= 600) {
                arrivals += inLeft && !left[i];
                contacts += atHammer && !touching[i];
                total += inLeft;
            }
            left[i] = inLeft;
            touching[i] = atHammer;
        }
        frames += frame >= 600;
    }
    std::cout << "circulation layout=" << (compact ? "4x2" : "6x2") << " measuredSeconds=80 leftArrivals=" << arrivals
              << " meanLeftBalls=" << double(total) / frames << " firstHammerVisits=" << contacts
              << " recoveries=" << model.recoveries() << '\n';
    // Gentler hammers shorten airborne residence in the left field. Require a
    // tenth of the inventory there on average, plus real arrivals and hammer visits.
    check(arrivals >= 100 && double(total) / frames >= model.balls().size() * .1 && contacts >= 30,
          "return trajectories continually supply the left playfield and first hammer");
}

int main(int argc, char** argv) try {
    if (argc > 1 && std::string_view(argv[1]) == "--circulation") {
        circulationTests(false);
        circulationTests(true);
        return passed ? 0 : 1;
    }
    audioTests();
    bassMappingTests();
    assetTests();
    modelTests(36);
    modelTests(48);
    cadenceTests();
    lifecycleTests();
    modelTests(36, true);
    modelTests(48, true);
    cadenceTests(true);
    lifecycleTests(true);
    circulationTests(false);
    circulationTests(true);
    std::cout << checks << " beat machine checks " << (passed ? "passed" : "FAILED") << '\n';
    return passed ? 0 : 1;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 2;
}
