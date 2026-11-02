#include "plugins/streetlink-desk/Physics.h"

#include <CyberCanvas/Physics2D.h>
#include <CyberCanvas/Sprites.h>
#include <sdk/Environment.h>
#include <sdk/Interaction.h>
#include <sdk/Update.h>
#include <sdk/cpp/Services.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <numbers>

#include "plugins/beat-machine/Machine.h"
#include "plugins/mecha-desk/Skin.h"
#include "plugins/streetlink-desk/PhysicsModel.h"

namespace streetlink {
namespace {
namespace ph = cybercanvas::physics;
using cybercanvas::Point;
using cybercanvas::Rect;
constexpr double kUnits = 20;

cdsdk::Settings defaults(bool clock) {
    cdsdk::Settings result{{"show_background", CD_SETTING_TOGGLE, 1},
                           {"running", CD_SETTING_TOGGLE, 1},
                           {"gain", CD_SETTING_NUMBER, 0, 3.5, {}, .25, 5},
                           {"reduced_motion", CD_SETTING_TOGGLE, 0}};
    if (!clock) {
        return {{{"show_background", CD_SETTING_TOGGLE, 1},
                 {"running", CD_SETTING_TOGGLE, 1},
                 {"reduced_motion", CD_SETTING_TOGGLE, 0},
                 {"gain", CD_SETTING_NUMBER, 0, 3.5, {}, .25, 5},
                 {"balls", CD_SETTING_CHOICE, 0, 0, "36", 0, 0, {"12", "16", "24", "36", "48"}}}};
    }
    return result;
}
}  // namespace

struct Physics::Impl : PhysicsModel {
    cdsdk::Settings& settings;
    aerisskin::Skin skin;
    cybercanvas::PrimitiveSprites sprites;
    cdsdk::Environment environment;
    cdsdk::Services services;
    cdsdk::AudioWatch audioWatch;
    cdsdk::AudioStatusWatch statusWatch;
    cdsdk::UpdateLoop updates;
    cdsdk::RedrawRequests redraw;
    cdsdk::Interaction interaction;
    beatmachine::AudioMotion audio;
    std::array<float, 64> bands{};
    std::array<double, 24> spectrum{};
    CdRect safe{0, 0, 320, 200};
    Point artOrigin{};
    double artScale = 1;
    bool hidden = false, arranged = false;
    uint32_t audioStatus = CD_DATA_LOADING;
    uint64_t lastAudio = 0, lastSecond = UINT64_MAX, beat = 0;
    double elapsed = 0, lastBeat = -1, bpm = 0;
    std::wstring time;

