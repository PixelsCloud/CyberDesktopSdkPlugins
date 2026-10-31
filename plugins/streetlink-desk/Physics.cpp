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

struct Physics::Impl {
    struct Particle {
        ph::Body body;
        unsigned chamber;
        double radius;
    };

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
    std::unique_ptr<ph::World> world;
    ph::Body ground;
    ph::Joint mouse;
    size_t dragged = 0;
    std::vector<Particle> particles;
    std::vector<Rect> chambers;
    std::vector<std::vector<Point>> lanes;
    std::array<float, 64> bands{};
    std::array<double, 24> spectrum{};
    CdRect safe{0, 0, 320, 200};
    Point artOrigin{};
    double artScale = 1;
    bool clock = false, hidden = false, arranged = false;
    uint32_t audioStatus = CD_DATA_LOADING;
    uint64_t lastAudio = 0, lastSecond = UINT64_MAX, beat = 0;
    double elapsed = 0, lastBeat = -1, bpm = 0;
    std::wstring time;

    Impl(const CdCreateContext& context, cdsdk::Settings& values)
        : settings(values), services(context.host, context.host_context) {
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
        if (world && mouse.id) {
            world->destroy(mouse);
        }
        mouse = {};
    }

    Point dragTarget(Point point, size_t index) const {
        const auto& particle = particles.at(index);
        point.x *= kUnits;
        point.y *= kUnits;
        if (!lanes.empty()) {
            const auto& lane = lanes.at(particle.chamber);
            point.x = std::clamp(point.x, lane.front().x + 2, lane.back().x - 2);
            for (size_t i = 1; i < lane.size(); ++i) {
                if (point.x <= lane[i].x) {
                    const double t = (point.x - lane[i - 1].x) / (lane[i].x - lane[i - 1].x);
                    point.y = lane[i - 1].y + t * (lane[i].y - lane[i - 1].y);
                    break;
                }
            }
        } else {
            const auto r = chambers.at(particle.chamber);
            const double rx = r.width / 2 - particle.radius - 1;
            const double ry = r.height / 2 - particle.radius - 1;
            const double x = (point.x - r.x - r.width / 2) / rx;
            const double y = (point.y - r.y - r.height / 2) / ry;
            const double scale = std::max(1., std::hypot(x, y));
            point = {r.x + r.width / 2 + x * rx / scale, r.y + r.height / 2 + y * ry / scale};
        }
        return {point.x / kUnits, point.y / kUnits};
    }

    void spawn(unsigned index, bool preload = false) {
        if (particles.size() >= 72) {
            return;
        }
        const unsigned channel = lanes.empty() ? index % chambers.size() : index % lanes.size();
        Point p;
        const double radius = lanes.empty() ? 1.25 : .95;
        if (!lanes.empty()) {
            p = lanes[channel].front();
            p.x += 1.5;
            if (preload) {
                const auto& lane = lanes[channel];
                const double x =
                    lane.front().x + (lane.back().x - lane.front().x - 4) * (index / lanes.size() + 1) / 21.;
                for (size_t j = 1; j < lane.size(); ++j) {
                    if (x <= lane[j].x) {
                        const double t = (x - lane[j - 1].x) / (lane[j].x - lane[j - 1].x);
                        p = {x, lane[j - 1].y + t * (lane[j].y - lane[j - 1].y)};
                        break;
                    }
                }
            }
        } else {
            const auto r = chambers[channel];
            const double a = index * 2.399963229728653;
            // Keep the complete particle inside the wall. A .51-radius seed placed
            // its centre outside the chamber and the first beat ejected it outward.
            const double ring = .12 + .10 * (index % 3);
            p = {r.x + r.width / 2 + std::cos(a) * r.width * ring, r.y + r.height / 2 + std::sin(a) * r.height * ring};
        }
        const auto body = world->createBody(
            {.position = {p.x / kUnits, p.y / kUnits}, .linearDamping = clock ? .7 : 1.1, .bullet = true});
        world->circle(body, {}, radius / kUnits, {.density = 1, .friction = .05, .restitution = .82});
        particles.push_back({body, channel, radius});
    }

