#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "plugins/steampunk-desk/SteamDesk.h"

namespace steamdesk {
const Spec& specification(std::string_view id) {
    for (const auto& spec : kCatalog) {
        if (id == spec.id) {
            return spec;
        }
    }
    throw std::invalid_argument("Unknown Steamworks plugin");
}

cdsdk::Settings defaults(MatrixKind kind) {
    auto settings = mechanicalpixels::defaults(kind);
    const CdSettingValue colors[]{
        {sizeof(CdSettingValue), CD_SETTING_COLOR, "text_color", 0xffe5acff, 0, ""},
        {sizeof(CdSettingValue), CD_SETTING_COLOR, "seconds_color", 0xffb348ff, 0, ""},
        {sizeof(CdSettingValue), CD_SETTING_NUMBER, "glow", 0, 0, ""},
    };
    if (settings.apply(colors, uint32_t(std::size(colors))) != CD_OK) {
        throw std::logic_error("Invalid Steamworks clock defaults");
    }
    return settings;
}

cybercanvas::Rect caseRegistration(MatrixKind kind) {
    return mechanicalpixels::isRound(kind) ? cybercanvas::Rect{30, 118, 2111, 479}
                                           : cybercanvas::Rect{13, 136, 2146, 421};
}

Geometry arrange(const Spec&, double width, double height, double dpi, bool seconds) {
    if (!std::isfinite(width) || !std::isfinite(height) || !std::isfinite(dpi) || width <= 0 || height <= 0 ||
        dpi < .5 || dpi > 4) {
        throw std::invalid_argument("Invalid Steamworks geometry");
    }
    // Both 4x1 clocks share a physical envelope and emitter pitch. The common
    // opening is inside both glass windows after their straight panels resize.
    constexpr cybercanvas::Rect opening{425, 138, 1299, 205};
    const double margin = std::min(width, height) * .025;
    const double scale = std::min((width - margin * 2) / kCaseSize.width, (height - margin * 2) / kCaseSize.height);
    const cybercanvas::Point origin{(width - kCaseSize.width * scale) / 2, (height - kCaseSize.height * scale) / 2};
    Geometry geometry;
    geometry.artwork = {origin.x, origin.y, kCaseSize.width * scale, kCaseSize.height * scale};
    geometry.aperture = {origin.x + opening.x * scale, origin.y + opening.y * scale, opening.width * scale,
                         opening.height * scale};
    geometry.columns = seconds ? 39 : 25;
    geometry.rows = 7;
    geometry.pitch = std::min(geometry.aperture.width * .96 / geometry.columns, geometry.aperture.height * .96 / 7);
    geometry.diameter = geometry.pitch * .82;
    geometry.grid = {geometry.aperture.x + (geometry.aperture.width - geometry.columns * geometry.pitch) / 2,
                     geometry.aperture.y + (geometry.aperture.height - geometry.rows * geometry.pitch) / 2};
    return geometry;
}
}  // namespace steamdesk
