#include "plugins/deep-whale/Motion.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>
#include <numbers>

#include "plugins/deep-whale/BeatScene.h"
#include "plugins/physics-toys/Toys.h"

namespace deep_motion {
using namespace cybercanvas;
namespace ph = cybercanvas::physics;

namespace {

cdsdk::Settings defaults(bool chime, bool compact) {
    if (chime) {
        return {{"speed", CD_SETTING_NUMBER, 0, .75, "", .25, 2},
                {"wind", CD_SETTING_NUMBER, 0, .55, "", 0, 1},
                {"running", CD_SETTING_TOGGLE, 1},
                {"reduced_motion", CD_SETTING_TOGGLE, 0},
                {"show_background", CD_SETTING_TOGGLE, 1},
                {"palette", CD_SETTING_CHOICE, 0, 0, "pastel", 0, 0, {"pastel", "ocean"}}};
    }
    return {{"theme", CD_SETTING_CHOICE, 0, 0, "dark", 0, 0, {"dark", "light", "system"}},
            {"show_background", CD_SETTING_TOGGLE, 1},
            {"gain", CD_SETTING_NUMBER, 0, 3.5, "", .25, 5},
            {"balls", CD_SETTING_CHOICE, 0, 0, compact ? "18" : "28", 0, 0, {"12", "18", "24", "28", "36", "48"}},
            {"running", CD_SETTING_TOGGLE, 1},
            {"reduced_motion", CD_SETTING_TOGGLE, 0}};
}
}  // namespace

struct Instance::Impl : BeatScene {
    CdCreateContext context;
    cdsdk::Settings& settings;
    whaleskin::Skin skin;
    cdsdk::Environment environment;
    cdsdk::UpdateLoop updates;
    cdsdk::RedrawRequests redraw;
    cdsdk::Interaction interaction;
    cdsdk::AudioWatch audio;
    cdsdk::AudioStatusWatch audioStatus;
    beatmachine::AudioMotion motion;
    std::unique_ptr<toys::Model> chimeModel;
    std::vector<std::pair<std::string, Size>> chimeArt;
    std::array<Point, 3> chimePins;

    ph::Joint mouse;
    CdRect safe{};
    uint64_t lastAudio = 0;
    bool chime = false, hidden = false;
    unsigned status = CD_DATA_LOADING;
    std::filesystem::path directory;

    Impl(const CdCreateContext& c, cdsdk::Settings& s) : context(c), settings(s) {
        directory = std::filesystem::u8path(c.plugin_directory);
        skin.bind(c);
        chime = skin.folder().starts_with("chime");
        environment.bind(c);
        redraw.bind(c);
        interaction.bind(c);
        updates.bind(c);
        updates.onInvalidate([this] {
            redraw.invalidate();
        });
        updates.onUpdate([this](AnimationTick t) {
            bool changed = skin.poll();
            if (!frozen()) {
                const auto now = environment.snapshot().monotonic_ms;
                if (!chime) {
                    motion.advance(t.delta,
                                   !lastAudio || now < lastAudio || now - lastAudio > 250 || status != CD_DATA_READY,
                                   settings.at("gain").number);
                }
                step(t.delta);
                changed = true;
            }
            return cdsdk::UpdateStep{
                changed, skin.pending() || (!frozen() && (chime ? chimeModel->active() || settings.at("wind").number > 0
                                                                : world->active() || motion.active()))};
        });
        reset();
        subscribe();
        schedule();
    }

    ~Impl() {
        audio.reset();
        audioStatus.reset();
        updates.stop();
        cancel();
        skin.onChange({});
    }

    bool frozen() const {
        return hidden || !settings.at("running").integer || settings.at("reduced_motion").integer;
    }

    void cancel() {
        interaction.cancel();
        if (chimeModel) {
            chimeModel->cancelDrag();
        }
        if (world && mouse.id) {
            world->destroy(mouse);
        }
        mouse = {};
    }

