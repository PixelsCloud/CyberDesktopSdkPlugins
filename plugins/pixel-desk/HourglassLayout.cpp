#include <algorithm>
#include <cmath>
#include <format>

#include "plugins/pixel-desk/PixelHourglass.h"
#include "plugins/pixel-desk/PixelLayout.h"
#include "plugins/pixel-desk/PixelParts.h"

namespace mechanicalhourglass {
const Spec& specification(std::string_view id) {
    for (const auto& spec : pixeldesk::kHourglassCatalog) {
        if (id == spec.id) {
            return spec;
        }
    }
    throw std::invalid_argument("Unknown pixel hourglass");
}

cdsdk::Settings defaults(bool timer) {
    auto settings = cdsdk::Settings{{
        {"duration", CD_SETTING_INTEGER, 30, 0, "", 5, 3600},
        {"physics", CD_SETTING_TOGGLE, 1},
        {"repeat", CD_SETTING_TOGGLE, 1},
        {"running", CD_SETTING_TOGGLE, 1},
        {"sand_color", CD_SETTING_COLOR, pixeldesk::kSandGold},
        {"glow", CD_SETTING_INTEGER, 0, 0, "", 0, 40},
        {"frame_rate", CD_SETTING_CHOICE, 0, 0, "20", 0, 0, {"15", "20", "30"}},
    }};
    if (!timer) {
        return settings;
    }
    return {{{"duration", CD_SETTING_INTEGER, 30, 0, "", 5, 3600},
             {"physics", CD_SETTING_TOGGLE, 1},
             {"repeat", CD_SETTING_TOGGLE, 1},
             {"running", CD_SETTING_TOGGLE, 1},
             {"sand_color", CD_SETTING_COLOR, pixeldesk::kSandGold},
             {"text_color", CD_SETTING_COLOR, pixeldesk::kMint},
             {"glow", CD_SETTING_INTEGER, 0, 0, "", 0, 40},
             {"frame_rate", CD_SETTING_CHOICE, 0, 0, "20", 0, 0, {"15", "20", "30"}}}};
}

std::string remainingText(int seconds) {
    seconds = std::clamp(seconds, 0, 3600);
    return std::format("{:02}:{:02}", seconds / 60, seconds % 60);
}

Geometry arrange(bool timer, double width, double height) {
    const double worldWidth = timer ? 380. : 180., worldHeight = timer ? 170. : 240.;
    const auto bounds = pixeldesk::fitBody(width, height, worldWidth, worldHeight);
    const double scale = bounds.width / worldWidth;
    const auto map = [&](cybercanvas::Rect r) {
        return cybercanvas::Rect{bounds.x + r.x * scale, bounds.y + r.y * scale, r.width * scale, r.height * scale};
    };
    Geometry result;
    result.body = bounds;
    result.jar = map(timer ? cybercanvas::Rect{22.5, 22, 98, 134} : cybercanvas::Rect{26, 17.5, 128, 205});
    if (timer) {
        result.display = map({149, 36, 200, 64});
        result.progress = map({153.75, 113.28, 189.24, 17.48});
    }
    return result;
}

cybercanvas::Point grainPosition(cybercanvas::Point point, cybercanvas::Rect jar) {
    // Preserve the established production registration of the unchanged 19x29
    // Box2D vessel into the rounded glass profile, row by row.
    const double y = std::clamp(point.y, 2.415, 26.415), row = std::clamp(y - .415, 2., 26.);
    const int a = int(std::floor(row)), b = std::min(26, a + 1);
    const double half = hourglassplugin::chamberHalfWidth(a) +
                        (hourglassplugin::chamberHalfWidth(b) - hourglassplugin::chamberHalfWidth(a)) * (row - a);
    const double localY = std::clamp((y - 14.415) / 12., -1., 1.);
    const double profile = std::max(.018, pixeldesk::hourglassHalfWidth(localY) - .035);
    const double localX = std::clamp((point.x - 9.415) / (half + .6), -1., 1.);
    return {jar.x + jar.width * (.5 + localX * profile), jar.y + jar.height * (.5 + localY * .345)};
}
}  // namespace mechanicalhourglass
