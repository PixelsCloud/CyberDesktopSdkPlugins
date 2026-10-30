#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>

#include "plugins/physics-toys/Toys.h"

namespace {
void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
    std::cout << "PASS " << message << '\n';
}

struct Host {
    struct Timer {
        CdTimerCallback callback;
        void* user;
        uint32_t interval;
    };

    std::map<uint64_t, Timer> timers;
    uint64_t next = 1, now = GetTickCount64(), redraws = 0;
    bool captured = false;
    CdEnvironmentSnapshot environment{
        sizeof(CdEnvironmentSnapshot), 1, 1792362480000, now, 480, CD_ENV_THEME_LIGHT, "en-US"};
    CdServiceHostApi api{};

    Host() {
        api.input.media.base.struct_size = sizeof(api);
        api.input.media.base.abi_version = 1;
        api.input.media.base.request_redraw = [](void* context) {
            ++static_cast<Host*>(context)->redraws;
        };
        api.input.media.base.subscribe_timer = [](void* context, uint32_t interval, CdTimerCallback callback,
                                                  void* user, uint64_t* token) -> CdResult {
            auto& h = *static_cast<Host*>(context);
            *token = h.next++;
            h.timers[*token] = {callback, user, interval};
            return CD_OK;
        };
        api.input.media.base.unsubscribe = [](void* context, uint64_t token) {
            static_cast<Host*>(context)->timers.erase(token);
        };
        api.query_service = [](void* context, const char* service, uint32_t version, void* result,
                               uint32_t size) -> CdResult {
            if (version != 1) {
                return CD_ERROR_VERSION;
            }
            if (std::string_view(service) == CD_SERVICE_ENVIRONMENT && size >= sizeof(CdEnvironmentApi)) {
                *static_cast<CdEnvironmentApi*>(result) = {sizeof(CdEnvironmentApi), 1,
                                                           [](void* context, CdEnvironmentSnapshot* value) -> CdResult {
                                                               *value = static_cast<Host*>(context)->environment;
                                                               return CD_OK;
                                                           }};
                return CD_OK;
            }
            if (std::string_view(service) == CD_SERVICE_INTERACTION && size >= sizeof(CdInteractionApi)) {
                *static_cast<CdInteractionApi*>(result) = {sizeof(CdInteractionApi), 1,
                                                           [](void*, uint32_t) -> CdResult {
                                                               return CD_OK;
                                                           },
                                                           [](void* context, uint32_t value) -> CdResult {
                                                               static_cast<Host*>(context)->captured = value != 0;
                                                               return CD_OK;
                                                           }};
                return CD_OK;
            }
            return CD_ERROR_UNSUPPORTED;
        };
    }

    void advance(uint32_t ms) {
        now += ms;
        environment.monotonic_ms = now;
        environment.unix_ms += ms;
        const auto copy = timers;
        for (auto [id, timer] : copy) {
            if (timers.contains(id)) {
                timer.callback(timer.user, now);
            }
        }
    }
};

