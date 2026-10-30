#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "plugins/mechanical-calendar/Calendar.h"
#include "plugins/mechanical-calendar/tools/TestHost.h"

namespace {
using namespace mechanicalcalendar;
using testing::Host;

void check(bool passed, const char* message) {
    if (!passed) {
        throw std::runtime_error(message);
    }
    std::cout << "PASS " << message << '\n';
}

CdResult setting(Instance& instance, const char* id, uint32_t type, int64_t integer, const char* text = nullptr) {
    const CdSettingValue value{sizeof(value), type, id, integer, 0, text};
    return instance.applySettings(&value, 1);
}

void calendar() {
    const auto october = month({2026, 10, 13}, 0, true);
    check(october.start == 3 && october.days == 31 && october.weeks == 5, "Approved October grid starts on Thursday");
    check(october.cells[15].date.day == 13 && october.cells[15].today, "Today marker belongs to the correct cell");
    check(!october.cells[0].current && !october.cells[34].current, "October leading and trailing cells stay adjacent");
    check(october.cells[5].weekend && october.cells[6].weekend && !october.cells[4].weekend,
          "Monday-first weekend columns use Saturday and Sunday");
    const auto sunday = month({2026, 10, 13}, 0, false);
    check(sunday.start == 4 && sunday.cells[0].weekend && sunday.cells[6].weekend && !sunday.cells[5].weekend,
          "Sunday-first grid remaps both dates and weekend columns");
    check(month({2021, 2, 1}, 0, true).weeks == 4, "Four-week month uses four complete date rows");
    check(month({2026, 8, 1}, 0, true).weeks == 6, "Six-week month includes every final-row date");
    check(month({2000, 2, 1}, 0, true).days == 29 && month({1900, 2, 1}, 0, true).days == 28 &&
              month({2100, 2, 1}, 0, true).days == 28,
          "Gregorian century leap-year rules remain correct");
    check(month({2000, 1, 1}, -1, true).date == Date{1999, 12, 1} &&
              month({1999, 12, 1}, 1, true).date == Date{2000, 1, 1},
          "Navigation crosses year boundaries in both directions");
    check(headerDigits({2000, 1, 1}) == L"200001" && headerDigits({2026, 10, 1}) == L"202610",
          "Year and month wheels use six padded live digits");
    bool complete = true;
    for (int year = 1800; year <= 2199; ++year) {
        for (int m = 1; m <= 12; ++m) {
            for (bool monday : {false, true}) {
                const auto value = month({year, m, 1}, 0, monday);
                std::set<int> days;
                for (unsigned i = 0; i < value.weeks * 7; ++i) {
                    const auto& day = value.cells[i];
                    if (day.current) {
                        complete &= days.insert(day.date.day).second && day.date.year == year && day.date.month == m;
                    }
                }
                complete &= days.size() == value.days && *days.begin() == 1 && *days.rbegin() == int(value.days) &&
                            value.weeks >= 4 && value.weeks <= 6;
            }
        }
    }
    check(complete, "9600 month/week-start combinations contain every date exactly once");
    CdEnvironmentSnapshot snapshot{sizeof(snapshot), 1, 1791849600000ull - 3600000, 0, 120};
    check(localDate(snapshot) == Date{2026, 10, 13}, "Calendar uses the host local timezone across UTC midnight");
    snapshot.utc_offset_minutes = -120;
    check(localDate(snapshot) == Date{2026, 10, 12}, "Negative timezone offsets choose the previous local day");
}

void click(Instance& instance, cybercanvas::Point point) {
    CdInput value{sizeof(value), CD_INPUT_PRESS, CD_BUTTON_LEFT, 0, point.x, point.y};
    instance.input(value);
    value.kind = CD_INPUT_RELEASE;
    value.buttons = 0;
    instance.input(value);
}

void lifecycle() {
    Host host;
    {
        const auto context = host.context(".");
        Instance a(context), b(context);
        check(host.timers.size() == 2 && a.displayed().today == Date{2026, 10, 13},
              "Instances start with independent minute watchers and real host dates");
        check(a.navigate(1) == CD_OK && a.offset() == 1 && a.rolling() && b.offset() == 0,
              "Navigation changes only this instance and starts the wheel transition");
        host.advance(250);
        check(!a.rolling() && host.timers.size() == 2,
              "Wheel transition finishes and releases its fast update cadence");
        const auto calls = host.callbacks;
        host.advance(120000);
        check(host.callbacks - calls <= 6,
              "Idle calendar checks the date at minute cadence rather than animation rate");
        host.allowTimers = false;
        check(a.navigate(2) != CD_OK && a.offset() == 1 && !a.rolling() && host.timers.size() == 2,
              "Failed animation timer preserves the existing month and watcher");
        host.allowTimers = true;
        check(a.navigate(14) == CD_OK, "Calendar accepts a later month within the supported range");
        const auto state = a.saveState();
        check(state.size() == 8 && b.restoreState(state) == CD_OK && b.offset() == 14 && !b.rolling(),
              "Saved month offset restores without an unwanted wheel animation");
        auto invalid = state;
        invalid[0] = 'X';
        check(b.restoreState(invalid) == CD_ERROR_ARGUMENT && b.offset() == 14,
              "Corrupt saved state is rejected without replacing the viewed month");
        check(b.restoreState({}) == CD_OK && b.offset() == 0, "Empty state returns to the actual current month");
        check(a.navigate(1201) == CD_ERROR_ARGUMENT && a.offset() == 14, "Navigation limits reject out-of-range state");
        check(setting(a, "accent", CD_SETTING_COLOR, 0x2681e6ff) == CD_OK &&
                  b.settings.at("accent").integer == 0xf0c77eff,
              "Selected accent color remains independent between calendar instances");
        check(setting(a, "week_start", CD_SETTING_CHOICE, 0, "sunday") == CD_OK && !a.displayed().monday &&
                  b.displayed().monday,
              "Week-start changes rebuild only the selected instance grid");
        check(setting(a, "week_start", CD_SETTING_CHOICE, 0, "invalid") == CD_ERROR_ARGUMENT && !a.displayed().monday,
              "Invalid settings leave the previous grid intact");
        check(setting(a, "font_family", CD_SETTING_FONT, 0, "\xff") == CD_ERROR_ARGUMENT,
              "Invalid UTF-8 font names are rejected before drawing");
        host.allowTimers = false;
        check(setting(a, "reduced_motion", CD_SETTING_TOGGLE, 1) != CD_OK && !a.settings.at("reduced_motion").integer &&
                  a.rolling(),
              "Failed reduced-motion timer transaction preserves animation and settings");
        host.allowTimers = true;
        check(setting(a, "reduced_motion", CD_SETTING_TOGGLE, 1) == CD_OK && !a.rolling(),
              "Reduced motion immediately completes a running wheel transition");
        check(a.navigate(-1) == CD_OK && !a.rolling(),
              "Reduced-motion navigation directly shows its destination month");
        a.setPaused(true);
        const auto hidden = a.displayed();
        host.unixMs += 86400000;
        host.advance(60000);
        check(a.displayed() == hidden && host.timers.size() == 1,
              "Hidden calendar stops all polling and keeps its state");
        a.setPaused(false);
        check(a.displayed().today.day == 14 && a.offset() == -1 && host.timers.size() == 2,
              "Resume refreshes the actual local date and preserves the viewed month offset");
        CdLayout layout{};
        layout.struct_size = sizeof(layout);
        layout.logical_width = 410;
        layout.logical_height = 260;
        layout.safe_content = {17, 11, 378, 234};
        a.layoutChanged(layout);
        const auto g = arrange(378, 234, a.displayed().weeks);
        const auto button = g.buttons[1];
        const cybercanvas::Point point{17 + button.x + button.width / 2, 11 + button.y + button.height / 2};
        click(a, point);
        check(a.offset() == 0, "Navigation hit testing follows the actual scaled button and inset safe area");
        CdInput drag{sizeof(drag), CD_INPUT_PRESS, CD_BUTTON_LEFT, 0, point.x, point.y};
        a.input(drag);
        drag.kind = CD_INPUT_MOVE;
        drag.x += 30;
        a.input(drag);
        drag.kind = CD_INPUT_RELEASE;
        drag.buttons = 0;
        drag.x -= 30;
        a.input(drag);
        check(a.offset() == 0, "Dragging away and returning to a button does not turn the month");
        a.navigate(5);
        CdInput doubleClick{sizeof(doubleClick), CD_INPUT_DOUBLE_CLICK, CD_BUTTON_LEFT, 0, 100, 100};
        a.input(doubleClick);
        check(a.offset() == 0, "Double-clicking the calendar returns to the current month");
        check(buttonAt(g, {-10, -10}) == 0, "Transparent exterior does not become a navigation button");
    }
    check(host.timers.empty(), "Destruction releases all per-instance date watchers");
}

void rendering(const std::filesystem::path& root) {
    Host host;
    host.iconApi = {sizeof(CdIconsApi), 1, [](void* pointer, const char* name, uint32_t color, const CdFrame* frame) {
                        if (std::string_view(name) != "chevron-left" && std::string_view(name) != "chevron-right") {
                            return CD_ERROR_UNSUPPORTED;
                        }
                        ++static_cast<Host*>(pointer)->iconCalls;
                        for (unsigned y = 0; y < frame->height; ++y) {
                            for (unsigned x = 0; x < frame->width; ++x) {
                                auto* p = frame->rgba + y * frame->stride + x * 4;
                                for (unsigned channel = 0; channel < 4; ++channel) {
                                    p[channel] = uint8_t(color >> (24 - channel * 8));
                                }
                            }
                        }
                        return CD_OK;
                    }};
    const auto path = (root / kPluginId).u8string();
    const std::string directory(reinterpret_cast<const char*>(path.data()), path.size());
    Renderer renderer(host.context(directory.c_str()));
    auto settings = defaults();
    auto calendar = month({2026, 10, 13}, 0, true);
    for (double dpi : {1., 1.25, 1.5, 2., 3.}) {
        const unsigned w = unsigned(400 * dpi), h = unsigned(250 * dpi), stride = w * 4 + 20;
        std::vector<uint8_t> bytes(size_t(stride) * h + 64, 0xa5);
        CdFrame frame{sizeof(frame), w, h, stride, uint64_t(stride) * h, bytes.data() + 32, dpi};
        bool rendered = false;
        for (unsigned pass = 0; pass < 200; ++pass) {
            rendered = renderer.render(calendar, settings, {}, frame, {7, 5, 385, 238});
            if (!rendered || renderer.ready()) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        check(rendered && renderer.ready(), "Native month rendering and background image decode succeed at actual DPI");
        bool valid = std::all_of(bytes.begin(), bytes.begin() + 32,
                                 [](auto p) {
                                     return p == 0xa5;
                                 }) &&
                     std::all_of(bytes.end() - 32, bytes.end(), [](auto p) {
                         return p == 0xa5;
                     });
        size_t transparent = 0;
        for (unsigned y = 0; y < h; ++y) {
            for (unsigned x = 0; x < w; ++x) {
                const auto* p = frame.rgba + y * stride + x * 4;
                valid &= p[0] <= p[3] && p[1] <= p[3] && p[2] <= p[3];
                transparent += p[3] == 0;
            }
            for (unsigned x = w * 4; x < stride; ++x) {
                valid &= frame.rgba[size_t(y) * stride + x] == 0xa5;
            }
        }
        check(valid && transparent > size_t(w) * h / 10,
              "Alpha, transparent margins, stride padding and buffer guards remain valid");
        const auto builds = renderer.cacheBuilds(), calls = host.iconCalls;
        renderer.render(calendar, settings, {{2026, 9, 1}, .5}, frame, {7, 5, 385, 238});
        check(renderer.cacheBuilds() == builds, "Wheel movement reuses the cached calendar grid and material layer");
        check(host.iconCalls == calls, "Lucide icon requests reuse the host raster cache for the same DPI");
        renderer.render(calendar, settings, {{2026, 9, 1}, .8, 1, 1, 1}, frame, {7, 5, 385, 238});
        check(renderer.cacheBuilds() == builds, "Button hover and press feedback do not rebuild the month layer");
    }
    const CdSettingValue accent{sizeof(accent), CD_SETTING_COLOR, "accent", 0x2681e6ff};
    check(settings.apply(&accent, 1) == CD_OK, "Standard color-picker values are accepted");
    std::vector<uint8_t> pixels(800 * 500 * 4);
    CdFrame frame{sizeof(frame), 800, 500, 3200, pixels.size(), pixels.data(), 2};
    renderer.render(calendar, settings, {}, frame);
    unsigned blue = 0;
    for (size_t i = 0; i < pixels.size(); i += 4) {
        blue += pixels[i + 2] > 100 && pixels[i + 2] > pixels[i] * 1.5;
    }
    check(blue > 40, "Selected accent reaches the rendered today cell border");
    const auto g = arrange(400, 250, calendar.weeks);
    const auto selected = g.cell(1, 3);
    std::array<bool, 4> corners{};
    for (unsigned y = unsigned(selected.y * 2); y < unsigned((selected.y + selected.height) * 2); ++y) {
        for (unsigned x = unsigned(selected.x * 2); x < unsigned((selected.x + selected.width) * 2); ++x) {
            const auto* pixel = frame.rgba + y * frame.stride + x * 4;
            if (pixel[2] <= 100 || pixel[2] <= pixel[0] * 1.5) {
                continue;
            }
            const double localX = (x / 2. - selected.x) / selected.width;
            const double localY = (y / 2. - selected.y) / selected.height;
            if ((localX < .18 || localX > .82) && (localY < .22 || localY > .78)) {
                corners[(localX > .5 ? 1 : 0) + (localY > .5 ? 2 : 0)] = true;
            }
        }
    }
    check(std::all_of(corners.begin(), corners.end(),
                      [](bool value) {
                          return value;
                      }),
          "Today accent follows all four cell corners instead of a central circular badge");
    bool centered = true;
    for (const auto slot : g.wheels) {
        unsigned left = frame.width, right = 0;
        for (unsigned y = unsigned(slot.y * 2); y < unsigned((slot.y + slot.height) * 2); ++y) {
            for (unsigned x = unsigned(slot.x * 2); x < unsigned((slot.x + slot.width) * 2); ++x) {
                const auto* pixel = frame.rgba + y * frame.stride + x * 4;
                if (pixel[0] > 160 && pixel[1] > 145 && pixel[2] > 120) {
                    left = std::min(left, x);
                    right = std::max(right, x);
                }
            }
        }
        centered &= left <= right && std::abs((left + right + 1.) / 2 - (slot.x + slot.width / 2) * 2) <= 1.5;
    }
    check(centered, "Visible year and month digits center within their individual material slots");
    frame.capacity = 1;
    check(!renderer.render(calendar, settings, {}, frame), "Undersized output buffers are rejected");
    frame.capacity = pixels.size();
    frame.device_pixel_ratio = std::numeric_limits<double>::quiet_NaN();
    check(!renderer.render(calendar, settings, {}, frame), "Invalid DPI is rejected before drawing");
}
}  // namespace

int wmain(int argc, wchar_t** argv) try {
    check(argc == 2, "Explicit plugin directory supplied");
    calendar();
    lifecycle();
    rendering(argv[1]);
    return 0;
} catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
}
