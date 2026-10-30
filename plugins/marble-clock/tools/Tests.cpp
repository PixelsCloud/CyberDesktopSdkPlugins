#include <CyberCanvas/PanelLayout.h>
#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <thread>

#include "plugins/marble-clock/Clock.h"
#include "plugins/mechanical-instruments/PhysicsMaterials.h"

namespace {
using namespace marbleclock;
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
    bool captured = false, enabled = false, captureAllowed = true;
    CdEnvironmentSnapshot environment{
        sizeof(CdEnvironmentSnapshot), 1, 1760000000000ull, now, 480, CD_ENV_THEME_DARK, "zh-CN"};
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
            if (version != 1) {
                return CD_ERROR_UNSUPPORTED;
            }
            if (std::string_view(name) == CD_SERVICE_ENVIRONMENT && size >= sizeof(CdEnvironmentApi)) {
                *static_cast<CdEnvironmentApi*>(output) = {sizeof(CdEnvironmentApi), 1,
                                                           [](void* host, CdEnvironmentSnapshot* value) -> CdResult {
                                                               *value = static_cast<Host*>(host)->environment;
                                                               return CD_OK;
                                                           }};
                return CD_OK;
            }
            if (std::string_view(name) == CD_SERVICE_INTERACTION && size >= sizeof(CdInteractionApi)) {
                *static_cast<CdInteractionApi*>(output) = {sizeof(CdInteractionApi), 1,
                                                           [](void* host, uint32_t enable) -> CdResult {
                                                               static_cast<Host*>(host)->enabled = enable != 0;
                                                               return CD_OK;
                                                           },
                                                           [](void* host, uint32_t capture) -> CdResult {
                                                               auto& self = *static_cast<Host*>(host);
                                                               if (capture && !self.captureAllowed) {
                                                                   return CD_ERROR_UNSUPPORTED;
                                                               }
                                                               self.captured = capture != 0;
                                                               return CD_OK;
                                                           }};
                return CD_OK;
            }
            return CD_ERROR_UNSUPPORTED;
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

    CdCreateContext context(const char* id = "com.cyberdesktop.clock.marble.6x2") {
        return {sizeof(CdCreateContext), 1, id, "test", CD_MARBLE_TEST_ROOT, &api.input.media.base, this};
    }
};