    Impl(const CdCreateContext& context, cdsdk::Settings& values)
        : PhysicsModel(context), settings(values), services(context.host, context.host_context) {
        clock = std::string_view(context.plugin_id).find("clock.marble") != std::string_view::npos;
        skin.bind(context);
        environment.bind(context);
        updates.bind(context);
        redraw.bind(context);
        interaction.bind(context);
        skin.onChange([this] {
            redraw.invalidate();
        });
        const auto path =
            std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(context.plugin_directory)));
        std::ifstream file(path / "assets/skin.json");
        const auto config = nlohmann::json::parse(file).at("physics");
        for (const auto& r : config.at("chambers")) {
            chambers.push_back({r[0], r[1], r[2], r[3]});
        }
        for (const auto& lane : config.at("lanes")) {
            std::vector<Point> points;
            for (const auto& p : lane) {
                points.push_back({p[0], p[1]});
            }
            lanes.push_back(std::move(points));
        }
        reset();
        updates.onInvalidate([this] {
            redraw.invalidate();
        });
        updates.onUpdate([this](cybercanvas::AnimationTick tick) {
            const bool changed = advance(tick.delta);
            schedule();
            return cdsdk::UpdateStep{changed, true};
        });
        subscribe();
        advance(0);
        schedule();
    }

    ~Impl() {
        audioWatch.reset();
        statusWatch.reset();
        updates.stop();
        skin.onChange({});
        cancel();
    }

    bool running() const {
        return !hidden && settings.at("running").integer && !settings.at("reduced_motion").integer;
    }

    void cancel() {
        interaction.cancel();
        cancelDrag();
    }

    void reset(unsigned preload = 0) {
        cancel();
        PhysicsModel::reset(clock ? preload : unsigned(std::stoi(settings.at("balls").text)));
        lastSecond = UINT64_MAX;
    }

    void subscribe() {
        audioWatch.reset();
        statusWatch.reset();
        audio.clear();
        bands.fill(0);
        spectrum.fill(0);
        lastAudio = 0;
        bpm = 0;
        lastBeat = -1;
        if (!running()) {
            return;
        }
        const CdAudioOptions options{sizeof(CdAudioOptions), 63, 30, 0, nullptr};
        statusWatch = services.audioStatus(CD_SERVICE_FFT, options, [this](const CdAudioStatus& value) {
            audioStatus = value.status;
            redraw.invalidate();
        });
        audioWatch = services.audio(CD_SERVICE_FFT, options, [this](const cdsdk::AudioSnapshot& sample) {
            audio.feed(sample.samples, sample.sampleRate);
            bands.fill(0);
            std::copy_n(sample.samples.begin(), std::min(sample.samples.size(), bands.size()), bands.begin());
            audioStatus = CD_DATA_READY;
            lastAudio = environment.snapshot().monotonic_ms;
            schedule();
        });
    }

    void schedule() {
        pauseBackground(!running());
        if (hidden) {
            updates.stop();
            return;
        }
        if (clock) {
            updates.start(running() ? 33 : 1000, environment.snapshot().monotonic_ms);
        } else if (running() && (active() || audio.active())) {
            updates.start(33, environment.snapshot().monotonic_ms);
        } else {
            updates.stop();
        }
    }

    bool advance(double dt) {
        const auto now = environment.snapshot();
        bool changed = false;
        if (clock) {
            const auto local = cdsdk::Environment::localTime(now);
            time = std::format(L"{:02}:{:02}:{:02}", local.tm_hour, local.tm_min, local.tm_sec);
            const auto second = now.unix_ms / 1000;
            if (second != lastSecond) {
                if (lastSecond == UINT64_MAX || second != lastSecond + 1 || local.tm_sec == 0) {
                    reset(unsigned(local.tm_sec));
                } else if (running()) {
                    spawn(unsigned(local.tm_sec));
                }
                lastSecond = second;
                changed = true;
            }
        }
        if (!running()) {
            return changed;
        }
        dt = std::clamp(dt, 0., .1);
        elapsed += dt;
        const bool stale = !lastAudio || now.monotonic_ms < lastAudio || now.monotonic_ms - lastAudio > 250 ||
                           audioStatus != CD_DATA_READY;
        {
            if (stale) {
                bands.fill(0);
            }
            changed |= audio.advance(dt, stale, settings.at("gain").number);
            for (size_t i = 0; i < spectrum.size(); ++i) {
                const size_t first = i * 63 / spectrum.size(), end = (i + 1) * 63 / spectrum.size();
                double peak = 0;
                for (size_t band = first; band < end; ++band) {
                    if (std::isfinite(bands[band])) {
                        peak = std::max(peak, double(bands[band]));
                    }
                }
                const double target = stale ? 0 : std::pow(std::clamp(peak * settings.at("gain").number, 0., 1.), .65);
                const double before = spectrum[i];
                spectrum[i] += (target - before) * (1 - std::exp(-dt / (target > before ? .035 : .18)));
                if (spectrum[i] < .001) {
                    spectrum[i] = 0;
                }
                changed |= std::abs(spectrum[i] - before) > .0001;
            }
            const auto signal = audio.signal();
            if (signal.beat != beat) {
                if (lastBeat >= 0 && elapsed - lastBeat >= .25 && elapsed - lastBeat <= 2) {
                    const double sample = 60 / (elapsed - lastBeat);
                    bpm = bpm ? bpm * .75 + sample * .25 : sample;
                }
                lastBeat = elapsed;
                beat = signal.beat;
                kick(.9 + signal.strength * 3.5);
            }
            if (stale && elapsed - lastBeat > 3) {
                bpm = 0;
            }
        }
        const bool moving = active();
        advancePhysics(dt, audio.signal(), audio.active(), elapsed);
        return !backgrounded() && (changed || moving);
    }

    void kick(double strength) {
        PhysicsModel::kick(strength, elapsed);
    }
};

