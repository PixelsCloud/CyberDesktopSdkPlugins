#include <sdk/Controls.h>
#include <sdk/Interaction.h>

#include <cmath>
#include <format>

#include "plugins/mecha-desk/Ambience.h"
#include "plugins/mecha-desk/Skin.h"
#include "sdk/services/AmbientAudio.h"

namespace aeris_ambience {
using namespace cybercanvas;

namespace {
#include "plugins/mecha-desk/PlantFoliage.inl"

constexpr const wchar_t* kChinese[]{L"雨声", L"海浪", L"溪流", L"林间", L"炉火", L"风声"};
constexpr const wchar_t* kEnglish[]{L"Rain", L"Ocean", L"Stream", L"Forest", L"Fire", L"Wind"};

unsigned trackAt(Spec spec, const cdsdk::Settings& settings, const Model& model, unsigned row) {
    if (spec.rows == 1) {
        return unsigned(settings.at("primary").integer);
    }
    constexpr unsigned pages[2][4]{{0, 1, 3, 4}, {2, 5, 3, 4}};
    return pages[model.noisePage ? 1 : 0][std::min(row, 3u)];
}
}  // namespace

struct Renderer::Impl {
    aerisskin::Skin skin;
    cdsdk::Interaction interaction;
    cdsdk::Controls plantControls;
    std::function<void(SceneEvent)> action;

    explicit Impl(const std::filesystem::path& p) : skin(p / "assets") {}