void lifecycle() {
    for (const auto kind : {toys::Kind::Chime, toys::Kind::Clock}) {
        Host host;
        CdCreateContext context{sizeof(CdCreateContext), 1, "test", "test", "", &host.api.input.media.base, &host};
        {
            toys::Instance instance(context, kind);
            const uint32_t w = kind == toys::Kind::Clock ? 360 : 260, h = kind == toys::Kind::Clock ? 185 : 290;
            std::vector<uint8_t> rgba(size_t(w) * h * 4);
            CdFrame frame{sizeof(CdFrame), w, h, w * 4, rgba.size(), rgba.data(), 1};
            CdLayout layout{};
            layout.struct_size = sizeof(layout);
            layout.logical_width = w;
            layout.logical_height = h;
            layout.safe_content = {0, 0, double(w), double(h)};
            instance.layoutChanged(layout);
            check(instance.render(frame) == CD_OK, "real instance renders");
            const auto first = rgba;
            instance.render(frame);
            check(rgba == first, "paint does not advance physics");
            const double x = kind == toys::Kind::Clock ? 48 : 70, y = kind == toys::Kind::Clock ? 107 : 149;
            instance.input({sizeof(CdInput), CD_INPUT_PRESS, CD_BUTTON_LEFT, 0, x, y});
            check(host.captured, "drag captures host pointer instead of moving tile");
            instance.input({sizeof(CdInput), CD_INPUT_MOVE, CD_BUTTON_LEFT, 0, x + 25, y + 12});
            for (int i = 0; i < 15; ++i) {
                host.advance(33);
                instance.render(frame);
            }
            check(rgba != first, "dragged physical piece changes output");
            instance.input({sizeof(CdInput), CD_INPUT_CANCEL});
            check(!host.captured, "lost capture cancels drag");
            instance.setPaused(true);
            check(host.timers.empty(), "hidden plugin has no timer");
            const auto hidden = rgba;
            host.advance(60000);
            instance.render(frame);
            // Time may refresh on explicit painting, but no physical catch-up occurs.
            instance.setPaused(false);
            check(host.timers.size() <= 1, "resume has only one update subscription");
            CdSettingValue transparent{sizeof(CdSettingValue), CD_SETTING_TOGGLE, "show_background", 0};
            check(instance.applySettings(&transparent, 1) == CD_OK, "transparent setting accepted");
            instance.render(frame);
            check(rgba[3] == 0, "transparent output clears background");
            CdSettingValue invalid{sizeof(CdSettingValue), CD_SETTING_NUMBER, "speed", 0, -10};
            check(instance.applySettings(&invalid, 1) == CD_ERROR_ARGUMENT, "invalid physics settings rejected");
            CdSettingValue reduced{sizeof(CdSettingValue), CD_SETTING_TOGGLE, "reduced_motion", 1};
            instance.applySettings(&reduced, 1);
            instance.render(frame);
            const auto still = rgba;
            for (int i = 0; i < 15; ++i) {
                host.advance(33);
                instance.render(frame);
            }
            check(rgba == still, "reduced motion preserves a static physical pose");
        }
        check(host.timers.empty() && !host.captured, "destroy releases timer and capture");
    }
}
}  // namespace

void randomWindTests() {
    using namespace toys;
    for (bool mechanical : {false, true}) {
        Model windy(Kind::Chime, false, mechanical);
        windy.setWindSeed(991);
        for (int i = 0; i < 3600; ++i) {
            windy.advance(1. / 60, 0, .75);
        }
        const auto resting = windy.state(0).position;
        double low = resting.x, high = resting.x;
        bool bounded = true;
        double minX = 100, maxX = -100, minY = 100, maxY = -100;
        for (int frame = 0; frame < 60 * 300; ++frame) {
            windy.advance(1. / 60, .55, .75);
            if (frame > 60 * 240) {
                low = std::min(low, windy.state(0).position.x);
                high = std::max(high, windy.state(0).position.x);
            }
            for (size_t i = 0; i < windy.pieces().size(); ++i) {
                const auto state = windy.state(i);
                minX = std::min(minX, state.position.x);
                maxX = std::max(maxX, state.position.x);
                minY = std::min(minY, state.position.y);
                maxY = std::max(maxY, state.position.y);
                bounded &= std::isfinite(state.angle) && std::isfinite(state.position.x) &&
                           std::isfinite(state.position.y) && state.position.x >= -.05 &&
                           state.position.x <= windy.size().width / 100 + .05 && state.position.y >= -.05 &&
                           state.position.y <= windy.size().height / 100 + .05;
            }
        }
        check(high - low > .08,
              "random breeze wakes a settled chime and sustains clearly visible motion after five minutes");
        std::cout << "wind bounds mechanical=" << mechanical << " x=" << minX << ".." << maxX << " y=" << minY << ".."
                  << maxY << " joints=" << windy.stats().joints << '\n';
        check(bounded && windy.stats().joints == 6,
              "long-running random gusts preserve widget bounds and all six joints");
        Model a(Kind::Chime, false, mechanical), b(Kind::Chime, false, mechanical),
            other(Kind::Chime, false, mechanical);
        a.setWindSeed(444);
        b.setWindSeed(444);
        other.setWindSeed(892);
        for (int i = 0; i < 360; ++i) {
            a.advance(1. / 30, .55, .75);
            b.advance(1. / 60, .55, .75);
            b.advance(1. / 60, .55, .75);
            other.advance(1. / 30, .55, .75);
        }
        check(std::hypot(a.state(0).position.x - b.state(0).position.x, a.state(0).position.y - b.state(0).position.y) <
                  1e-6,
              "equal random wind seeds give identical 30 and 60 Hz trajectories");
        check(std::hypot(a.state(0).position.x - other.state(0).position.x,
                         a.state(0).position.y - other.state(0).position.y) > .002,
              "independent wind seeds produce different gust trajectories");
    }
}