Physics::Physics(const CdCreateContext& context)
    : settings(defaults(std::string_view(context.plugin_id).find("clock.marble") != std::string_view::npos)),
      impl_(std::make_unique<Impl>(context, settings)) {}

Physics::~Physics() = default;

CdResult Physics::render(const CdFrame& frame) {
    auto& s = *impl_;
    s.consumeBackground();
    s.redraw.beginFrame();
    s.updates.beginFrame();
    s.skin.appearance(settings);
    const bool okay = s.skin.render(frame, s.safe, [&](auto& c) {
        for (const auto& lane : s.lanes) {
            auto path = c.path();
            path.moveTo(lane.front());
            for (size_t i = 1; i < lane.size(); ++i) {
                path.lineTo(lane[i]);
            }
            c.stroke(path, 0x082c32ff, {.width = 5});
            c.stroke(path, 0x00c7ce88, {.width = .7});
        }
        if (!s.clock) {
            for (const auto r : s.chambers) {
                c.oval(r.x + r.width / 2, r.y + r.height / 2, r.width / 2, r.height / 2, 0x081b20ff);
                c.oval(r.x + r.width / 2, r.y + r.height / 2, r.width * .25, r.height * .25, 0x00dce966, .6);
            }
        }
        s.sprites.begin(c, 3.5, 0);
        for (const auto& particle : s.particles) {
            // Draw the final constrained pose. Box2D's intermediate interpolation
            // state can still precede our channel/chamber boundary correction.
            const auto state = s.state(particle.body);
            const auto color = particle.chamber % 2 ? 0xf6d848ffu : 0x49f5ffffu;
            s.sprites.add(c, color, {state.position.x * kUnits, state.position.y * kUnits}, [&](auto& tile) {
                tile.oval(0, 0, particle.radius, particle.radius, color);
                tile.oval(-.3, -.3, .35, .35, 0xffffffff);
            });
        }
        s.sprites.flush(c);
        const auto readout = s.skin.region("readout0");
        const auto value = s.clock ? s.time : s.bpm > 0 ? std::to_wstring(int(std::round(s.bpm))) : L"--";
        s.skin.label(c, value, readout, readout.height * .85, 0xf3f8faff);
        const auto plot = s.skin.region("signal");
        if (plot.width > 0) {
            constexpr unsigned count = 24;
            const double step = plot.width / count;
            for (unsigned i = 0; i < count; ++i) {
                const double h = std::max(.5, plot.height * s.spectrum[i]);
                c.box(plot.x + i * step, plot.y + plot.height - h, step * .65, h, .1, 0x00e7efff);
            }
        }
        const auto meters = s.skin.region("meters");
        constexpr unsigned metersCount = 9;
        for (unsigned i = 0; meters.width > 0 && i < metersCount; ++i) {
            const double height = std::max(.5, meters.height * s.spectrum[i * 23 / (metersCount - 1)]);
            c.box(meters.x + i * meters.width / metersCount, meters.y + meters.height - height,
                  meters.width / (metersCount * 1.5), height, .2, 0x00e7efff);
        }
    });
    std::vector<cybercanvas::HitRegion> regions;
    if (s.clock && s.running()) {
        const auto native = s.skin.region("readout0");
        const auto display = s.skin.displayRegion("readout0", s.safe);
        s.artScale = display.width / native.width;
        s.artOrigin = {s.safe.x + display.x - native.x * s.artScale, s.safe.y + display.y - native.y * s.artScale};
        for (size_t i = 0; i < s.particles.size(); ++i) {
            const auto state = s.state(s.particles[i].body);
            const double x = s.artOrigin.x + state.position.x * kUnits * s.artScale;
            const double y = s.artOrigin.y + state.position.y * kUnits * s.artScale;
            regions.push_back({uint32_t(i + 1), cybercanvas::HitShape::ellipse({x - 4, y - 4, 8, 8}),
                               cybercanvas::PointerBehavior::Drag, 1});
        }
    }
    s.interaction.regions(std::move(regions));
    s.arranged = okay;
    s.schedule();
    return okay ? CD_OK : CD_ERROR_ARGUMENT;
}