    void arrange(Spec spec, const cdsdk::Settings& settings, const Model& model, CdRect safe) {
        std::vector<HitRegion> regions;
        const auto add = [&](uint32_t key, std::string name, bool slider = false) {
            auto r = skin.displayRegion(name, safe);
            if (slider && skin.terminal()) {
                const double padding = std::max(0., 10. - r.height) / 2;
                r.y -= padding;
                r.height += padding * 2;
            }
            r.x += safe.x;
            r.y += safe.y;
            if (r.width > 0 && r.height > 0) {
                regions.push_back(
                    {key, HitShape::rectangle(r), slider ? PointerBehavior::Drag : PointerBehavior::Button, 1});
            }
        };
        if (spec.rows > 1 || !model.noisePage) {
            for (unsigned i = 0; i < (spec.rows == 1 ? 1 : 4); ++i) {
                const unsigned track = trackAt(spec, settings, model, i);
                add(100 + track, "toggle" + std::to_string(i));
                add(20 + track, "slider" + std::to_string(i), true);
            }
        }
        if (spec.rows > 1 || model.noisePage) {
            add(10, "master", true);
            add(1, "play");
            add(3, "timer");
        }
        add(4, "page");
        if (spec.rows == 1 && !model.noisePage) {
            add(2, "source");
        }
        interaction.regions(std::move(regions));
    }
};

Renderer::Renderer(std::wstring p) : impl_(std::make_unique<Impl>(p)) {}

Renderer::~Renderer() = default;

void Renderer::bindHost(const CdCreateContext& c) {
    impl_->skin.bind(c);
    impl_->interaction.bind(c);
}

void Renderer::bindPlant(const CdCreateContext& context, std::function<void(uint32_t)> action) {
    impl_->plantControls.bind(context, std::move(action));
}

bool Renderer::plantInput(Spec, const cdsdk::Settings&, CdRect safe, const CdInput& input) {
    impl_->plantControls.items({{1, impl_->skin.displayRegion("pot", safe), "Water plant"},
                                {2, impl_->skin.displayRegion("care", safe), "Water plant"}},
                               {safe.x, safe.y});
    impl_->plantControls.input(input);
    // Every plant event is handled here: transparent space never waters the pot.
    return true;
}

void Renderer::onInvalidate(std::function<void()> f) {
    impl_->skin.onChange(std::move(f));
}

void Renderer::onAction(std::function<void(SceneEvent)> f) {
    impl_->action = std::move(f);
}

void Renderer::onDiagnostics(std::function<void(cdsdk::InstanceStats&)>) {}

void Renderer::cancelInput() {
    impl_->interaction.cancel();
    impl_->plantControls.cancel();
}

bool Renderer::advanceFeedback(double) {
    return false;
}

bool Renderer::feedbackActive() const {
    return false;
}

bool Renderer::imagesPending() const {
    return impl_->skin.pending();
}

bool Renderer::pollImages() {
    return impl_->skin.poll();
}

void Renderer::pauseImages(bool p) {
    impl_->skin.pause(p);
    if (p) {
        cancelInput();
    }
}

SceneEvent Renderer::noiseInput(Spec spec, const cdsdk::Settings& settings, const Model& model, CdRect safe,
                                const CdInput& e) {
    impl_->arrange(spec, settings, model, safe);
    auto r = impl_->interaction.input(e);
    SceneEvent result;
    result.key = r.clicked ? r.clicked : r.target;
    result.clicked = r.clicked != 0;
    if ((r.target == 10 || (r.target >= 20 && r.target < 26)) && r.drag != DragPhase::None &&
        r.drag != DragPhase::Cancel) {
        unsigned row = 0;
        if (spec.rows > 1 && r.target != 10) {
            for (unsigned i = 0; i < 4; ++i) {
                if (trackAt(spec, settings, model, i) == r.target - 20) {
                    row = i;
                    break;
                }
            }
        }
        const auto b = impl_->skin.displayRegion(r.target == 10 ? "master" : "slider" + std::to_string(row), safe);
        const double knob = std::min(4.5, b.height * .36);
        result.value = 100 * std::clamp((e.x - safe.x - b.x - knob) / std::max(1., b.width - 2 * knob), 0., 1.);
    }
    return result;
}

bool Renderer::render(Spec spec, const cdsdk::Settings& settings, const Model& m, const CdFrame& f, CdRect safe) {
    auto& skin = impl_->skin;
    skin.appearance(settings);
    return skin.render(f, safe, [&](auto& c) {
        const bool en = settings.at("language").text == "en";
        if (spec.kind == Kind::Plant) {
            const auto area = skin.region("plant");
            const bool garden = spec.columns >= 4 && settings.at("garden").integer;
            const auto species = settings.at("species").text;
            const int type = species == "succulent" ? 1 : species == "daisy" ? 2 : 0;
            const double growth = m.growth(wallSeconds(), settings.at("mode").text == "grow");
            c.clipped(area, [&] {
                if (skin.terminal() && type == 0) {
                    c.saved([&] {
                        c.translate(area.x + area.width / 2, area.y + area.height);
                        c.rotate(settings.at("animation").integer ? std::sin(m.motion.seconds() * .65) * 1.2 : 0);
                        const double factor = .55 + .45 * growth;
                        c.scale(factor, factor);
                        skin.part(c, "foliage", {-area.width / 2, -area.height, area.width, area.height}, true);
                    });
                    return;
                }
                for (unsigned i = 0; i < (garden ? 3u : 1u); ++i) {
                    const double width = garden ? area.width / 3 : area.width;
                    foliage(c, area.x + width * (i + .5), area.y + area.height * .88,
                            std::min(area.height * .98, width * (garden ? 1.45 : 1.05)), garden ? int(i) : type, m,
                            growth, m.dark);
                }
            });
            const auto left = skin.region("plantLeft");
            if (skin.terminal() && left.width > 0) {
                skin.part(c, "foliageLeft", left, true);
            }
            // The approved enamel pot sits in front of the live stems, and never sways.
            skin.part(c, "pot", skin.region("pot"), true);
            skin.button(c, skin.region("care"), "droplet", m.waterOpacity() > 0);
            if (settings.at("show_info").integer && (spec.rows > 1 || skin.terminal())) {
                constexpr const wchar_t* zh[]{L"绿萝", L"多肉", L"雏菊"};
                constexpr const wchar_t* names[]{L"Pothos", L"Succulent", L"Daisy"};
                const auto state = m.waterOpacity() > 0                       ? (en ? L"Watered" : L"已浇水")
                                   : settings.at("mode").text == "ornamental" ? (en ? L"Ornamental" : L"观赏")
                                   : growth >= .99                            ? (en ? L"Mature" : L"已长成")
                                   : growth < .34                             ? (en ? L"Seedling" : L"幼苗")
                                                                              : (en ? L"Growing" : L"生长中");
                const auto info = skin.region("info");
                c.gradientBox(info, 2, skin.terminal() ? 0x081215ff : 0xfffcf4f4,
                              skin.terminal() ? 0x101c20ff : 0xe6f2f6f4);
                if (skin.terminal()) {
                    if (spec.columns == 1) {
                        // This badge is only about 20px wide. A growth bar communicates
                        // the state without squeezing four glyphs into microprint.
                        skin.progress(c, {info.x, info.y + (info.height - 4) / 2, info.width, 4}, growth);
                    } else {
                        const double size = std::max(15., info.height * .85);
                        skin.metric(c, std::to_wstring(int(std::round(growth * 100))), L"%", info, size, size * .48,
                                    0xf3f8faff);
                    }
                } else {
                    skin.caption(c,
                                 std::wstring(garden ? (en ? L"Garden" : L"花园")
                                              : en   ? names[type]
                                                     : zh[type]) +
                                     (spec.columns <= 2 ? L"" : std::wstring(L" · ") + state),
                                 info, info.height * .72);
                }
            }
            if (m.waterOpacity() > 0) {
                const auto r = skin.region("plant").width > 0 ? skin.region("plant") : skin.region("pot");
                for (int i = 0; i < 5; ++i) {
                    const double t = std::fmod(m.reaction() + i * .19, 1.);
                    c.oval(r.x + r.width * (.3 + i * .1), r.y + r.height * (.35 + .5 * t), 1, 1.6,
                           0x8ad9f000 | uint32_t(m.waterOpacity() * 180));
                }
            }
            return;
        }
        impl_->arrange(spec, settings, m, safe);
        const auto transport = skin.region("transport");
        if (skin.terminal() && transport.width > 0) {
            c.gradientBox(transport, 4, 0x081215e8, 0x101c20e8);
        }
        const auto name = [&](unsigned track) {
            return en ? kEnglish[track] : kChinese[track];
        };
        if (spec.rows > 1 || !m.noisePage) {
            for (unsigned i = 0; i < (spec.rows == 1 ? 1 : 4); ++i) {
                const unsigned track = trackAt(spec, settings, m, i);
                const auto row = skin.region("row" + std::to_string(i));
                const auto toggle = skin.region("toggle" + std::to_string(i));
                const auto slider = skin.region("slider" + std::to_string(i));
                const bool on = m.audioStatus == CD_AMBIENT_PLAYING && m.levels[track] > 0;
                skin.caption(c, name(track), row, skin.terminal() ? 13 : spec.rows == 1 ? 11 : 8);
                if (skin.terminal()) {
                    constexpr const char* symbols[]{"cloud-rain", "waves", "droplet", "trees", "flame", "wind"};
                    skin.button(c, toggle, spec.rows == 1 ? (on ? "pause" : "play") : symbols[track], on);
                } else if (spec.rows == 1) {
                    skin.icon(c, {toggle.x + 3, toggle.y + 3, toggle.width - 6, toggle.height - 6},
                              on ? "pause" : "play", 0xd9f0e8ff);
                } else {
                    constexpr const char* symbols[]{"cloud-sun", "wind", "droplet", "music-2", "sun", "wind"};
                    skin.icon(c,
                              {toggle.x + toggle.width * .35, toggle.y + toggle.height * .05, toggle.width * .30,
                               toggle.height * .46},
                              symbols[track], on ? 0x529bacff : 0x273e4dff);
                    if (on) {
                        c.box(toggle.x + toggle.width * .3, toggle.y + toggle.height - 1, toggle.width * .4, 1.2, .6,
                              0x529bacff);
                    }
                }
                skin.progress(c, slider, m.levels[track]);
            }
        }
        const bool playing = m.audioStatus == CD_AMBIENT_PLAYING;
        if (spec.rows > 1 || m.noisePage) {
            const auto play = skin.region("play");
            skin.icon(c, {play.x + 2, play.y + 2, play.width - 4, play.height - 4}, playing ? "pause" : "play",
                      0xd9f0e8ff);
            skin.progress(c, skin.region("master"), m.master);
            const auto timer = skin.region("timer");
            const auto remaining = m.deadline ? std::max<int64_t>(0, int64_t(m.deadline) - int64_t(wallSeconds()))
                                              : settings.at("timer_minutes").integer * 60;
            skin.caption(
                c,
                remaining > 0 ? std::format(L"{:02}:{:02}", remaining / 60, remaining % 60) : (en ? L"Off" : L"关闭"),
                timer, skin.terminal() ? 12 : timer.height * .58);
        }
        const auto page = skin.region("page");
        if (skin.terminal() && spec.rows == 1) {
            skin.button(c, page, m.noisePage ? "chevron-left" : "sliders-horizontal");
            if (!m.noisePage) {
                skin.button(c, skin.region("source"), "list-music");
            }
        } else if (skin.terminal()) {
            skin.button(c, page, m.noisePage ? "chevron-left" : "chevron-right");
        } else if (spec.rows > 1) {
            skin.icon(c, {page.x + 3, page.y + 3, page.width - 6, page.height - 6},
                      m.noisePage ? "chevron-left" : "chevron-right", 0xd9f0e8ff);
        }
        // Sound scenery responds to the selected track, without moving the frame or controls.
        if (settings.at("animation").integer && playing) {
            const auto r = skin.region("row0");
            const auto scenery = skin.region("scenery");
            const double x = skin.terminal() ? scenery.x + scenery.width / 2 : spec.rows == 1 ? 42 : 155;
            const double y = skin.terminal() ? scenery.y + scenery.height / 2 : spec.rows == 1 ? 56 : 140;
            const unsigned track = unsigned(settings.at("primary").integer);
            for (unsigned i = 0; i < 4; ++i) {
                const double t = m.motion.seconds() + i * 1.57;
                c.oval(x + std::sin(t * .7) * 5, y + std::cos(t) * 4, .55 + (track == 4 ? .35 : 0), .8,
                       0x92d5f000 | uint32_t(90 + 50 * std::sin(t)));
            }
        }
        if (m.audioError) {
            const auto r = skin.region("error");
            c.gradientBox(r, 2, 0xfff6edff, 0xf1dcc6ff);
            c.strokeBox(r, 2, 0xc5a37fff, {.width = .4});
            skin.caption(c, en ? L"Audio error" : L"音频异常", r, 6.5, 0x935b43ff);
        }
    });
}

bool Renderer::save(const std::wstring& p, const CdFrame& f) {
    return impl_->skin.save(p, f);
}
}  // namespace aeris_ambience
