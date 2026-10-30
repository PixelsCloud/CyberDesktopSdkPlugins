#include <Windows.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <vector>

#include "AmbienceCatalog.h"
#include "Atmosphere.h"
#include "plugins/ambience-suite/NoiseScene.h"
#include "sdk/services/AmbientAudio.h"

namespace {
void check(bool ok, const char* why) {
    if (!ok) {
        throw std::runtime_error(why);
    }
}

void responsiveLayout() {
    using namespace atmosphere;
    using namespace cybercanvas;
    const Spec spec{"test", Kind::Noise, 2, 4};
    Canvas canvas;
    NoiseScene view({});
    auto settings = defaults(spec);
    Model model;
    for (double dpr : {1., 1.25, 1.5, 2., 3.}) {
        for (const auto size : {Size{144, 400}, {288, 144}, {288, 200}, {480, 300}}) {
            for (bool more : {false, true}) {
                model.noisePage = more;
                auto& scene = view.prepare(canvas, spec, settings, model, size.width, size.height, dpr);
                const auto bounds = [&](uint32_t key) {
                    return scene.bounds(scene.find(key));
                };
                check(bounds(10).width > 20, "Responsive noise layout lost master volume");
                for (uint32_t key : {1u, 3u, 4u, 10u, 20u, 21u, 22u, 23u, 24u, 25u}) {
                    const auto r = bounds(key);
                    if (r.width && r.height) {
                        check(r.x >= 0 && r.y >= 0 && r.x + r.width <= size.width + .01 &&
                                  r.y + r.height <= size.height + .01,
                              "Noise scene bounds outside card");
                        if (key >= 20) {
                            const bool expected =
                                more ? key == 22 || key == 25 : key == 20 || key == 21 || key == 23 || key == 24;
                            check(expected && r.y + r.height <= bounds(3).y + .01,
                                  "Track visibility or footer overlap");
                            CdInput press{sizeof(press),      CD_INPUT_PRESS,     CD_BUTTON_LEFT, 0,
                                          r.x + r.width * .5, r.y + r.height * .5};
                            const auto event = scene.input(press);
                            check(event.key == key && event.value && *event.value == 50,
                                  "Scene drawing/input geometry diverged");
                            scene.cancel();
                        }
                    }
                }
                const auto before = scene.stats();
                view.prepare(canvas, spec, settings, model, size.width, size.height, dpr);
                check(before.layouts == scene.stats().layouts && before.measurements == scene.stats().measurements,
                      "Unchanged noise state triggered layout or text measurement");
                model.master = .72f;
                view.prepare(canvas, spec, settings, model, size.width, size.height, dpr);
                check(before.layouts == scene.stats().layouts, "Noise volume change reflowed the scene");
            }
        }
    }
}

struct Host {
    struct Timer {
        CdTimerCallback callback;
        void* user;
        uint32_t interval;
    };

    std::map<uint64_t, Timer> timers;
    std::map<uint64_t, std::shared_ptr<CdAmbientSnapshot>> audio;
    inline static std::map<std::string, std::weak_ptr<CdAmbientSnapshot>> shared;
    uint64_t next = 1, now = GetTickCount64();
    int redraws = 0;
    CdServiceHostApi api{};

    Host() {
        api.input.media.base = {sizeof(api), CD_PLUGIN_ABI_VERSION, redraw, nullptr, nullptr, nullptr, stop, timer};
        api.query_service = query;
    }

    static void CD_CALL redraw(void* p) {
        ++static_cast<Host*>(p)->redraws;
    }

    static CdResult CD_CALL timer(void* p, uint32_t interval, CdTimerCallback cb, void* user, uint64_t* out) {
        auto& h = *static_cast<Host*>(p);
        *out = h.next++;
        h.timers.emplace(*out, Timer{cb, user, interval});
        return CD_OK;
    }

    static void CD_CALL stop(void* p, uint64_t token) {
        static_cast<Host*>(p)->timers.erase(token);
    }

