#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "plugins/pixel-desk/PixelDesk.h"
#include "plugins/pixel-desk/PixelLayout.h"

namespace pixeldesk {
const Spec& specification(std::string_view id) {
    for (const auto& spec : kCatalog) {
        if (id == spec.id) {
            return spec;
        }
    }
    throw std::invalid_argument("Unknown Pixel Desk plugin");
}

cdsdk::Settings defaults(MatrixKind kind) {
    auto settings = mechanicalpixels::defaults(kind);
    const CdSettingValue colors[]{
        {sizeof(CdSettingValue), CD_SETTING_COLOR, "text_color",
         kind == MatrixKind::ClockSquare ? 0x92edb5ff : 0xffce63ff, 0, ""},
        {sizeof(CdSettingValue), CD_SETTING_COLOR, "seconds_color",
         kind == MatrixKind::ClockSquare ? 0x92edb5ff : 0xffce63ff, 0, ""},
        {sizeof(CdSettingValue), CD_SETTING_NUMBER, "glow", 0, 0, ""},
    };
    if (settings.apply(colors, uint32_t(std::size(colors))) != CD_OK) {
        throw std::logic_error("Invalid Pixel Desk clock defaults");
    }
    return settings;
}

Geometry arrange(const Spec& spec, double width, double height, double dpi, bool seconds) {
    if (!std::isfinite(width) || !std::isfinite(height) || !std::isfinite(dpi) || width <= 0 || height <= 0 ||
        dpi < .5 || dpi > 4) {
        throw std::invalid_argument("Invalid Pixel Desk geometry");
    }
    // Register the visible case, not the transparent PNG canvas. The live matrix
    // remains in the actual glass opening at every host grid size and DPI.
    const bool normalized = std::string_view(spec.art) == "clock-square-normalized";
    const cybercanvas::Rect visible =
        normalized ? cybercanvas::Rect{55, 130, 1946, 485} : cybercanvas::Rect{79, 126, 2014, 476};
    const cybercanvas::Rect opening =
        normalized ? cybercanvas::Rect{230, 235, 1590, 276} : cybercanvas::Rect{236, 226, 1700, 242};
    const cybercanvas::Size source = normalized ? cybercanvas::Size{2055, 765} : cybercanvas::Size{2172, 724};
    const auto body = fitBody(width, height, visible.width, visible.height);
    const double scale = body.width / visible.width;
    const cybercanvas::Point origin{body.x - visible.x * scale, body.y - visible.y * scale};
    Geometry geometry;
    geometry.artwork = {origin.x, origin.y, source.width * scale, source.height * scale};
    geometry.aperture = {origin.x + opening.x * scale, origin.y + opening.y * scale, opening.width * scale,
                         opening.height * scale};
    geometry.columns = seconds ? 39 : 25;
    geometry.rows = 7;
    geometry.pitch = std::min(geometry.aperture.width * .98 / geometry.columns, geometry.aperture.height * .96 / 7);
    geometry.diameter = geometry.pitch * .91;
    geometry.grid = {geometry.aperture.x + (geometry.aperture.width - geometry.columns * geometry.pitch) / 2,
                     geometry.aperture.y + (geometry.aperture.height - geometry.rows * geometry.pitch) / 2};
    return geometry;
}
}  // namespace pixeldesk
