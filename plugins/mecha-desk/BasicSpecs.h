#pragma once
#include <array>

namespace aeris_basic {
inline constexpr std::array<BasicSpec, 15> kBasicSpecs{{
    {"com.cyberdesktop.clock.marble.4x2", "chrono-4x2", BasicKind::Digital, 4, 2},
    {"com.cyberdesktop.clock.marble.6x2", "chrono-6x2", BasicKind::Digital, 6, 2},
    {"com.cyberdesktop.clock.analog.1x1", "analog-1x1", BasicKind::Analog, 1, 1},
    {"com.cyberdesktop.clock.analog.1x2", "analog-2x1", BasicKind::Analog, 2, 1},
    {"com.cyberdesktop.clock.analog.1x4", "analog-4x1", BasicKind::Analog, 4, 1},
    {"com.cyberdesktop.clock.analog.2x2", "analog-2x2", BasicKind::Analog, 2, 2},
    {"com.cyberdesktop.clock.digital.1x1", "digital-1x1", BasicKind::Digital, 1, 1},
    {"com.cyberdesktop.clock.digital.1x2", "digital-2x1", BasicKind::Digital, 2, 1},
    {"com.cyberdesktop.clock.digital.1x4", "digital-4x1", BasicKind::Digital, 4, 1},
    {"com.cyberdesktop.clock.digital.2x2", "digital-2x2", BasicKind::Digital, 2, 2},
    {"com.cyberdesktop.clock.flip.1x4", "flip-4x1", BasicKind::Flip, 4, 1},
    {"com.cyberdesktop.clock.flip.2x2", "flip-2x2", BasicKind::Flip, 2, 2},
    {"com.cyberdesktop.clock.flip.2x4", "flip-4x2", BasicKind::Flip, 4, 2},
    {"com.cyberdesktop.clock.lcd.1x2", "lcd-2x1", BasicKind::Lcd, 2, 1},
    {"com.cyberdesktop.clock.lcd.1x4", "lcd-4x1", BasicKind::Lcd, 4, 1},
}};
}