    static CdResult CD_CALL open(void* p, const char* key, uint64_t* out) {
        auto& h = *static_cast<Host*>(p);
        std::shared_ptr<CdAmbientSnapshot> state;
        if (*key) {
            state = shared[key].lock();
        }
        if (!state) {
            state = std::make_shared<CdAmbientSnapshot>(CdAmbientSnapshot{
                sizeof(CdAmbientSnapshot), CD_AMBIENT_STOPPED, 0, 0, {sizeof(CdAmbientConfig), 30, .25f, {.6f}}, 0});
            if (*key) {
                if (!std::strcmp(key, "fire")) {
                    state->config.levels[0] = 0;
                    state->config.levels[4] = .6f;
                }
                shared[key] = state;
            }
        }
        *out = h.next++;
        h.audio.emplace(*out, state);
        return CD_OK;
    }

    static void CD_CALL close(void* p, uint64_t token) {
        static_cast<Host*>(p)->audio.erase(token);
    }

    static CdResult CD_CALL configure(void* p, uint64_t token, const CdAmbientConfig* value) {
        auto& h = *static_cast<Host*>(p);
        if (!h.audio.contains(token)) {
            return CD_ERROR_ARGUMENT;
        }
        h.audio.at(token)->config = *value;
        return CD_OK;
    }

    static CdResult CD_CALL command(void* p, uint64_t token, uint32_t action) {
        auto& h = *static_cast<Host*>(p);
        if (!h.audio.contains(token)) {
            return CD_ERROR_ARGUMENT;
        }
        h.audio.at(token)->status = action == CD_AMBIENT_PLAY    ? CD_AMBIENT_PLAYING
                                    : action == CD_AMBIENT_PAUSE ? CD_AMBIENT_PAUSED
                                                                 : CD_AMBIENT_STOPPED;
        return CD_OK;
    }

    static CdResult CD_CALL snapshot(void* p, uint64_t token, CdAmbientSnapshot* out) {
        auto& h = *static_cast<Host*>(p);
        if (!h.audio.contains(token)) {
            return CD_ERROR_ARGUMENT;
        }
        *out = *h.audio.at(token);
        return CD_OK;
    }

    static CdResult CD_CALL query(void*, const char* id, uint32_t version, void* out, uint32_t size) {
        if (std::strcmp(id, CD_SERVICE_AMBIENT) || version != 1 || size < sizeof(CdAmbientApi)) {
            return CD_ERROR_UNSUPPORTED;
        }
        *static_cast<CdAmbientApi*>(out) = {sizeof(CdAmbientApi), 1, open, close, configure, command, snapshot};
        return CD_OK;
    }

