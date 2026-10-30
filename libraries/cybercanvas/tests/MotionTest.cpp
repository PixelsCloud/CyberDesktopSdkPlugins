#include <CyberCanvas/Motion.h>

#include <iostream>
#include <limits>

namespace {
bool passed = true;

void check(const char* name, bool condition) {
    passed &= condition;
    std::cout << (condition ? "PASS " : "FAIL ") << name << '\n';
}

template <class F>
bool rejects(F action) {
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
    try {
        Keyframes<double> keys({{0, 0}, {1, 10}, {3, 20}});
        check("piecewise time is not uniform by key count", keys.sample(.5) == 5 && keys.sample(2) == 15);
        check("clamped endpoints", keys.sample(-1) == 0 && keys.sample(4) == 20);
        check("duplicate timestamps rejected", rejects([] {
                  Keyframes<double>({{0, 0}, {0, 1}});
              }));
        check("invalid pose rejected", rejects([] {
                  Keyframes<Pose>({{0, {.translation = {std::numeric_limits<double>::quiet_NaN(), 0}}}});
              }));
        double first = 0, second = 0, third = 0;
        Timeline timeline;
        timeline.append(1, Keyframes<double>({{0, 0}, {1, 10}}), [&](double value) {
            first = value;
        });
        timeline.join(2, Keyframes<double>({{0, 0}, {2, 20}}), [&](double value) {
            second = value;
        });
        timeline.append(3, Keyframes<double>({{0, 0}, {1, 30}}), [&](double value) {
            third = value;
        });
        check("sequence waits for longest parallel member", timeline.duration() == 3);
        timeline.advance(.5);
        check("parallel tracks advance together", first == 5 && second == 5 && third == 0);
        timeline.advance(2);
        check("sequence starts at group end", first == 10 && second == 20 && third == 15);
        timeline.paused(true);
        timeline.advance(100);
        check("pause excludes elapsed input", third == 15 && timeline.position() == 2.5 && !timeline.active());
        timeline.paused(false);
        timeline.speed(-1);
        timeline.advance(1);
        check("reverse resets a future track", first == 10 && second == 15 && third == 0);
        timeline.seek(.25);
        check("seek is absolute", first == 2.5 && second == 2.5 && third == 0);
        timeline.clear();
        timeline.speed(1);
        int completed = 0;
        timeline.at(0, 1, Keyframes<double>({{0, 0}, {1, 10}}),
                    [&](double v) {
                        first = v;
                    },
                    {.completed = [&] {
                        ++completed;
                        timeline.cancel(1);
                    }});
        timeline.advance(1);
        check("completion deferred outside sample", first == 10 && completed == 0 && !timeline.active());
        timeline.dispatch();
        timeline.dispatch();
        check("callback can safely delete its animation", completed == 1 && !timeline.active());
        timeline.clear();
        first = 0;
        timeline.animate(
            1,
            [&] {
                return first;
            },
            [&](double v) {
                first = v;
            },
            10., 1., Ease::Linear);
        timeline.advance(.5);
        timeline.animate(
            1,
            [&] {
                return first;
            },
            [&](double v) {
                first = v;
            },
            15., 1., Ease::Linear);
        timeline.advance(.5);
        check("replacement continues from the visible value", first == 10);
        timeline.clear();
        auto owner = std::make_shared<int>(1);
        timeline.at(0, 1, Keyframes<double>({{0, 0}, {1, 10}}),
                    [&](double v) {
                        first = v;
                    },
                    {.owner = std::weak_ptr<void>(owner), .completed = [&] {
                         ++completed;
                     }});
        timeline.advance(1);
        owner.reset();
        timeline.dispatch();
        check("expired owner suppresses queued completion", completed == 1 && !timeline.active());
        timeline.clear();
        timeline.at(0, 1, Keyframes<double>({{0, 0}, {1, 10}}),
                    [&](double v) {
                        first = v;
                    },
                    {.repeat = Repeat::PingPong, .cycles = 2});
        timeline.advance(1);
        check("ping pong reaches destination", first == 10);
        timeline.advance(3);
        check("finite ping pong ends at source", first == 0 && !timeline.active());
        timeline.clear();
        timeline.at(0, 1, Keyframes<double>({{0, 7}}), [&](double v) {
            first = v;
        });
        timeline.advance(0);
        check("zero duration is a finite assignment", first == 7 && !timeline.active());
        check("zero duration cannot repeat forever", rejects([&] {
                  timeline.at(0, 2, Keyframes<double>({{0, 7}}),
                              [](double) {
                              },
                              {.repeat = Repeat::Loop, .cycles = 0});
              }));
        timeline.clear();
        timeline.at(0, 1, Keyframes<double>({{0, 0}, {1, 10}}), [&](double) {
            timeline.clear();
        });
        check("setter mutation is rejected without corrupting scheduler", rejects([&] {
                  timeline.advance(.1);
              }));
        timeline.clear();
        check("scheduler recovers after setter exception", !timeline.active());
        for (int fps : {30, 60, 120, 144}) {
            Timeline paced;
            paced.at(0, 1, Keyframes<double>({{0, 0}, {2, 20}}), [&](double v) {
                first = v;
            });
            for (int i = 0; i < fps; ++i) {
                paced.advance(1. / fps);
            }
            check("frame rate independent sampling", std::abs(first - 10) < 1e-9);
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return passed ? 0 : 1;
}
