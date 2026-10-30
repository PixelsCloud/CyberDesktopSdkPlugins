#include <algorithm>
#include <iostream>
#include <stdexcept>

#include "plugins/lunar-suite/MechanicalLunar.h"
#include "plugins/mechanical-calendar/tools/TestHost.h"

namespace {
using lunarplugin::Date;
using lunarplugin::Spec;
using mechanicalcalendar::testing::Host;

void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

size_t fastTimers(const Host& host) {
    return std::count_if(host.timers.begin(), host.timers.end(), [](const auto& item) {
        return item.second.interval == 33;
    });
}

struct LiveCalendar {
    Host& host;
    Spec spec;
    CdInstanceApi api{};
    static constexpr CdRect safe{13, 11, 360, 440};

    LiveCalendar(Host& host, Spec spec) : host(host), spec(spec) {
        auto context = host.context("");
        context.plugin_id = spec.id;
        require(lunarplugin::create(spec, &context, &api, sizeof(api)) == CD_OK, "Create mechanical lunar instance");
        CdLayout layout{};
        layout.struct_size = sizeof(layout);
        layout.logical_width = 400;
        layout.logical_height = 470;
        layout.safe_content = safe;
        api.layout_changed(api.instance, &layout);
    }

    ~LiveCalendar() {
        api.destroy(api.instance);
    }

    Date selected() const {
        uint8_t state[8]{};
        uint64_t size = 0;
        require(api.save_state(api.instance, state, sizeof(state), &size) == CD_OK && size == 8, "Save live calendar");
        return {state[3] | int(state[4]) << 8, state[5], state[6]};
    }

    void restore(Date date, bool follow = false) {
        const uint8_t state[]{'L',
                              'U',
                              1,
                              uint8_t(date.year),
                              uint8_t(date.year >> 8),
                              uint8_t(date.month),
                              uint8_t(date.day),
                              uint8_t(follow)};
        require(api.restore_state(api.instance, state, sizeof(state)) == CD_OK, "Restore live calendar");
    }

    void click(cybercanvas::Rect box) {
        const auto layout = lunarplugin::mechanicalLunarLayout(spec, safe.width, safe.height);
        const auto mapped = layout.stage.map(box);
        CdInput input{sizeof(input),
                      CD_INPUT_PRESS,
                      CD_BUTTON_LEFT,
                      0,
                      safe.x + mapped.x + mapped.width / 2,
                      safe.y + mapped.y + mapped.height / 2};
        api.input(api.instance, &input);
        input.kind = CD_INPUT_RELEASE;
        input.buttons = 0;
        api.input(api.instance, &input);
    }

    void navigate(bool next) {
        const auto layout = lunarplugin::mechanicalLunarLayout(spec, safe.width, safe.height);
        click(next ? layout.next : layout.previous);
    }

    void today() {
        const auto layout = lunarplugin::mechanicalLunarLayout(spec, safe.width, safe.height);
        click(spec.rows == 1 ? layout.visible : layout.header);
    }

    void reduced(bool enabled) {
        const CdSettingValue value{sizeof(value), CD_SETTING_TOGGLE, "reduced_motion", enabled};
        require(api.apply_settings(api.instance, &value, 1) == CD_OK, "Reduced motion setting");
    }
};

void lifecycle(Spec spec) {
    Host host;
    {
        LiveCalendar first(host, spec), second(host, spec);
        first.restore({2026, 12, 17});
        second.restore({2026, 12, 17});
        require(host.timers.size() == 2 && !fastTimers(host), "Restoration must not start an animation");
        if (spec.rows == 1) {
            first.today();
        } else {
            first.navigate(true);
            require(first.selected() == Date{2027, 1, 17}, "Forward navigation crosses the year boundary");
        }
        require(fastTimers(host) == 1 && host.timers.size() == 3 && second.selected() == Date{2026, 12, 17},
                "Only the changed calendar starts a fast timer");
        const auto redraws = host.redraws;
        host.advance(66);
        require(fastTimers(host) == 1 && host.redraws > redraws, "Wheel advances through intermediate frames");
        host.advance(200);
        require(!fastTimers(host) && host.timers.size() == 2, "Completed wheel releases its fast timer");
        const auto callbacks = host.callbacks;
        host.advance(2000);
        require(host.callbacks - callbacks <= 6, "Idle calendars do not retain animation-rate callbacks");

        if (spec.rows > 1) {
            first.navigate(false);
            require(first.selected() == Date{2026, 12, 17} && fastTimers(host) == 1, "Backward year rollover animates");
            host.advance(33);
            first.navigate(true);
            first.navigate(true);
            require(first.selected() == Date{2027, 2, 17} && fastTimers(host) == 1,
                    "Rapid navigation replaces one transition");
            host.advance(250);
            host.allowTimers = false;
            first.navigate(true);
            require(first.selected() == Date{2027, 3, 17} && !fastTimers(host),
                    "Animation timer failure still shows the requested date");
            host.allowTimers = true;
            first.navigate(true);
        } else {
            first.restore({2026, 12, 17});
            first.today();
        }
        require(fastTimers(host) == 1, "Start another transition");
        first.reduced(true);
        require(!fastTimers(host), "Reduced motion immediately finishes the transition");
        first.restore({2026, 12, 17});
        if (spec.rows > 1) {
            first.navigate(true);
        } else {
            first.today();
        }
        require(!fastTimers(host), "Reduced-motion navigation remains static");
        first.reduced(false);
        first.restore({2026, 12, 17});
        first.today();
        require(fastTimers(host) == 1, "Return to today uses the wheel transition");
        first.api.set_paused(first.api.instance, 1);
        const auto selected = first.selected();
        require(host.timers.size() == 1, "Hidden instance stops both date and animation timers");
        host.unixMs += 86400000;
        host.advance(1000);
        require(first.selected() == selected, "Hidden calendar remains unchanged");
        first.api.set_paused(first.api.instance, 0);
        require(host.timers.size() == 2 && !fastTimers(host), "Resume refreshes the date without replaying animation");
        const auto resumed = first.selected();
        host.unixMs += 86400000;
        host.advance(1000);
        require(first.selected() == lunarplugin::shiftDay(resumed, 1),
                "Following calendar advances across local midnight");
        require(fastTimers(host) == (spec.rows < 4 ? 1 : 0), "Midnight animates only changing visible date wheels");
        host.advance(250);

        if (spec.rows == 4) {
            first.restore({2026, 2, 17});
            const auto layout =
                lunarplugin::mechanicalLunarLayout(spec, LiveCalendar::safe.width, LiveCalendar::safe.height);
            const auto month = lunarplugin::monthGrid({2026, 2, 17}, true);
            const cybercanvas::CalendarGrid grid{layout.dates, size_t(month.rows)};
            first.click(grid.cell(23));
            require(first.selected() == Date{2026, 2, 18} && !fastTimers(host),
                    "Selecting a day in the same month keeps unchanged header wheels still");
        }
        first.restore({2026, 12, 17});
        first.today();
        require(fastTimers(host) == 1, "Destroy an instance during a running transition");
    }
    require(host.timers.empty(), "Destruction releases every subscription");
    std::cout << "PASS lunar animation lifecycle, navigation, midnight, reduced motion, hidden and teardown: "
              << spec.id << '\n';
}
}  // namespace

int main() try {
    lifecycle({"com.cyberdesktop.calendar.lunar.mechanical.1x2", 1, 2, true});
    lifecycle({"com.cyberdesktop.calendar.lunar.mechanical.2x2", 2, 2, true});
    lifecycle({"com.cyberdesktop.calendar.lunar.mechanical.4x4", 4, 4, true});
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