void mechanicalWindLifecycle() {
    const char* id = "com.cyberdesktop.physics.windchime.mechanical.2x2";
    const auto directory = (std::filesystem::path(CD_TOY_TEST_BINARY_ROOT) / "plugins" / id).string();
    Host host;
    CdCreateContext context{sizeof(CdCreateContext),    1,    id, "wind-lifecycle", directory.c_str(),
                            &host.api.input.media.base, &host};
    {
        toys::Instance instance(context, toys::Kind::Chime, true);
        std::vector<uint8_t> pixels(260 * 290 * 4);
        CdFrame frame{sizeof(CdFrame), 260, 290, 1040, pixels.size(), pixels.data(), 1};
        for (int i = 0; i < 50; ++i) {
            check(instance.render(frame) == CD_OK, "mechanical chime renders its actual packaged materials");
            Sleep(5);
        }
        const auto before = pixels;
        const auto redraws = host.redraws;
        for (int i = 0; i < 180; ++i) {
            host.advance(33);
            instance.render(frame);
        }
        check(pixels != before && host.redraws > redraws + 10,
              "default random wind visibly animates the real plugin without any click or drag");
        const CdSettingValue reduced{sizeof(CdSettingValue), CD_SETTING_TOGGLE, "reduced_motion", 1};
        check(instance.applySettings(&reduced, 1) == CD_OK, "mechanical chime accepts reduced motion");
        instance.render(frame);
        const auto still = pixels;
        for (int i = 0; i < 30; ++i) {
            host.advance(33);
            instance.render(frame);
        }
        check(host.timers.empty() && pixels == still, "reduced motion stops wind and preserves the displayed pose");
        const CdSettingValue resumed{sizeof(CdSettingValue), CD_SETTING_TOGGLE, "reduced_motion", 0};
        instance.applySettings(&resumed, 1);
        instance.setPaused(true);
        check(host.timers.empty(), "hidden mechanical chime releases physics and image timers");
        host.advance(600000);
        instance.setPaused(false);
        instance.render(frame);
        check(pixels == still, "resume preserves the pose without replaying ten minutes of offline wind");
        for (int i = 0; i < 60; ++i) {
            host.advance(33);
            instance.render(frame);
        }
        check(pixels != still, "wind resumes naturally after the widget becomes visible");
    }
    check(host.timers.empty() && !host.captured, "mechanical wind lifecycle leaves no host subscriptions");
}

