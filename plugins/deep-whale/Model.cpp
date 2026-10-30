#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "plugins/deep-whale/DeepWhale.h"

namespace deepwhale {
const Spec& specification(std::string_view id) {
    for (const auto& spec : kCatalog) {
        if (id == spec.id) {
            return spec;
        }
    }
    throw std::invalid_argument("Unknown Deep Whale clock");
}

cdsdk::Settings defaults(MatrixKind kind) {
    auto settings = mechanicalpixels::defaults(kind);
    const CdSettingValue values[]{
        {sizeof(CdSettingValue), CD_SETTING_COLOR, "text_color", 0xd6e7fdff},
        {sizeof(CdSettingValue), CD_SETTING_COLOR, "seconds_color", 0xbed6f4ff},
        {sizeof(CdSettingValue), CD_SETTING_NUMBER, "glow", 0, 0},
    };
    if (settings.apply(values, uint32_t(std::size(values))) != CD_OK) {
        throw std::logic_error("Invalid Deep Whale defaults");
    }
    return settings;
}

cybercanvas::ImageRegion artworkRegion(MatrixKind kind) {
    return mechanicalpixels::isRound(kind) ? cybercanvas::ImageRegion{10, 98, 2100, 608}
                                           : cybercanvas::ImageRegion{49, 32, 1988, 687};
}

cybercanvas::Rect readingRegion(MatrixKind kind) {
    return mechanicalpixels::isRound(kind) ? cybercanvas::Rect{320, 375, 1055, 213}
                                           : cybercanvas::Rect{527, 435, 1190, 184};
}

Geometry arrange(const Spec& spec, double width, double height, double dpi, bool seconds) {
    if (!std::isfinite(width) || !std::isfinite(height) || !std::isfinite(dpi) || width < 28 || height < 28 ||
        dpi < .5 || dpi > 4) {
        throw std::invalid_argument("Invalid Deep Whale geometry");
    }
    const bool round = mechanicalpixels::isRound(spec.kind);
    const auto source = artworkRegion(spec.kind);
    const auto face = readingRegion(spec.kind);
    const double scale = std::min((width - 14) / source.width, (height - 14) / source.height);
    Geometry g;
    g.originalBody = {source.width * scale, source.height * scale};
    g.artwork = {7, (height - g.originalBody.height) / 2, width - 14, g.originalBody.height};
    const double extension = g.artwork.width - g.originalBody.width;
    g.bodyCut = ((round ? 1120. : 1250.) - source.x) * scale;
    g.aperture = {g.artwork.x + (face.x - source.x) * scale, g.artwork.y + (face.y - source.y) * scale,
                  face.width * scale + extension, face.height * scale};
    g.columns = seconds ? 39 : 25;
    g.rows = 7;
    // Distribute intact square/round emitters across the complete reading face.
    g.pitchX = g.aperture.width * .97 / g.columns;
    g.pitchY = g.aperture.height * .90 / g.rows;
    g.pitch = std::min(g.pitchX, g.pitchY);
    g.diameter = g.pitch * .76;
    g.grid = {g.aperture.x + (g.aperture.width - g.columns * g.pitchX) / 2,
              g.aperture.y + (g.aperture.height - g.rows * g.pitchY) / 2};
    g.emitters.fill(true);
    for (unsigned i = 0; i < g.columns; ++i) {
        g.columnOffsets[i] = i * g.pitchX;
    }
    if (!round) {
        // Keep 5x7 glyphs isotropic. Extra width belongs between characters only.
        g.pitchX = g.pitchY = g.pitch;
        const std::string_view layout = seconds ? "00:00:00" : "00:00";
        const unsigned glyphColumns = seconds ? 32u : 21u;
        const double gap =
            std::clamp((g.aperture.width * .97 - glyphColumns * g.pitch) / (layout.size() - 1), g.pitch, 3 * g.pitch);
        const double readingWidth = glyphColumns * g.pitch + (layout.size() - 1) * gap;
        g.grid = {g.aperture.x + (g.aperture.width - readingWidth) / 2,
                  g.aperture.y + (g.aperture.height - 7 * g.pitch) / 2};
        unsigned column = 0;
        double pen = 0;
        g.emitters.fill(false);
        for (size_t i = 0; i < layout.size(); ++i) {
            const unsigned width = layout[i] == ':' ? 1u : 5u;
            for (unsigned j = 0; j < width; ++j) {
                g.columnOffsets[column] = pen + j * g.pitch;
                g.emitters[column++] = true;
            }
            pen += width * g.pitch;
            if (i + 1 < layout.size()) {
                g.columnOffsets[column++] = pen + (gap - g.pitch) / 2;
                pen += gap;
            }
        }
    }
    return g;
}
}  // namespace deepwhale
