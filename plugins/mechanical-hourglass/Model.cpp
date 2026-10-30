#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "plugins/mechanical-hourglass/MechanicalHourglass.h"

namespace mechanicalhourglass {
const Spec& specification(std::string_view id) {
    for (const auto& spec : kCatalog) {
        if (id == spec.id) {
            return spec;
        }
    }
    throw std::invalid_argument("Unknown mechanical hourglass");
}

cdsdk::Settings defaults(bool timer) {
    auto settings = cdsdk::Settings{{
        {"duration", CD_SETTING_INTEGER, 30, 0, "", 5, 3600},
        {"physics", CD_SETTING_TOGGLE, 1},
        {"repeat", CD_SETTING_TOGGLE, 1},
        {"running", CD_SETTING_TOGGLE, 1},
        {"sand_color", CD_SETTING_COLOR, 0xf0bd65ff},
        {"glow", CD_SETTING_INTEGER, 18, 0, "", 0, 40},
        {"frame_rate", CD_SETTING_CHOICE, 0, 0, "20", 0, 0, {"15", "20", "30"}},
    }};
    if (!timer) {
        return settings;
    }
    return {{{"duration", CD_SETTING_INTEGER, 30, 0, "", 5, 3600},
             {"physics", CD_SETTING_TOGGLE, 1},
             {"repeat", CD_SETTING_TOGGLE, 1},
             {"running", CD_SETTING_TOGGLE, 1},
             {"sand_color", CD_SETTING_COLOR, 0xf0bd65ff},
             {"text_color", CD_SETTING_COLOR, 0xffe3a6ff},
             {"glow", CD_SETTING_INTEGER, 18, 0, "", 0, 40},
             {"frame_rate", CD_SETTING_CHOICE, 0, 0, "20", 0, 0, {"15", "20", "30"}}}};
}

std::string remainingText(int seconds) {
    seconds = std::clamp(seconds, 0, 3600);
    const int minutes = seconds / 60, remainder = seconds % 60;
    return (minutes < 10 ? "0" : "") + std::to_string(minutes) + ":" + (remainder < 10 ? "0" : "") +
           std::to_string(remainder);
}

bool digitCell(std::string_view text, unsigned column, unsigned row) {
    constexpr unsigned glyphs[10][7]{{7, 5, 5, 5, 5, 5, 7}, {2, 6, 2, 2, 2, 2, 7}, {7, 1, 1, 7, 4, 4, 7},
                                     {7, 1, 1, 7, 1, 1, 7}, {5, 5, 5, 7, 1, 1, 1}, {7, 4, 4, 7, 1, 1, 7},
                                     {7, 4, 4, 7, 5, 5, 7}, {7, 1, 1, 2, 2, 2, 2}, {7, 5, 5, 7, 5, 5, 7},
                                     {7, 5, 5, 7, 1, 1, 7}};
    if (text.size() != 5 || row >= 7 || column >= kDigitColumns) {
        return false;
    }
    if (column == 8) {
        return row == 2 || row == 4;
    }
    const unsigned index = column < 8 ? column / 4 : 3 + (column - 10) / 4;
    const unsigned local = column < 8 ? column % 4 : (column - 10) % 4;
    if (column == 9 || local >= 3 || index >= text.size() || text[index] < '0' || text[index] > '9') {
        return false;
    }
    return (glyphs[text[index] - '0'][row] & (1u << (2 - local))) != 0;
}

Geometry arrange(bool timer, double width, double height) {
    Geometry g;
    // Source coordinates preserve the trunnions, screws and glass proportions.
    // Only the straight middle of the countdown enclosure changes length.
    const double sourceWidth = timer ? 1774 : 1254, sourceHeight = timer ? 887 : 1254;
    const cybercanvas::Rect visible =
        timer ? cybercanvas::Rect{12, 80, 1747, 758} : cybercanvas::Rect{40, 85, 1174, 1100};
    const double scale =
        std::max(.001, std::min((height - 4) / visible.height, (width - 4) / (visible.width * (timer ? .84 : 1.))));
    const double extension = timer ? std::max(-300. * scale, width - 4 - visible.width * scale) : 0;
    const double x = (width - (visible.width * scale + extension)) / 2 - visible.x * scale;
    const double y = (height - visible.height * scale) / 2 - visible.y * scale;
    g.body = {x, y, sourceWidth * scale, sourceHeight * scale};
    g.extension = extension;
    g.sliceStart = 1000 * scale;
    g.sliceEnd = 1450 * scale;
    const auto map = [&](cybercanvas::Rect r) {
        return cybercanvas::Rect{x + r.x * scale, y + r.y * scale, r.width * scale, r.height * scale};
    };
    g.jar = map(timer ? cybercanvas::Rect{270, 145, 385, 625} : cybercanvas::Rect{360, 177, 532, 886});
    if (timer) {
        g.display = map({824, 303, 750, 330});
        g.display.width += extension;
        g.progress = map({862, 673, 725, 22});
        g.progress.width += extension;
        g.pitch = std::min(g.display.width / kDigitColumns, g.display.height / 7);
        g.display.x += (g.display.width - g.pitch * kDigitColumns) / 2;
        g.display.y += (g.display.height - g.pitch * 7) / 2;
        g.display.width = g.pitch * kDigitColumns;
        g.display.height = g.pitch * 7;
    }
    return g;
}

cybercanvas::Point grainPosition(cybercanvas::Point point, cybercanvas::Rect jar) {
    // Map the existing contained 19x29 simulation into the rounded glass profile.
    // Row-wise width remapping keeps every original grain inside the new vessel.
    const double y = std::clamp(point.y, 2.415, 26.415);
    const double row = std::clamp(y - .415, 2., 26.);
    const int a = int(std::floor(row)), b = std::min(26, a + 1);
    const double half = hourglassplugin::chamberHalfWidth(a) +
                        (hourglassplugin::chamberHalfWidth(b) - hourglassplugin::chamberHalfWidth(a)) * (row - a);
    const double localY = std::clamp((y - 14.415) / 12., -1., 1.);
    const double profile = .018 + .35 * std::pow(std::sin(std::abs(localY) * 1.5707963267948966), 1.15);
    const double localX = std::clamp((point.x - 9.415) / (half + .6), -1., 1.);
    return {jar.x + jar.width * (.5 + localX * profile), jar.y + jar.height * (.5 + localY * .345)};
}
}  // namespace mechanicalhourglass