void assetTests() {
    Renderer renderer(std::filesystem::path(CD_MARBLE_TEST_ROOT) / "assets");
    for (unsigned i = 0; renderer.pendingAssets() && i < 1000; ++i) {
        renderer.pollAssets();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    renderer.pollAssets();
    const auto stats = renderer.assetStats();
    check(stats.ready == 4 && !stats.pending && !stats.failed,
          "all four packaged PNG materials decode on the shared worker");
    check(stats.referencedBytes <= 6 * 1024 * 1024,
          "shared mechanical materials have a bounded decoded memory footprint");
    uint64_t pngBytes = 0;
    for (const auto& entry :
         std::filesystem::directory_iterator(std::filesystem::path(CD_MARBLE_TEST_ROOT) / "assets")) {
        if (entry.path().extension() == ".png") {
            pngBytes += entry.file_size();
        }
    }
    check(pngBytes < 12 * 1024 * 1024,
          "both independently authored chassis and common precision parts stay below 12 MiB");
    Model model;
    model.setTime(10, 28, 44);
    cdsdk::Canvas canvas;
    std::vector<uint8_t> bytes(1200 * 400 * 4);
    CdFrame frame{sizeof(CdFrame), 1200, 400, 4800, bytes.size(), bytes.data(), 2};
    const auto draw = [&] {
        canvas.render(frame, {}, [&](auto& c, auto) {
            c.scale(200 / kHeight, 200 / kHeight);
            renderer.draw(c, model, {}, 200 / kHeight);
        });
    };
    draw();
    for (unsigned i = 0; renderer.pendingAssets() && i < 1000; ++i) {
        renderer.pollAssets();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    draw();
    const auto first = bytes;
    const auto uploads = canvas.stats().uploads.count;
    const auto before = model.motionTime();
    for (unsigned i = 0; i < 30; ++i) {
        draw();
    }
    check(first == bytes && model.motionTime() == before,
          "loaded PNGs render deterministically without advancing mechanics");
    check(canvas.stats().uploads.count == uploads, "repeated steel balls and gears reuse their uploaded textures");
    for (bool compact : {false, true}) {
        const auto shape = geometry(compact);
        bool valid = true;
        for (int i = 0; i <= 100; ++i) {
            const auto drive = driveGeometry(shape, kClosedAngle + (kOpenAngle - kClosedAngle) * i / 100);
            valid &= std::isfinite(drive.angle) &&
                     std::abs(std::hypot(drive.crank.x - drive.lever.x, drive.crank.y - drive.lever.y) - 26) < 1e-6;
        }
        check(valid, "the visible crank and output lever preserve rod length through the entire gate travel");
    }
    model.setTime(10, 28, 45);
    const auto firstAngle = model.feederAngle();
    step(model, 6);
    check(model.feederAngle() != firstAngle, "the feeder wheel advances in response to an actual emitted ball");
    step(model, 30);
    const auto finalAngle = model.feederAngle();
    step(model, 30);
    check(model.feederAngle() == finalAngle, "the feeder finishes its stroke and stays still between emissions");
    model.setTime(23, 59, 59, false);
    model.setTime(0, 0, 0);
    check(model.previousTimeText() == L"23:59:59" && model.digitProgress() == 0,
          "mechanical digit transitions handle midnight without corrupting displayed time");
    step(model, 18);
    check(model.digitProgress() == 1, "the digit drum finishes its brief transition between clock ticks");
    model.setTime(2, 30, 45, false);
    check(model.digitProgress() == 1,
          "reduced motion and time resynchronization update digits without replaying an animation");
    model.setTime(2, 30, 46);
    step(model, 3);
    model.quiet();
    check(model.digitProgress() == 1, "pausing mid-transition settles the drum instead of freezing a clipped digit");
}

void feedingTests() {
    for (bool compact : {false, true}) {
        for (int start : {0, 25, 40}) {
            Model model(compact);
            model.setTime(10, 28, start);
            size_t mostOutside = 0;
            for (int second = start + 1; second < 60; ++second) {
                model.setTime(10, 28, second);
                step(model, 60);
                size_t inside = 0;
                for (const auto& marble : model.marbles()) {
                    const auto p = model.state(marble).position;
                    inside += p.x * kPixelsPerMeter > model.shape().left + 8 &&
                              p.x * kPixelsPerMeter < model.shape().pivot - 8 && p.y * kPixelsPerMeter > 210 &&
                              p.y * kPixelsPerMeter < 330;
                }
                mostOutside = std::max(mostOutside, model.marbles().size() - inside);
                if (model.marbles().size() != size_t(second)) {
                    throw std::runtime_error("a normal incoming marble escaped collection before the minute ended");
                }
            }
            std::cout << "feeding diagnostics compact=" << compact << " start=" << start
                      << " maximum outside=" << mostOutside << '\n';
            if (mostOutside > 6) {
                for (const auto& marble : model.marbles()) {
                    const auto p = model.state(marble).position;
                    std::cout << "ball " << marble.token << " at " << p.x * kPixelsPerMeter << ','
                              << p.y * kPixelsPerMeter << '\n';
                }
            }
            check(mostOutside <= 6, "continuous incoming balls reach the bin without backing up onto the return rail");
        }
    }
}

void modelTests(bool compact) {
    Model model(compact);
    model.setTime(10, 28, 57);
    check(model.timeText() == L"10:28:57" && model.marbles().size() == 57,
          "current minute reconstructs a stable pile without replaying elapsed seconds");
    check(!model.setTime(10, 28, 57) && model.releases() == 0, "same second never creates duplicate balls");
    check(model.shape().width == (compact ? 720 : 1080),
          "compact layout changes geometry rather than shrinking its text");
    model.setTime(10, 28, 58);
    const auto ball = model.marbles().back();
    const auto source = model.state(ball).position;
    check(model.releases() == 1 && source.x * kPixelsPerMeter < 100, "new second releases one body at the dispenser");
    step(model, 20);
    check(model.state(ball).position.x > source.x && model.contacts() > 0,
          "gravity and contact move the ball along its first rail");
    bool reachedReturn = false;
    for (unsigned i = 0; i < 420; ++i) {
        model.advance(1. / 60);
        const auto found = std::find_if(model.marbles().begin(), model.marbles().end(), [&](const Marble& m) {
            return m.token == ball.token;
        });
        if (found != model.marbles().end()) {
            const auto p = model.state(*found).position;
            reachedReturn |= p.x * kPixelsPerMeter < model.shape().pivot && p.y * kPixelsPerMeter > 215;
        }
    }
    check(reachedReturn, "a released ball traverses both rail directions and enters the collection bin");
    const auto beforeDrain = model.marbles().size();
    model.drain();
    step(model, 30);
    check(model.flapAngle() < -.10 && model.phase() != Phase::Collecting,
          "motor opens the loaded flap with a real hinge and bounded angle");
    model.release();
    check(model.pending() == 1, "new releases queue while the old minute empties");
    step(model, 420);
    std::cout << "drain diagnostics compact=" << compact << " old=" << model.oldMarbles()
              << " recycled=" << model.recycled() << " recovered=" << model.recoveries()
              << " count=" << model.marbles().size() << " angle=" << model.flapAngle() << '\n';
    check(model.recycled() >= beforeDrain && model.recoveries() == 0,
          "normal full-bin emptying reaches the return outlet without fallback deletion");
    check(
        model.phase() == Phase::Collecting && model.pending() == 0 && std::abs(model.flapAngle() - kClosedAngle) < .01,
        "flap closes and queued releases resume automatically");
    model.setTime(23, 59, 59, false);
    model.setTime(0, 0, 0);
    check(model.timeText() == L"00:00:00" && model.phase() == Phase::Opening,
          "midnight changes the clock correctly and starts the new collection cycle");
    model.setTime(12, 30, 12);
    check(model.timeText() == L"12:30:12" && model.phase() == Phase::Collecting && model.marbles().size() == 12,
          "system clock jumps rebuild current state without missed-ball bursts");
    const auto token = model.marbles().front().token;
    const auto p = model.state(model.marbles().front()).position;
    check(model.beginDrag(token, {p.x * kPixelsPerMeter, p.y * kPixelsPerMeter}) && model.stats().joints == 2,
          "grabbing a stable ball creates exactly one mouse joint");
    model.drag({-10000, -10000});
    step(model, 30);
    const auto held = model.state(model.marbles().front()).position;
    check(held.x > 0 && held.y > .15, "drag targets outside the plugin clamp to physical bounds");
    model.cancelDrag();
    check(!model.dragging() && model.stats().joints == 1,
          "release destroys mouse capture without losing the flap hinge");
    model.drain();
    for (unsigned i = 0; i < 100; ++i) {
        model.release();
    }
    check(model.pending() == 8, "repeated input cannot create an unbounded pending-ball queue");
    model.quiet();
    check(model.phase() == Phase::Collecting && !model.dragging() && model.pending() == 0,
          "quiet state clears transient physical actions and pending releases");
    const auto before = model.stats().steps;
    model.advance(std::numeric_limits<double>::quiet_NaN());
    model.advance(-1);
    check(model.stats().steps == before, "invalid elapsed time cannot enter the physics solver");

    Model sustained(compact);
    sustained.setTime(0, 0, 0);
    size_t peak = 0;
    for (int seconds = 1; seconds <= 600; ++seconds) {
        sustained.setTime(seconds / 3600, seconds / 60 % 60, seconds % 60);
        for (unsigned frame = 0; frame < 60; ++frame) {
            sustained.advance(1. / 60);
            peak = std::max(peak, sustained.marbles().size());
            if (sustained.marbles().size() > kMaximumMarbles || sustained.stats().bodies > 74 ||
                sustained.flapAngle() < kOpenAngle - .015 || sustained.flapAngle() > kClosedAngle + .015) {
                throw std::runtime_error("physics capacity or flap limits exceeded");
            }
            for (const auto& marble : sustained.marbles()) {
                const auto position = sustained.state(marble).position;
                if (!std::isfinite(position.x) || !std::isfinite(position.y) || position.x < 0 ||
                    position.x * kPixelsPerMeter > sustained.shape().width || position.y < .16 || position.y > .72) {
                    throw std::runtime_error("a ball escaped the visible mechanism");
                }
            }
        }
    }
    std::cout << "ten-minute diagnostics compact=" << compact << " peak=" << peak
              << " releases=" << sustained.releases() << " recycled=" << sustained.recycled()
              << " recovered=" << sustained.recoveries() << '\n';
    check(peak >= 57 && peak <= kMaximumMarbles && sustained.releases() >= 590,
          "ten minutes preserve one release per second with bounded backlog and body count");
    check(sustained.recycled() >= 530 && sustained.recoveries() == 0,
          "repeated minute cycles physically recycle their balls without normal-path recovery");
    step(sustained, 600);
    check(sustained.marbles().empty() && sustained.stats().bodies == 2 && sustained.stats().joints == 1,
          "final cycle releases every ball body while retaining only ground and flap");
}

void cadenceTests() {
    std::vector<Point> poses;
    for (int fps : {30, 60, 120, 144}) {
        Model model;
        model.setTime(0, 0, 0);
        model.release();
        model.drain();
        for (int i = 0; i < fps * 8; ++i) {
            model.advance(1. / fps);
        }
        std::cout << "cadence diagnostics fps=" << fps << " recycled=" << model.recycled()
                  << " recovered=" << model.recoveries() << " phase=" << int(model.phase()) << '\n';
        check(model.recycled() == 1 && model.recoveries() == 0 && model.phase() == Phase::Collecting,
              "each display cadence completes the same physical release and recycle cycle");
        model.release();
        for (int i = 0; i < fps; ++i) {
            model.advance(1. / fps);
        }
        poses.push_back(model.state(model.marbles().back()).position);
    }
    check(std::all_of(poses.begin(), poses.end(),
                      [&](Point p) {
                          return std::hypot(p.x - poses.front().x, p.y - poses.front().y) < .001;
                      }),
          "30/60/120/144 Hz apply motor and conveyor forces on identical fixed simulation steps");
}

void captureTests() {
    for (bool compact : {false, true}) {
        Host host;
        host.environment.unix_ms = host.environment.unix_ms / 60000 * 60000 + 20000;
        const auto id = compact ? "com.cyberdesktop.clock.marble.4x2" : "com.cyberdesktop.clock.marble.6x2";
        Instance instance(host.context(id));
        const uint32_t logicalWidth = compact ? 400 : 600;
        constexpr double dpi = 2;
        const uint32_t width = (logicalWidth + 20) * 2, height = 440;
        std::vector<uint8_t> bytes(size_t(width) * height * 4);
        CdFrame frame{sizeof(CdFrame), width, height, width * 4, bytes.size(), bytes.data(), dpi};
        CdLayout layout{};
        layout.struct_size = sizeof(layout);
        layout.logical_width = logicalWidth + 20;
        layout.logical_height = 220;
        layout.safe_content = {10, 10, double(logicalWidth), 200};
        instance.layoutChanged(layout);
        instance.render(frame);
        const auto shape = geometry(compact);
        auto safe = mechanicalart::physicsmaterials::sceneBounds(logicalWidth, 200);
        safe.x += 10;
        safe.y += 10;
        const auto viewport = cybercanvas::sceneViewport(safe, {shape.width, kHeight});
        const auto hit = viewport.toPanel({shape.left + 17, floorHeight(shape, shape.left + 17) - 13});
        const double x = hit.x, y = hit.y;
        auto event = [&](uint32_t kind, double px, double py, uint32_t buttons = CD_BUTTON_LEFT) {
            instance.input({sizeof(CdInput), kind, buttons, 0, px, py, 0});
        };
        event(CD_INPUT_PRESS, x, y);
        check(host.captured, "DPI-aware ball hit testing claims capture before the host can move the tile");
        event(CD_INPUT_MOVE, -500, -500);
        host.advance(33);
        instance.render(frame);
        check(host.captured, "moving outside the plugin retains a bounded physical drag");
        event(CD_INPUT_CANCEL, 0, 0, 0);
        check(!host.captured, "lost-capture cancellation releases the mouse joint and host claim");
        instance.setPaused(true);
        instance.setPaused(false);
        instance.render(frame);
        event(CD_INPUT_PRESS, x, y);
        instance.layoutChanged(layout);
        check(!host.captured, "layout change cancels capture before replacing input geometry");
        instance.render(frame);
        event(CD_INPUT_PRESS, x, y);
        instance.setPaused(true);
        check(!host.captured && host.timers.empty(), "hide releases active capture and removes animation subscription");
        instance.setPaused(false);
        instance.render(frame);
        host.captureAllowed = false;
        event(CD_INPUT_PRESS, x, y);
        check(!host.captured, "a host that rejects capture leaves no invisible physical grab");
    }
}

void rollingClockTests() {
    const auto output = std::filesystem::path(CD_MARBLE_TEST_OUTPUT) / "rolling";
    std::filesystem::create_directories(output);
    for (bool compact : {false, true}) {
        for (unsigned cellWidth : {80u, 160u}) {
            for (double dpi : {1., 2.}) {
                Host host;
                host.environment.unix_ms = host.environment.unix_ms / 60000 * 60000 + 57000;
                const char* id = compact ? "com.cyberdesktop.clock.marble.4x2" : "com.cyberdesktop.clock.marble.6x2";
                Instance instance(host.context(id));
                const double logicalWidth = (compact ? 4 : 6) * cellWidth - 6;
                const double logicalHeight = 2 * cellWidth * 5 / 4 - 6;
                const auto width = uint32_t(logicalWidth * dpi), height = uint32_t(logicalHeight * dpi);
                std::vector<uint8_t> bytes(size_t(width) * height * 4);
                CdFrame frame{sizeof(CdFrame), width, height, width * 4, bytes.size(), bytes.data(), dpi};
                CdLayout layout{};
                layout.struct_size = sizeof(layout);
                layout.logical_width = logicalWidth;
                layout.logical_height = logicalHeight;
                layout.device_pixel_ratio = dpi;
                layout.safe_content = {0, 0, logicalWidth, logicalHeight};
                instance.layoutChanged(layout);
                instance.render(frame);
                for (int i = 0; i < 50; ++i) {
                    instance.render(frame);
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                const auto shape = geometry(compact);
                const auto viewport = cybercanvas::sceneViewport(
                    mechanicalart::physicsmaterials::sceneBounds(logicalWidth, logicalHeight), {shape.width, kHeight});
                const auto topLeft = viewport.toPanel({shape.width - 110, 29});
                const auto bottomRight = viewport.toPanel({shape.width - 74, 86});
                const auto digits = [&] {
                    std::vector<uint8_t> result;
                    for (unsigned y = unsigned(std::ceil(topLeft.y * dpi)); y < unsigned(bottomRight.y * dpi); ++y) {
                        const auto begin =
                            bytes.begin() + size_t(y) * frame.stride + unsigned(std::ceil(topLeft.x * dpi)) * 4;
                        const auto end = bytes.begin() + size_t(y) * frame.stride + unsigned(bottomRight.x * dpi) * 4;
                        result.insert(result.end(), begin, end);
                    }
                    return result;
                };
                cdsdk::Canvas capture;
                const auto prefix = std::string(compact ? "4x2" : "6x2") + "-w" + std::to_string(cellWidth) + "-dpi" +
                                    std::to_string(int(dpi * 100));
                const auto save = [&](const char* phase) {
                    check(capture.save((output / (prefix + "-" + phase + ".png")).wstring(), frame),
                          "rolling clock host frame saved at actual grid size and DPI");
                };
                const auto before = digits();
                save("before");
                host.advance(1000);
                instance.render(frame);
                host.advance(66);
                instance.render(frame);
                const auto rolling = digits();
                save("rolling");
                host.advance(300);
                instance.render(frame);
                const auto settled = digits();
                save("settled");
                check(!before.empty() && rolling != before && settled != rolling && settled != before,
                      "actual instance renders an intermediate rolling drum and settles on the next second");
                instance.render(frame);
                check(digits() == settled, "drawing a settled drum does not advance its animation");
            }
        }
    }
}

void lifecycleTests() {
    Host host;
    host.environment.unix_ms -= host.environment.unix_ms % 60000;
    {
        Instance instance(host.context());
        constexpr uint32_t width = 620, height = 220, stride = width * 4 + 16;
        std::vector<uint8_t> bytes(size_t(stride) * height, 0);
        CdFrame frame{sizeof(CdFrame), width, height, stride, bytes.size(), bytes.data(), 1};
        CdLayout layout{};
        layout.struct_size = sizeof(layout);
        layout.logical_width = width;
        layout.logical_height = height;
        layout.safe_content = {10, 10, 600, 200};
        instance.layoutChanged(layout);
        check(instance.render(frame) == CD_OK && host.enabled,
              "host draws a padded safe-area frame and enables interactive input");
        // Decode completion is a legitimate visual update, independent of clock motion.
        for (int i = 0; i < 50; ++i) {
            instance.render(frame);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        const auto initial = bytes;
        const auto redraws = host.redraws;
        instance.render(frame);
        check(bytes == initial && host.redraws == redraws,
              "rendering never advances time, physics or redraw subscriptions");
        check(bytes[3] == 0 && bytes[(height - 1) * stride + 3] == 0,
              "outside safe-content margins remain transparent");
        host.advance(1000);
        check(host.timers.size() == 1 && host.timers.begin()->second.interval == 33,
              "one host timer changes to animation cadence when the next ball is released");
        for (int i = 0; i < 20; ++i) {
            host.advance(33);
            instance.render(frame);
        }
        check(bytes != initial, "actual host frames display changing time and moving physical balls");
        instance.setPaused(true);
        const auto hiddenRedraws = host.redraws;
        host.advance(3600000);
        check(host.timers.empty() && host.redraws == hiddenRedraws && !host.captured,
              "hidden plugin releases timers and pointer capture without redraw work");
        instance.setPaused(false);
        check(host.timers.size() == 1, "resume installs one subscription and synchronizes to current time");
        CdSettingValue reduced{sizeof(CdSettingValue), CD_SETTING_TOGGLE, "reduced_motion", 1};
        check(instance.applySettings(&reduced, 1) == CD_OK, "reduced motion is accepted");
        host.advance(1000);
        check(host.timers.begin()->second.interval > 33,
              "reduced motion retains second-level clock updates without physical ticks");
        instance.render(frame);
        const auto before = bytes;
        CdSettingValue theme{sizeof(CdSettingValue), CD_SETTING_CHOICE, "theme", 0, 0, "light"};
        check(instance.applySettings(&theme, 1) == CD_OK && instance.render(frame) == CD_OK && bytes != before,
              "theme change invalidates the cached backplate");
        CdSettingValue invalid[]{{sizeof(CdSettingValue), CD_SETTING_CHOICE, "theme", 0, 0, "dark"},
                                 {sizeof(CdSettingValue), CD_SETTING_CHOICE, "unknown", 0, 0, "bad"}};
        check(instance.applySettings(invalid, 2) != CD_OK && instance.settings.at("theme").text == "light",
              "invalid settings reject atomically and preserve the working theme");
        auto broken = frame;
        broken.rgba = nullptr;
        check(instance.render(broken) == CD_ERROR_ARGUMENT, "malformed frame buffers are rejected");
        layout.logical_width = std::numeric_limits<double>::quiet_NaN();
        instance.layoutChanged(layout);
        check(instance.render(frame) == CD_OK, "malformed layout notifications preserve usable geometry");
    }
    check(host.timers.empty() && !host.captured, "destruction releases all host subscriptions and capture");
}

void abiTest(const wchar_t* filename, const char* id) {
    const auto library = LoadLibraryW(filename);
    check(library != nullptr, "published DLL loads");
    const auto query = reinterpret_cast<CdResult(CD_CALL*)(uint32_t, CdPluginApi*, uint32_t)>(
        GetProcAddress(library, "cd_plugin_query"));
    CdPluginApi api{};
    check(query && query(99, &api, sizeof(api)) == CD_ERROR_VERSION && query(1, &api, sizeof(api)) == CD_OK &&
              std::string_view(api.id) == id,
          "both layout DLLs advertise their actual ID through C ABI 1");
    Host host;
    auto context = host.context(id);
    CdInstanceApi instance{};
    check(api.create(&context, &instance, sizeof(instance)) == CD_OK, "exported plugin creates an owned host instance");
    instance.destroy(instance.instance);
    check(host.timers.empty(), "C ABI instance destruction releases its timer");
    FreeLibrary(library);
}
}  // namespace

int wmain(int argc, wchar_t** argv) try {
    const auto imageStore = cybercanvas::ImageStore::shared();
    assetTests();
    feedingTests();
    modelTests(false);
    modelTests(true);
    cadenceTests();
    captureTests();
    rollingClockTests();
    lifecycleTests();
    if (argc == 3) {
        abiTest(argv[1], "com.cyberdesktop.clock.marble.6x2");
        abiTest(argv[2], "com.cyberdesktop.clock.marble.4x2");
    }
    std::cout << checks << " checks passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
}