    void advance(uint64_t ms) {
        now += ms;
        const auto batch = timers;
        for (const auto& [token, value] : batch) {
            value.callback(value.user, now);
        }
    }
};

void animationLifecycle(const std::string& directory) {
    using namespace atmosphere;
    for (const auto kind : {Kind::Plant, Kind::Noise}) {
        const Spec spec{"animation-contract", kind, 2, 4};
        Host host;
        CdCreateContext context{sizeof(context),   CD_PLUGIN_ABI_VERSION,      spec.id, "motion",
                                directory.c_str(), &host.api.input.media.base, &host};
        CdInstanceApi api{};
        check(create(spec, &context, &api, sizeof(api)) == CD_OK, "Animation lifecycle create");
        CdLayout layout{};
        layout.struct_size = sizeof(layout);
        layout.logical_width = 288;
        layout.logical_height = 200;
        layout.safe_content = {4, 4, 280, 192};
        layout.device_pixel_ratio = 1;
        api.layout_changed(api.instance, &layout);
        std::vector<uint8_t> pixels(288 * 200 * 4);
        CdFrame frame{sizeof(frame), 288, 200, 288 * 4, pixels.size(), pixels.data(), 1};
        const auto render = [&] {
            check(api.render(api.instance, &frame) == CD_OK, "Animation lifecycle render");
            for (int attempt = 0; attempt < 1000; ++attempt) {
                const auto pending = std::find_if(host.timers.begin(), host.timers.end(), [](const auto& item) {
                    return item.second.interval == 33;
                });
                if (pending == host.timers.end()) {
                    break;
                }
                const auto timer = pending->second;
                timer.callback(timer.user, host.now);
                Sleep(1);
            }
            check(api.render(api.instance, &frame) == CD_OK, "Settled animation lifecycle render");
            return pixels;
        };
        const auto change = [&](const CdSettingValue& value) {
            check(api.apply_settings(api.instance, &value, 1) == CD_OK, "Animation lifecycle settings");
        };
        const auto click = [&](double x, double y) {
            CdInput event{sizeof(event), CD_INPUT_PRESS, CD_BUTTON_LEFT, 0, x + 4, y + 4, 0};
            api.input(api.instance, &event);
            event.kind = CD_INPUT_RELEASE;
            event.buttons = 0;
            api.input(api.instance, &event);
            event.kind = CD_INPUT_LEAVE;
            api.input(api.instance, &event);
        };
        if (kind == Kind::Plant) {
            change({sizeof(CdSettingValue), CD_SETTING_TOGGLE, "animation", 0});
            check(host.timers.empty(), "Static ornamental plant retained a timer");
            const auto idle = render();
            const int quiet = host.redraws;
            host.advance(5000);
            check(host.redraws == quiet && render() == idle, "Static plant requested a frame");
            click(140, 160);
            check(host.timers.size() == 1 && host.timers.begin()->second.interval == 50,
                  "Watering did not start transient frames with continuous animation disabled");
            const auto watered = render();
            host.advance(50);
            host.advance(100);
            check(render() != watered, "Watering feedback did not advance");
            for (int i = 0; i < 80; ++i) {
                host.advance(50);
            }
            check(host.timers.empty() && render() == idle,
                  "Settled watering did not return to the idle pose/timer state");
            click(140, 160);
            host.advance(50);
            host.advance(100);
        } else {
            check(host.timers.size() == 1 && host.timers.begin()->second.interval == 1000,
                  "Stopped noise needs only a service poll");
            const auto idle = render();
            const int quiet = host.redraws;
            host.advance(1000);
            check(host.redraws == quiet && render() == idle, "Stopped noise kept redrawing");
            const cybercanvas::Rect play{240, 6, 34, 34};
            const auto pressPlay = [&] {
                click(play.x + play.width / 2, play.y + play.height / 2);
            };
            pressPlay();
            check(host.timers.size() == 1 && host.timers.begin()->second.interval == 50,
                  "Playback did not enable breathing frames");
            const auto start = render();
            for (int i = 0; i < 20; ++i) {
                host.advance(50);
            }
            check(render() != start, "Playing sound artwork did not breathe");
            pressPlay();
            check(host.timers.begin()->second.interval == 1000 && render() == idle,
                  "Paused noise did not restore neutral artwork");
            const int pausedRedraws = host.redraws;
            host.advance(1000);
            check(host.redraws == pausedRedraws, "Paused noise kept requesting animation frames");
            pressPlay();
            change({sizeof(CdSettingValue), CD_SETTING_TOGGLE, "animation", 0});
            const auto disabled = render();
            const int disabledRedraws = host.redraws;
            host.advance(1000);
            check(
                host.timers.begin()->second.interval == 1000 && host.redraws == disabledRedraws && render() == disabled,
                "Reduced-motion noise did not remain static");
            change({sizeof(CdSettingValue), CD_SETTING_TOGGLE, "animation", 1});
            change({sizeof(CdSettingValue), CD_SETTING_CHOICE, "frame_rate", 0, 0, "30"});
            check(host.timers.size() == 1 && host.timers.begin()->second.interval == 34,
                  "Noise frame rate change did not replace its timer");
            change({sizeof(CdSettingValue), CD_SETTING_INTEGER, "master", 0});
            check(host.timers.begin()->second.interval == 1000, "Muted noise retained its animation timer");
            change({sizeof(CdSettingValue), CD_SETTING_INTEGER, "master", 25});
            check(host.timers.begin()->second.interval == 34, "Unmuting did not resume animation");
            host.advance(34);
            host.advance(100);
        }
        const auto beforeHide = render();
        api.set_paused(api.instance, 1);
        const int hiddenRedraws = host.redraws;
        host.advance(3600000);
        check(host.timers.empty() && host.redraws == hiddenRedraws,
              "Hidden animation retained a subscription or redraw");
        if (kind == Kind::Noise) {
            check(host.audio.begin()->second->status == CD_AMBIENT_PLAYING, "Hiding animation stopped the sound");
        }
        api.set_paused(api.instance, 0);
        check(render() == beforeHide, "Showing the card jumped its pose");
        host.advance(50);
        check(render() == beforeHide, "First resumed tick included hidden time");
        host.advance(100);
        check(render() != beforeHide, "Resumed animation did not continue after its new anchor");
        api.destroy(api.instance);
        check(host.timers.empty() && host.audio.empty(), "Animation lifecycle leaked resources");
    }
}
}  // namespace

