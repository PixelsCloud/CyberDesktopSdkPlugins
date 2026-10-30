#include "plugins/mecha-desk/PlayerRegions.h"

namespace aeris_player {
cybercanvas::Rect nativeProgress(std::string_view id) {
    if (id == "com.cyberdesktop.music.player.mechanical.1x4") {
        return {175.39159891598916, 61.231707336787565, 112.10907859078588, 3.119918704663206};
    }
    if (id == "com.cyberdesktop.music.player.mechanical.2x2") {
        return {45.47692267460317, 165.0903846153846, 68.68653826719577, 3.9557692307692207};
    }
    if (id == "com.cyberdesktop.music.player.mechanical.2x4") {
        return {143.0809528721683, 156.1, 102.85000005339808, 4.452380952380963};
    }
    return {};
}
}  // namespace aeris_player