    void reset() {
        cancel();
        particles.clear();
        world = std::make_unique<ph::World>(ph::WorldOptions{.gravity = {0, 0}, .substeps = 2, .collectEvents = false});
        ground = world->createBody({.type = ph::BodyType::Static});
        for (const auto r : chambers) {
            for (unsigned i = 0; i < 64; ++i) {
                const auto point = [&](unsigned n) {
                    const double a = n * 2 * std::numbers::pi / 64;
                    return Point{(r.x + r.width / 2 + std::cos(a) * r.width / 2) / kUnits,
                                 (r.y + r.height / 2 + std::sin(a) * r.height / 2) / kUnits};
                };
                world->capsule(ground, point(i), point(i + 1), .04, {.restitution = .8});
            }
        }
        for (const auto& lane : lanes) {
            // Audio impulses can reverse a particle. Close both ends so it cannot
            // leave the channel before the next simulation step wraps it around.
            for (const auto end : {lane.front(), lane.back()}) {
                world->capsule(ground, {end.x / kUnits, (end.y - 2) / kUnits}, {end.x / kUnits, (end.y + 2) / kUnits},
                               .025, {.restitution = .3});
            }
            for (size_t i = 1; i < lane.size(); ++i) {
                for (double side : {-1., 1.}) {
                    const Point a{lane[i - 1].x / kUnits, (lane[i - 1].y + side * 2) / kUnits};
                    const Point b{lane[i].x / kUnits, (lane[i].y + side * 2) / kUnits};
                    world->capsule(ground, a, b, .025, {.restitution = .3});
                }
            }
        }
        if (!clock) {
            for (unsigned i = 0; i < unsigned(std::stoi(settings.at("balls").text)); ++i) {
                spawn(i);
            }
        }
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
        if (hidden) {
            updates.stop();
            return;
        }
        if (clock) {
            updates.start(running() ? 33 : 1000, environment.snapshot().monotonic_ms);
        } else if (running() && (world->active() || audio.active() || mouse.id)) {
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
                    reset();
                    for (unsigned i = 0; i < unsigned(local.tm_sec); ++i) {
                        spawn(i, true);
                    }
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
        for (const auto& p : particles) {
            const auto state = world->state(p.body);
            if (!lanes.empty()) {
                const auto& lane = lanes[p.chamber];
                if (state.position.x * kUnits > lane.back().x - 2) {
                    world->teleport(p.body, {(lane.front().x + 2) / kUnits, lane.front().y / kUnits});
                }
                const double mass = std::numbers::pi * std::pow(p.radius / kUnits, 2);
                const double drive = 1.3 + audio.signal().bass * 2.5;
                const double blend = 1 - std::exp(-dt / .18);
                world->impulse(p.body,
                               {mass * (drive - state.velocity.x) * blend, mass * (.1 - state.velocity.y) * blend});
            } else if (audio.active()) {
                const auto r = chambers[p.chamber];
                const double x = state.position.x - (r.x + r.width / 2) / kUnits;
                const double y = state.position.y - (r.y + r.height / 2) / kUnits;
                const double mass = std::numbers::pi * std::pow(p.radius / kUnits, 2);
                const auto signal = audio.signal();
                const double radius = std::max(.01, std::hypot(x, y));
                const double drive = .5 + signal.bass * 1.5 + signal.middle * .6;
                const double phase = elapsed * (1.2 + signal.high * 3) + p.chamber * std::numbers::pi;
                // Follow a bounded velocity field. A positional tangential force keeps
                // injecting energy at the rim and eventually overwhelms tiny colliders.
                const Point target{-y / radius * drive - x * .8 + std::cos(phase) * signal.bass * .6,
                                   x / radius * drive - y * .8 + std::sin(phase) * signal.bass * .6};
                const double blend = 1 - std::exp(-dt / .18);
                world->impulse(p.body, {mass * (target.x - state.velocity.x) * blend,
                                        mass * (target.y - state.velocity.y) * blend});
            }
        }
        const bool moving = world->active();
        world->advance(dt);
        containParticles();
        return changed || moving;
    }

    void containParticles() {
        for (const auto& p : particles) {
            const auto state = world->state(p.body);
            Point position{state.position.x * kUnits, state.position.y * kUnits};
            Point normal{};
            bool corrected = false;
            if (lanes.empty()) {
                const auto r = chambers[p.chamber];
                const Point center{r.x + r.width / 2, r.y + r.height / 2};
                const double rx = r.width / 2 - p.radius - 1, ry = r.height / 2 - p.radius - 1;
                const double x = (position.x - center.x) / rx, y = (position.y - center.y) / ry;
                const double distance = std::hypot(x, y);
                if (distance > 1) {
                    position = {center.x + x * rx / distance, center.y + y * ry / distance};
                    normal = {x / rx, y / ry};
                    corrected = true;
                }
            } else {
                const auto& lane = lanes[p.chamber];
                Point closest{};
                double nearest = std::numeric_limits<double>::max();
                for (size_t i = 1; i < lane.size(); ++i) {
                    const Point a = lane[i - 1], b = lane[i];
                    const double dx = b.x - a.x, dy = b.y - a.y;
                    const double t =
                        std::clamp(((position.x - a.x) * dx + (position.y - a.y) * dy) / (dx * dx + dy * dy), 0., 1.);
                    const Point point{a.x + t * dx, a.y + t * dy};
                    const double distance = std::hypot(position.x - point.x, position.y - point.y);
                    if (distance < nearest) {
                        nearest = distance;
                        closest = point;
                    }
                }
                const double clearance = std::max(.1, 2 - p.radius - .5);
                if (nearest > clearance) {
                    normal = {position.x - closest.x, position.y - closest.y};
                    position = {closest.x + normal.x * clearance / nearest, closest.y + normal.y * clearance / nearest};
                    corrected = true;
                }
            }
            Point velocity = state.velocity;
            if (corrected) {
                const double length = std::hypot(normal.x, normal.y);
                normal = {normal.x / length, normal.y / length};
                const double outward = velocity.x * normal.x + velocity.y * normal.y;
                if (outward > 0) {
                    velocity = {velocity.x - normal.x * outward * 1.65, velocity.y - normal.y * outward * 1.65};
                }
                world->teleport(p.body, {position.x / kUnits, position.y / kUnits});
            }
            const double speed = std::hypot(velocity.x, velocity.y);
            if (speed > 4) {
                velocity = {velocity.x * 4 / speed, velocity.y * 4 / speed};
            }
            if (corrected || speed > 4) {
                world->velocity(p.body, velocity);
            }
        }
    }

    void kick(double strength) {
        for (size_t i = 0; i < particles.size(); ++i) {
            const auto& p = particles[i];
            const double angle = i * 2.399963 + elapsed;
            const double mass = std::numbers::pi * std::pow(p.radius / kUnits, 2);
            world->impulse(p.body, {std::cos(angle) * mass * strength, std::sin(angle) * mass * strength});
        }
    }
};

Physics::Physics(const CdCreateContext& context)
    : settings(defaults(std::string_view(context.plugin_id).find("clock.marble") != std::string_view::npos)),
      impl_(std::make_unique<Impl>(context, settings)) {}

Physics::~Physics() = default;

CdResult Physics::render(const CdFrame& frame) {
    auto& s = *impl_;
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
            const auto state = s.world->state(particle.body);
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
            const auto state = s.world->state(s.particles[i].body);
            const double x = s.artOrigin.x + state.position.x * kUnits * s.artScale;
            const double y = s.artOrigin.y + state.position.y * kUnits * s.artScale;
            regions.push_back({uint32_t(i + 1), cybercanvas::HitShape::ellipse({x - 4, y - 4, 8, 8}),
                               cybercanvas::PointerBehavior::Drag, 1});
        }
    }
    s.interaction.regions(std::move(regions));
    s.arranged = okay;
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
    const auto value = impl_->world->stats();
    return {value.steps, value.advanceNanoseconds, value.awakeBodies, value.maximumAdvanceNanoseconds};
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
        s.dragged = hit.target - 1;
        s.mouse = s.world->mouse(s.ground, s.particles[s.dragged].body, s.dragTarget(p, s.dragged), 10);
    } else if (hit.drag == cybercanvas::DragPhase::Update && s.mouse.id) {
        s.world->mouseTarget(s.mouse, s.dragTarget(p, s.dragged));
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