int main() {
    try {
        using namespace atmosphere;
        Model model;
        model.plantedAt = 100000;
        check(model.growth(100000, false) == 1 && model.growth(100000, true) == .15, "Growth mode defaults");
        check(model.growth(100000 + 7 * 86400, true) == 1, "Offline growth");
        check(model.growth(99000, true) == .15, "Clock rollback");
        model.water(100001);
        const auto bonus = model.bonus;
        model.water(100002);
        check(model.bonus == bonus && model.waterFeedback, "Watering cooldown");
        model.click(20, 30);
        check(!model.waterFeedback, "Touch feedback incorrectly emitted water");
        model.advance(1, .75);
        check(model.motion.seconds() == .1875, "Animation delta bound");

        Model breeze;
        breeze.motion.seek(1.2);
        const double initialSway = breeze.sway();
        breeze.advance(.1, .75, false);
        check(breeze.sway() == initialSway, "Paused plant wind advanced");
        breeze.advance(.1, 0);
        check(breeze.sway() == initialSway, "Zero-speed plant wind advanced");
        breeze.motion.seek(6.8);
        check(std::abs(breeze.sway() - initialSway) > .001, "Plant wind repeated the old fixed sway cycle");
        for (int i = 0; i < 200; ++i) {
            breeze.motion.seek(i * .13);
            check(std::abs(breeze.sway()) <= 1, "Plant wind exceeded sway bounds");
        }

        const auto source = std::filesystem::path(__FILE__).parent_path().parent_path();
        const auto utf8 = source.u8string();
        const std::string directory(reinterpret_cast<const char*>(utf8.data()), utf8.size());
        Renderer renderer(source.wstring());
        int renders = 0;
        for (const auto& item : catalog) {
            const auto spec = item.spec;
            auto settings = defaults(spec);
            check(spec.kind == Kind::Noise || settings.at("speed").number == .75, "Default speed");
            for (const char* variant : spec.kind == Kind::Plant
                                           ? std::initializer_list<const char*>{"pothos", "succulent", "daisy"}
                                           : std::initializer_list<const char*>{"noise"}) {
                if (spec.kind != Kind::Noise) {
                    const CdSettingValue change{sizeof(change), CD_SETTING_CHOICE, "species", 0, 0, variant};
                    check(settings.apply(&change, 1) == CD_OK, "Variant selection");
                }
                for (double dpi : {1., 1.5, 2.}) {
                    for (int cellHeight : {72, 100, 150}) {
                        const uint32_t width = uint32_t(spec.columns * 72 * dpi),
                                       height = uint32_t(spec.rows * cellHeight * dpi);
                        std::vector<uint8_t> pixels(size_t(width) * height * 4);
                        CdFrame frame{sizeof(frame), width, height, width * 4, pixels.size(), pixels.data(), dpi};
                        for (bool dark : {false, true}) {
                            for (int background : {0, 1}) {
                                const CdSettingValue change{sizeof(change), CD_SETTING_TOGGLE, "show_background",
                                                            background};
                                check(settings.apply(&change, 1) == CD_OK, "Background");
                                Model pose;
                                pose.plantedAt = wallSeconds();
                                pose.dark = dark;
                                pose.motion.seek(1.4);
                                pose.waterFeedback = true;
                                pose.wateredAt = wallSeconds();
                                pose.interaction.seek(.7);
                                pose.clickX = spec.columns * 36;
                                pose.clickY = spec.rows * cellHeight * .55;
                                check(renderer.render(spec, settings, pose, frame, {}), "DPI/theme/variant render");
                                for (int attempt = 0; attempt < 1000 && renderer.imagesPending(); ++attempt) {
                                    Sleep(1);
                                    renderer.pollImages();
                                }
                                check(!renderer.imagesPending() && renderer.render(spec, settings, pose, frame, {}),
                                      "Asynchronous artwork did not settle");
                                size_t ink = 0;
                                for (size_t i = 3; i < pixels.size(); i += 4) {
                                    ink += pixels[i] > 0;
                                    check(pixels[i - 1] <= pixels[i] && pixels[i - 2] <= pixels[i] &&
                                              pixels[i - 3] <= pixels[i],
                                          "Invalid premultiplied output");
                                }
                                check(ink > 25, "Empty widget");
                                const auto same = pixels;
                                check(renderer.render(spec, settings, pose, frame, {}) && pixels == same,
                                      "Unstable identical frame");
                                ++renders;
                            }
                        }
                    }
                }
            }
            Host host;
            CdCreateContext context{sizeof(context),   CD_PLUGIN_ABI_VERSION,      spec.id, "test",
                                    directory.c_str(), &host.api.input.media.base, &host};
            CdInstanceApi api{};
            check(create(spec, &context, &api, sizeof(api)) == CD_OK, "Instance creation");
            const int w = spec.columns * 72, h = spec.rows * 100;
            CdLayout layout{};
            layout.struct_size = sizeof(layout);
            layout.logical_width = w;
            layout.logical_height = h;
            layout.safe_content = {4, 4, double(w - 8), double(h - 8)};
            layout.device_pixel_ratio = 1;
            api.layout_changed(api.instance, &layout);
            const CdSettingValue invalid{sizeof(invalid), CD_SETTING_CHOICE, "lighting", 0, 0, "bad"};
            check(api.apply_settings(api.instance, &invalid, 1) == CD_ERROR_ARGUMENT, "Invalid setting accepted");
            uint64_t stateSize = 0;
            check(api.save_state(api.instance, nullptr, 0, &stateSize) == CD_OK, "State sizing");
            std::vector<uint8_t> state(size_t(stateSize), 0);
            check(api.save_state(api.instance, state.data(), state.size(), &stateSize) == CD_OK &&
                      api.restore_state(api.instance, state.data(), state.size()) == CD_OK,
                  "State roundtrip");
            if (spec.kind == Kind::Plant) {
                state[0] = 'X';
                check(api.restore_state(api.instance, state.data(), state.size()) == CD_ERROR_ARGUMENT,
                      "Corrupt plant state");
                const auto click = [&](bool drag) {
                    CdInput input{sizeof(input), CD_INPUT_PRESS, CD_BUTTON_LEFT, 0, w * .5, h * .8, 0};
                    api.input(api.instance, &input);
                    if (drag) {
                        input.kind = CD_INPUT_MOVE;
                        input.x += 12;
                        api.input(api.instance, &input);
                    }
                    input.kind = CD_INPUT_RELEASE;
                    input.buttons = 0;
                    api.input(api.instance, &input);
                };
                click(true);
                api.save_state(api.instance, state.data(), state.size(), &stateSize);
                uint64_t watered = 0;
                std::memcpy(&watered, state.data() + 12, 8);
                check(!watered, "Tile drag watered plant");
                click(false);
                api.save_state(api.instance, state.data(), state.size(), &stateSize);
                std::memcpy(&watered, state.data() + 12, 8);
                check(watered, "Water click missing");
            } else if (spec.kind == Kind::Noise) {
                check(host.audio.size() == 1 && host.audio.begin()->second->status == CD_AMBIENT_STOPPED,
                      "Noise started without user action");
                const CdSettingValue mix[]{{sizeof(CdSettingValue), CD_SETTING_INTEGER, "rain", 42},
                                           {sizeof(CdSettingValue), CD_SETTING_INTEGER, "ocean", 23}};
                check(api.apply_settings(api.instance, mix, 2) == CD_OK, "Custom mix apply");
                CdSettingValue restored[]{{sizeof(CdSettingValue), CD_SETTING_INTEGER, "rain", 42},
                                          {sizeof(CdSettingValue), CD_SETTING_INTEGER, "ocean", 23},
                                          {sizeof(CdSettingValue), CD_SETTING_CHOICE, "preset", 0, 0, "custom"}};
                check(api.apply_settings(api.instance, restored, 3) == CD_OK &&
                          std::abs(host.audio.begin()->second->config.levels[0] - .42f) < .001f &&
                          std::abs(host.audio.begin()->second->config.levels[1] - .23f) < .001f,
                      "Settings restore reset custom mix");
                Host::command(&host, host.audio.begin()->first, CD_AMBIENT_PLAY);
            }
            std::vector<uint8_t> pixels(size_t(w) * h * 4);
            CdFrame frame{sizeof(frame), uint32_t(w), uint32_t(h), uint32_t(w) * 4, pixels.size(), pixels.data(), 1};
            check(api.render(api.instance, &frame) == CD_OK, "Live render");
            host.advance(100);
            check(api.render(api.instance, &frame) == CD_OK, "Live animation render");
            const auto beforePause = pixels;
            api.set_paused(api.instance, 1);
            check(host.timers.empty(), "Hidden animation timer retained");
            if (spec.kind == Kind::Noise) {
                check(host.audio.begin()->second->status == CD_AMBIENT_PLAYING, "Hiding stopped background sound");
            }
            api.set_paused(api.instance, 0);
            check(host.timers.size() == 1, "Resume subscription count");
            check(api.render(api.instance, &frame) == CD_OK && pixels == beforePause, "Resume jumped animation pose");
            api.destroy(api.instance);
            check(host.timers.empty() && host.audio.empty(), "Instance resource leak");
        }
        responsiveLayout();
        animationLifecycle(directory);
        // Two noise cards may share one sound group and keep independent lifetimes.
        Host firstHost, secondHost;
        CdCreateContext firstContext{sizeof(firstContext), CD_PLUGIN_ABI_VERSION,           "first",   "a",
                                     directory.c_str(),    &firstHost.api.input.media.base, &firstHost};
        CdCreateContext secondContext{sizeof(secondContext),
                                      CD_PLUGIN_ABI_VERSION,
                                      "second",
                                      "b",
                                      directory.c_str(),
                                      &secondHost.api.input.media.base,
                                      &secondHost};
        CdInstanceApi first{}, second{};
        check(create({"first", Kind::Noise, 2, 4}, &firstContext, &first, sizeof(first)) == CD_OK,
              "Create first noise");
        check(create({"second", Kind::Noise, 1, 2}, &secondContext, &second, sizeof(second)) == CD_OK,
              "Create second noise");
        const CdSettingValue link{sizeof(link), CD_SETTING_CHOICE, "link", 0, 0, "rain"};
        check(first.apply_settings(first.instance, &link, 1) == CD_OK &&
                  second.apply_settings(second.instance, &link, 1) == CD_OK,
              "Join shared audio group");
        Host::command(&firstHost, firstHost.audio.begin()->first, CD_AMBIENT_PLAY);
        check(firstHost.audio.begin()->second == secondHost.audio.begin()->second &&
                  secondHost.audio.begin()->second->status == CD_AMBIENT_PLAYING,
              "Shared playback state");
        first.destroy(first.instance);
        check(secondHost.audio.begin()->second->status == CD_AMBIENT_PLAYING,
              "Removing one noise card stopped another");
        second.destroy(second.instance);
        std::cout
            << "PASS: growth, watering, drag cancellation, " << renders
            << " DPI/theme/background/variant renders, persistent state, idle/transient motion scheduling, breathing, "
               "hidden-time exclusion and shared audio group lifecycle\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
