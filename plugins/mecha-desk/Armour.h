#pragma once

#include "plugins/mecha-desk/Skin.h"
#include "plugins/widget-suite/Widget.h"

namespace aeris_spectrum {
struct ArmourPart {
    std::string name;
    cybercanvas::Rect bounds;
    cybercanvas::Point origin{.5, .5}, travel;
    double turn = 0;
    int z = 0;
};

struct Actuator {
    double x, width, height, bottom, cap;
};

std::vector<Actuator> loadActuators(const std::filesystem::path& directory);
void drawActuators(aerisskin::Skin& skin, cybercanvas::Canvas& canvas, const std::vector<Actuator>& rods,
                   const widgetplugin::Spectrum& spectrum, bool peaks);

std::vector<ArmourPart> loadArmour(const std::filesystem::path& directory);
void drawArmour(aerisskin::Skin& skin, cybercanvas::Canvas& canvas, const std::vector<ArmourPart>& parts,
                const widgetplugin::Spectrum& spectrum);
}  // namespace aeris_spectrum