int main() try {
    using namespace toys;
    lifecycle();
    randomWindTests();
    mechanicalWindLifecycle();
    for (const auto kind : {Kind::Chime, Kind::Clock}) {
        Model model(kind);
        check(model.pieces().size() == 6, "bounded physical pieces");
        const auto initial = model.state(0).position;
        model.pluck();
        for (int i = 0; i < 60; ++i) {
            model.advance(1. / 60, 0, 1);
        }
        auto moved = model.state(0).position;
        check(std::hypot(moved.x - initial.x, moved.y - initial.y) > .001, "impulse produces physical motion");
        const auto joints = model.stats().joints;
        model.beginDrag(0, {1.3, 1.4});
        for (int i = 0; i < 60; ++i) {
            model.drag({1.4, 1.6});
            model.advance(1. / 60, 0, 1);
        }
        check(model.stats().joints == joints + 1, "mouse joint exists during drag");
        model.cancelDrag();
        model.cancelDrag();
        check(model.stats().joints == joints, "cancel releases mouse joint exactly once");
        model.advance(std::numeric_limits<double>::quiet_NaN(), 0, 1);
        for (int i = 0; i < 1800; ++i) {
            model.advance(1. / 60, 0, 1);
        }
        bool bounded = true;
        for (size_t i = 0; i < model.pieces().size(); ++i) {
            const auto s = model.state(i);
            bounded &= std::isfinite(s.position.x) && std::isfinite(s.position.y) && s.position.x >= -.05 &&
                       s.position.x <= model.size().width / 100 + .05 && s.position.y >= -.05 &&
                       s.position.y <= model.size().height / 100 + .05;
        }
        check(bounded, "dragged bodies remain inside the widget");
        const auto before = model.state(0).position;
        for (int i = 0; i < 60; ++i) {
            model.advance(1. / 60, 1, 0);
        }
        check(std::hypot(model.state(0).position.x - before.x, model.state(0).position.y - before.y) < 1e-8,
              "zero speed freezes physics");
        model.reset(true);
        check(model.stats().joints == joints, "reset removes transient state");
    }
    Model clock(Kind::Clock);
    clock.setTime(0, 9, 5, true);
    check(clock.digits() == L"120905" && !clock.afternoon(), "midnight uses 12 AM with seconds");
    clock.setTime(12, 0, 0, true);
    check(clock.digits() == L"120000" && clock.afternoon(), "noon uses 12 PM");
    clock.setTime(23, 59, 59, false);
    check(clock.digits() == L"235959", "24-hour clock is accurate to the second");
    check(!clock.setTime(23, 59, 59, false), "same second does not retrigger animation");
    clock.setTime(0, 0, 0, false);
    check(clock.digits() == L"000000", "midnight carries all six digits");
    for (bool compact : {false, true}) {
        Model animated(Kind::Clock, compact), control(Kind::Clock, compact);
        animated.setTime(10, 28, 8, false);
        control.setTime(10, 28, 8, false);
        for (int i = 0; i < 300; ++i) {
            animated.advance(1. / 60, 0, .75);
            control.advance(1. / 60, 0, .75);
        }
        animated.setTime(10, 28, 9, false);
        control.setTime(10, 28, 9, false, false);
        for (int i = 0; i < 12; ++i) {
            animated.advance(1. / 60, 0, .75);
            control.advance(1. / 60, 0, .75);
        }
        check(std::abs(animated.state(5).position.y - control.state(5).position.y) > .005,
              "seconds digit visibly springs in regular and compact clocks");
        check(std::abs(animated.state(0).position.y - control.state(0).position.y) < .000001,
              "unchanged hour digit receives no seconds impulse");
        animated.setTime(10, 28, 10, false);
        check(animated.digits() == L"102810", "seconds carry updates both seconds cards");
    }
    Model a(Kind::Chime), b(Kind::Chime);
    for (int i = 0; i < 360; ++i) {
        a.advance(1. / 30, 0, 1);
        b.advance(1. / 60, 0, 1);
        b.advance(1. / 60, 0, 1);
    }
    check(
        std::hypot(a.state(0).position.x - b.state(0).position.x, a.state(0).position.y - b.state(0).position.y) < 1e-6,
        "fixed-step trajectory is frame-rate independent");
    return 0;
} catch (const std::exception& e) {
    std::cerr << "FAIL " << e.what() << '\n';
    return 1;
}