CdResult Physics::applySettings(const CdSettingValue* values, uint32_t count) {
    auto candidate = settings;
    const auto result = candidate.apply(values, count);
    if (result != CD_OK) {
        return result;
    }
    auto& s = *impl_;
    const bool countChanged = !s.clock && candidate.at("balls").text != settings.at("balls").text;
    s.cancel();
    settings = std::move(candidate);
    if (countChanged) {
        s.reset();
    }
    s.subscribe();
    s.schedule();
    s.redraw.invalidate();
    return CD_OK;
}

void Physics::layoutChanged(const CdLayout& layout) {
    auto& s = *impl_;
    s.cancel();
    s.arranged = false;
    const auto r = layout.safe_content;
    if (std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.width) && std::isfinite(r.height) && r.x >= 0 &&
        r.y >= 0 && r.width > 14 && r.height > 14 && r.x + r.width <= layout.logical_width &&
        r.y + r.height <= layout.logical_height) {
        s.safe = r;
    }
    s.redraw.invalidate();
}

std::array<uint64_t, 4> Physics::physicsCounters() const {
    const auto value = impl_->stats();
    return {value.steps, value.advanceNanoseconds, value.awakeBodies, value.maximumAdvanceNanoseconds};
}

cybercanvas::physics::PhysicsStats Physics::physicsStats() const {
    return impl_->stats();
}

void Physics::setPaused(bool paused) {
    auto& s = *impl_;
    s.hidden = paused;
    s.cancel();
    s.skin.pause(paused);
    s.updates.pause(paused);
    s.redraw.pause(paused);
    s.subscribe();
    if (!paused) {
        s.lastSecond = UINT64_MAX;
        s.advance(0);
        s.redraw.invalidate();
    }
    s.schedule();
}

void Physics::input(const CdInput& event) {
    auto& s = *impl_;
    if (!s.clock) {
        return;
    }
    if (event.kind == CD_INPUT_CANCEL) {
        s.cancel();
        return;
    }
    if (s.hidden || !s.arranged || !std::isfinite(event.x) || !std::isfinite(event.y)) {
        return;
    }
    const auto toggle = s.skin.displayRegion("toggle", s.safe);
    if (event.kind == CD_INPUT_RELEASE && event.x >= toggle.x && event.x < toggle.x + toggle.width &&
        event.y >= toggle.y && event.y < toggle.y + toggle.height) {
        const CdSettingValue change{sizeof(CdSettingValue), CD_SETTING_TOGGLE, "running",
                                    !settings.at("running").integer};
        applySettings(&change, 1);
        return;
    }
    if (!s.running()) {
        return;
    }
    if (event.kind == CD_INPUT_RELEASE) {
        for (const auto name : {"previous", "next"}) {
            const auto r = s.skin.displayRegion(name, s.safe);
            if (event.x >= r.x && event.x < r.x + r.width && event.y >= r.y && event.y < r.y + r.height) {
                s.cancel();
                if (std::string_view(name) == "previous") {
                    s.reset();
                } else {
                    s.kick(2);
                }
                s.schedule();
                s.redraw.invalidate();
                return;
            }
        }
    }
    const auto hit = s.interaction.input(event);
    const Point p{(event.x - s.artOrigin.x) / s.artScale / kUnits, (event.y - s.artOrigin.y) / s.artScale / kUnits};
    if (hit.drag == cybercanvas::DragPhase::Begin && hit.target > 0 && hit.target <= s.particles.size()) {
        if (!s.beginDrag(hit.target - 1, p)) {
            s.interaction.cancel();
        }
    } else if (hit.drag == cybercanvas::DragPhase::Update) {
        s.drag(p);
    } else if (hit.drag == cybercanvas::DragPhase::End || hit.drag == cybercanvas::DragPhase::Cancel) {
        s.cancel();
    } else if (event.kind == CD_INPUT_DOUBLE_CLICK) {
        s.reset();
    } else if (event.kind == CD_INPUT_PRESS && (event.buttons & CD_BUTTON_LEFT)) {
        if (s.clock) {
            s.spawn(unsigned(s.particles.size()));
        } else {
            s.kick(2);
        }
    }
    s.schedule();
    s.redraw.invalidate();
}
}  // namespace streetlink
