#include <CyberCanvas/Animation.h>

#include <cmath>
#include <iostream>
#include <limits>

namespace {
template <typename Action>
bool rejected(Action&& action) {
    try {
        action();
        return false;
    } catch (const std::exception&) {
        return true;
    }
}
}  // namespace

int main() {
    using namespace cybercanvas;
    bool passed = true;
    const auto check = [&](const char* name, bool result) {
        passed &= result;
        std::cout << (result ? "PASS " : "FAIL ") << name << '\n';
    };
    try {
        AnimationClock clock;
        check("first host tick establishes an anchor", clock.tick(1000).delta == 0);
        check("host milliseconds advance seconds", std::abs(clock.tick(1100).seconds - .1) < 1e-12);
        check("duplicate ticks do not advance", clock.tick(1100).delta == 0);
        check("clock rollback does not underflow", clock.tick(900).delta == 0 && clock.tick(950).delta == .05);
        const auto beforeSpeed = clock.seconds();
        clock.setSpeed(.75);
        check("speed changes preserve the existing pose", clock.seconds() == beforeSpeed);
        check("speed scales the next delta", std::abs(clock.tick(1050).delta - .075) < 1e-12);
        const auto beforePause = clock.seconds();
        clock.setPaused(true);
        clock.tick(1000000);
        clock.setPaused(false);
        check("hidden time is excluded on resume", clock.tick(2000000).seconds == beforePause);
        check("resumed clocks continue at their prior speed", std::abs(clock.tick(2000100).delta - .075) < 1e-12);
        clock.setSpeed(0);
        check("zero speed stops motion without losing the host delta",
              clock.tick(2000200).delta == 0 && clock.tick(2000300).unscaledDelta == .1);
        clock.setSpeed(1);
        check("long stalls have a bounded advance", clock.tick(3000000).delta == .25);
        const auto beforeAnchor = clock.seconds();
        clock.resetAnchor();
        check("rebasing after idle does not change the accumulated time", clock.tick(5000000).seconds == beforeAnchor);
        check("invalid timing input is rejected", rejected([&] {
                                                      clock.advance(-1);
                                                  }) &&
                                                      rejected([&] {
                                                          clock.setSpeed(std::numeric_limits<double>::quiet_NaN());
                                                      }) &&
                                                      rejected([&] {
                                                          clock.setMaximumStep(0);
                                                      }));
        for (auto ease :
             {Ease::Linear, Ease::InCubic, Ease::OutCubic, Ease::InOutCubic, Ease::InOutSine, Ease::OutBack}) {
            check("easing endpoints remain exact", eased(0, ease) == 0 && eased(1, ease) == 1);
        }
        check("back easing retains intentional overshoot", eased(.7, Ease::OutBack) > 1);
        Tween<double> once(0, 10, {.duration = 2, .easing = Ease::InOutSine});
        check("one-shot tracks finish at the destination and stop requesting frames",
              once.sample(0).value == 0 && once.sample(1).active && once.sample(2).finished && !once.sample(2).active &&
                  once.sample(200).value == 10);
        Tween<double> loop(0, 10, {.duration = 1, .repeat = Repeat::Loop, .cycles = 2});
        check("finite loops restart and finish at the final destination",
              std::abs(loop.sample(1.25).value - 2.5) < 1e-6 && loop.sample(2).finished && loop.sample(2).value == 10);
        Tween<double> ping(0, 10, {.duration = 1, .repeat = Repeat::PingPong, .cycles = 0});
        check("infinite ping-pong repeats complete round trips",
              ping.sample(0).value == 0 && ping.sample(1).value == 10 && ping.sample(2).value == 0 &&
                  ping.sample(200).active && ping.sample(1.5).value == 5);
        Tween<double> finitePing(0, 10, {.duration = 1, .repeat = Repeat::PingPong, .cycles = 2});
        check("finite ping-pong ends at the starting pose",
              finitePing.sample(4).finished && finitePing.sample(4).value == 0);
        Tween<double> absolute(0, 100, {.duration = 10});
        double reference = -1;
        for (int fps : {30, 60, 120}) {
            AnimationClock paced;
            paced.setSpeed(.75);
            paced.synchronize(1000);
            for (int frame = 1; frame <= fps * 8; ++frame) {
                paced.tick(1000 + uint64_t(std::llround(frame * 1000. / fps)));
            }
            const auto value = absolute.sample(paced.seconds()).value;
            check("30/60/120 FPS clocks reach the same pose",
                  std::abs(paced.seconds() - 6) < 1e-12 && std::abs(value - 60) < 1e-5 &&
                      (reference < 0 || std::abs(reference - value) < 1e-9));
            reference = value;
        }
        Tween<Pose> pose({}, {.translation = {10, 20}, .scale = {2, 3}, .rotation = 90, .opacity = 0});
        const auto middle = pose.sample(.5).value;
        check("one track interpolates translation, scale, rotation and opacity",
              middle.translation.x == 5 && middle.translation.y == 10 && middle.scale.width == 1.5 &&
                  middle.scale.height == 2 && middle.rotation == 45 && middle.opacity == .5);
        const auto later = pose.sample(.9).value;
        const auto earlier = pose.sample(.25).value;
        check("sampling can move backward then return to a later time",
              earlier.rotation == 22.5 && pose.sample(.9).value.rotation == later.rotation);
        check("invalid tracks and samples report errors", rejected([] {
                                                              Tween<double>(0, 1, {.duration = 0});
                                                          }) &&
                                                              rejected([&] {
                                                                  once.sample(-1);
                                                              }) &&
                                                              rejected([&] {
                                                                  once.sample(std::numeric_limits<double>::infinity());
                                                              }));
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return passed ? 0 : 1;
}