    void reset() {
        cancel();
        balls.clear();
        pendulums.clear();
        world.reset();
        time = pulse = 0;
        beat = accent = 0;
        if (chime) {
            std::ifstream f(directory / "assets/skin.json");
            auto j = nlohmann::json::parse(f);
            toys::ChimeLayout layout;
            layout.size = {skin.size().width - 14, skin.size().height - 14};
            chimeArt.clear();
            for (unsigned pin = 0; pin < 3; ++pin) {
                const auto& value = j.at("pendulums").at(pin).at("pivot");
                chimePins[pin] = {value[0], value[1]};
            }
            unsigned i = 0;
            for (const auto& e : j.at("chimePieces")) {
                Rect r{e["rect"][0], e["rect"][1], e["rect"][2], e["rect"][3]};
                const Point center{(r.x + r.width / 2 - 7) / 100, (r.y + r.height / 2 - 7) / 100};
                const int parent = e.at("parent");
                const Point anchor = parent < 0 ? Point{center.x, (r.y - 7) / 100}
                                                : Point{0, layout.pieces.at(size_t(parent)).half.height};
                layout.pieces.at(i) = {center, anchor, {0, -r.height / 200}, {r.width / 200, r.height / 200},
                                       parent, i};
                chimeArt.emplace_back(e.at("name").get<std::string>(), Size{r.width, r.height});
                ++i;
            }
            chimeModel = std::make_unique<toys::Model>(layout);
            chimeModel->setWindSeed(uint32_t(environment.snapshot().monotonic_ms ^ reinterpret_cast<uintptr_t>(this)));
            return;
        }
        resetBeat(skin.region("world"), unsigned(std::stoi(settings.at("balls").text)), skin.columns() == 4);
    }

    void step(double dt) {
        if (chime) {
            chimeModel->advance(dt, settings.at("wind").number, settings.at("speed").number);
        } else {
            stepBeat(dt, motion.signal());
        }
    }

    void subscribe() {
        audio.reset();
        audioStatus.reset();
        lastAudio = 0;
        motion.clear();
        if (chime || frozen()) {
            return;
        }
        const cdsdk::Services services(context.host, context.host_context);
        const CdAudioOptions options{sizeof(CdAudioOptions), 63, 30, 0, nullptr};
        audioStatus = services.audioStatus(CD_SERVICE_FFT, options, [this](const CdAudioStatus& s) {
            status = s.status;
            schedule();
        });
        audio = services.audio(CD_SERVICE_FFT, options, [this](const cdsdk::AudioSnapshot& s) {
            if (frozen()) {
                return;
            }
            status = CD_DATA_READY;
            lastAudio = environment.snapshot().monotonic_ms;
            motion.feed(s.samples, s.sampleRate);
            schedule();
        });
    }

    void schedule() {
        if (hidden ||
            (!skin.pending() && (frozen() || (chime && !chimeModel->active() && settings.at("wind").number == 0)))) {
            updates.stop();
        } else {
            updates.start(33, environment.snapshot().monotonic_ms);
        }
    }

    void orb(Canvas& c, Point p, double r, unsigned color) {
        constexpr uint32_t light[]{0xf4fcffff, 0xfff6e2ff, 0xf0f3ffff}, dark[]{0x7fb8d3ff, 0xdec08dff, 0xaaaed3ff};
        c.gradientBox({p.x - r, p.y - r, r * 2, r * 2}, r, light[color % 3], dark[color % 3]);
        c.oval(p.x, p.y, r, r, 0x64829bff, .45);
        c.oval(p.x - r * .23, p.y - r * .28, r * .20, r * .20, 0xfffefab0);
    }

    void targets() {
        std::vector<HitRegion> regions;
        if (!frozen()) {
            unsigned i = 0;
            auto add = [&](const Piece& p) {
                const auto s = world->state(p.body);
                Rect r;
                if (chime) {
                    r = {s.position.x * kUnits + p.art.x, s.position.y * kUnits + p.art.y, p.art.width, p.art.height};
                } else {
                    r = {field.x + s.position.x * kUnits - 6, field.y + s.position.y * kUnits - 6, 12, 12};
                }
                // Convert complete art coordinates to the same logical host viewport.
                const auto size = skin.size();
                const double scale =
                    std::min((safe.width - 14) / (size.width - 14), (safe.height - 14) / (size.height - 14));
                r = {safe.x + (safe.width - size.width * scale) / 2 + r.x * scale,
                     safe.y + (safe.height - size.height * scale) / 2 + r.y * scale, r.width * scale, r.height * scale};
                regions.push_back({++i, HitShape::rectangle(r), PointerBehavior::Drag, 1});
            };
            for (const auto& b : balls) {
                add(b);
            }
            if (chime) {
                const auto size = skin.size();
                const double scale =
                    std::min((safe.width - 14) / (size.width - 14), (safe.height - 14) / (size.height - 14));
                for (size_t index = 0; index < chimeArt.size(); ++index) {
                    const auto state = chimeModel->state(index);
                    const auto art = chimeArt[index].second;
                    const auto transform =
                        D2D1::Matrix3x2F::Rotation(float(state.angle * 180 / std::numbers::pi)) *
                        D2D1::Matrix3x2F::Translation(float(7 + state.position.x * 100),
                                                      float(7 + state.position.y * 100)) *
                        D2D1::Matrix3x2F::Scale(float(scale), float(scale)) *
                        D2D1::Matrix3x2F::Translation(float(safe.x + (safe.width - size.width * scale) / 2),
                                                      float(safe.y + (safe.height - size.height * scale) / 2));
                    regions.push_back({uint32_t(index + 1),
                                       HitShape::rectangle({-art.width / 2, -art.height / 2, art.width, art.height})
                                           .transformed(transform),
                                       PointerBehavior::Drag, 1});
                }
            } else {
                for (const auto& p : pendulums) {
                    add(p);
                }
            }
            if (!chime) {
                auto r = skin.displayRegion("world", safe);
                r.x += safe.x;
                r.y += safe.y;
                regions.push_back({1000, HitShape::rectangle(r), PointerBehavior::Button, 0});
            }
        }
        interaction.regions(std::move(regions));
    }
};

Instance::Instance(const CdCreateContext& c)
    : settings(defaults(std::string_view(c.plugin_id).find("windchime") != std::string_view::npos,
                        std::string_view(c.plugin_id).ends_with("4x2"))),
      impl_(std::make_unique<Impl>(c, settings)) {}

Instance::~Instance() = default;

CdResult Instance::render(const CdFrame& frame) {
    auto& p = *impl_;
    p.updates.beginFrame();
    p.redraw.beginFrame();
    p.skin.appearance(settings);
    const bool ok = p.skin.render(frame, p.safe, [&](auto& c) {
        if (p.chime) {
            for (size_t i = 0; i < p.chimeArt.size(); ++i) {
                const auto state = p.chimeModel->state(i);
                const auto& piece = p.chimeModel->pieces()[i];
                const auto anchor = p.chimeModel->anchor(piece), attachment = p.chimeModel->attachment(piece);
                const auto from = i % 2 ? Point{7 + anchor.x * 100, 7 + anchor.y * 100} : p.chimePins[i / 2];
                c.line(from.x, from.y, 7 + attachment.x * 100, 7 + attachment.y * 100,
                       settings.at("palette").text == "ocean" ? 0x5a9fbcff : 0xc6aa7bff, .65);
                c.saved([&] {
                    c.translate(7 + state.position.x * 100, 7 + state.position.y * 100);
                    c.rotate(state.angle * 180 / std::numbers::pi);
                    const auto& [name, size] = p.chimeArt[i];
                    p.skin.part(c, name, {-size.width / 2, -size.height / 2, size.width, size.height}, true);
                    if (settings.at("palette").text == "ocean") {
                        c.oval(0, 0, std::min(2., size.width * .12), std::min(2., size.width * .12), 0x65d7ffff);
                    }
                });
            }
        } else {
            c.clipped(p.field, [&] {
                c.translate(p.field.x, p.field.y);
                for (const auto& b : p.pendulums) {
                    const auto s = p.world->interpolated(b.body);
                    Point bob{(s.position.x - std::sin(s.angle) * b.length) * kUnits,
                              (s.position.y + std::cos(s.angle) * b.length) * kUnits};
                    c.line(b.anchor.x * kUnits, b.anchor.y * kUnits, bob.x, bob.y, 0x466c8fff, 1.2);
                    p.orb(c, {b.anchor.x * kUnits, b.anchor.y * kUnits}, 2, 1);
                    p.orb(c, bob, b.radius * kUnits, b.color);
                }
                const double y = p.world->interpolated(p.floor).position.y * kUnits, w = p.field.width;
                for (unsigned i = 0; i < 3; ++i) {
                    const double x = w * (.28 + .22 * i), half = w * .095;
                    c.saved([&] {
                        c.translate(x, y);
                        auto shell = c.path();
                        shell.moveTo({-half - 2, 0})
                            .cubicTo({-half, -4}, {-half * .7, -4.5}, {-half * .5, -2.6})
                            .cubicTo({-half * .35, -6}, {-half * .12, -6}, {0, -3.4})
                            .cubicTo({half * .12, -6}, {half * .35, -6}, {half * .5, -2.6})
                            .cubicTo({half * .7, -4.5}, {half, -4}, {half + 2, 0})
                            .cubicTo({half * .65, 4}, {half * .25, 8}, {0, 8})
                            .cubicTo({-half * .25, 8}, {-half * .65, 4}, {-half - 2, 0})
                            .close();
                        c.masked(shell, [&] {
                            c.gradientBox({-half - 2, -6, half * 2 + 4, 14}, 0, i == 1 ? 0xfff5e7ff : 0xebf9ffff,
                                          i == 1 ? 0xbed4e8ff : 0x90c7e0ff);
                        });
                        c.stroke(shell, 0xb99b6dff, {.width = .65});
                        for (int rib = -2; rib <= 2; ++rib) {
                            auto line = c.path();
                            line.moveTo({0, 6.3}).quadraticTo({rib * half * .22, 2}, {rib * half * .43, -1.5});
                            c.stroke(line, 0x6d9fc2a0, {.width = .55});
                        }
                        p.orb(c, {0, 6}, 1.7, i == 1 ? 1 : 0);
                    });
                }
                for (const auto& b : p.balls) {
                    const auto s = p.world->interpolated(b.body);
                    p.orb(c, {s.position.x * kUnits, s.position.y * kUnits}, b.radius * kUnits, b.color);
                }
            });
        }
    });
    p.targets();
    if (p.skin.pending()) {
        p.schedule();
    }
    return ok ? CD_OK : CD_ERROR_ARGUMENT;
}

void Instance::input(const CdInput& e) {
    auto& p = *impl_;
    if (p.frozen()) {
        return;
    }
    const auto r = p.interaction.input(e);
    auto size = p.skin.size();
    const double scale = std::min((p.safe.width - 14) / (size.width - 14), (p.safe.height - 14) / (size.height - 14));
    Point q{(e.x - p.safe.x - (p.safe.width - size.width * scale) / 2) / scale,
            (e.y - p.safe.y - (p.safe.height - size.height * scale) / 2) / scale};
    if (p.chime) {
        const Point target{std::clamp(q.x - 7, 0., size.width - 14) / 100,
                           std::clamp(q.y - 7, 0., size.height - 14) / 100};
        if (r.drag == DragPhase::Begin && r.target > 0 && r.target <= 6) {
            p.chimeModel->beginDrag(r.target - 1, target);
        } else if (r.drag == DragPhase::Update) {
            p.chimeModel->drag(target);
        } else if (r.drag == DragPhase::End || r.drag == DragPhase::Cancel || e.kind == CD_INPUT_CANCEL) {
            p.cancel();
        }
        if (e.kind == CD_INPUT_DOUBLE_CLICK) {
            p.chimeModel->pluck();
        }
        p.schedule();
        p.redraw.invalidate();
        return;
    }
    if (!p.chime) {
        q.x -= p.field.x;
        q.y -= p.field.y;
    }
    q.x /= kUnits;
    q.y /= kUnits;
    if (r.drag == DragPhase::Begin && r.target > 0 && r.target < 1000) {
        size_t i = r.target - 1;
        const auto body = i < p.balls.size() ? p.balls[i].body : p.pendulums.at(i - p.balls.size()).body;
        p.mouse = p.world->mouse(p.ground, body, q, 12, 4, .75);
    } else if (r.drag == DragPhase::Update && p.mouse.id) {
        p.world->mouseTarget(p.mouse, q);
    } else if (r.drag == DragPhase::End || r.drag == DragPhase::Cancel || e.kind == CD_INPUT_CANCEL) {
        p.cancel();
    }
    if (r.clicked == 1000) {
        for (const auto& b : p.balls) {
            const auto s = p.world->interpolated(b.body);
            const double dx = s.position.x - q.x, dy = s.position.y - q.y, d = std::max(.35, std::hypot(dx, dy));
            if (d < 3.5) {
                p.world->impulse(b.body, {dx / d * .025, -.045});
            }
        }
    }
    p.schedule();
    p.redraw.invalidate();
}

void Instance::layoutChanged(const CdLayout& l) {
    if (l.logical_width <= 14 || l.logical_height <= 14) {
        return;
    }
    auto& p = *impl_;
    p.cancel();
    p.safe = l.safe_content.width > 14 && l.safe_content.height > 14 ? l.safe_content
                                                                     : CdRect{0, 0, l.logical_width, l.logical_height};
    p.redraw.invalidate();
}

void Instance::setPaused(bool v) {
    auto& p = *impl_;
    p.hidden = v;
    p.cancel();
    p.audio.reset();
    p.audioStatus.reset();
    if (p.world) {
        p.world->paused(v);
    }
    p.skin.pause(v);
    p.updates.pause(v);
    p.redraw.pause(v);
    if (!v) {
        p.subscribe();
        p.redraw.invalidate();
    }
    p.schedule();
}

CdResult Instance::applySettings(const CdSettingValue* v, uint32_t n) {
    auto next = settings;
    const auto result = next.apply(v, n);
    if (result != CD_OK) {
        return result;
    }
    auto& p = *impl_;
    bool reset = !p.chime && next.at("balls").text != settings.at("balls").text;
    settings = std::move(next);
    p.cancel();
    if (reset) {
        p.reset();
    }
    p.subscribe();
    p.schedule();
    p.redraw.invalidate();
    return CD_OK;
}
}  // namespace deep_motion
