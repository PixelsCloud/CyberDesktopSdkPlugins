#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

#include "plugins/mechanical-pixels/Pixels.h"

namespace mechanicalpixels {
const Spec& specification(std::string_view id) {
    for (const auto& spec : kCatalog) {
        if (id == spec.id) {
            return spec;
        }
    }
    throw std::invalid_argument("Unknown mechanical pixel plugin");
}

cdsdk::Settings defaults(Kind kind) {
    if (isClock(kind)) {
        return {{{"format", CD_SETTING_CHOICE, 0, 0, "24", 0, 0, {"24", "12"}},
                 {"timezone", CD_SETTING_CHOICE, 0, 0, "local", 0, 0, {"local", "utc", "offset"}},
                 {"utc_offset", CD_SETTING_INTEGER, 480, 0, "", -720, 840},
                 {"seconds", CD_SETTING_TOGGLE, 1},
                 {"blink_colon", CD_SETTING_TOGGLE, 0},
                 {"text_color", CD_SETTING_COLOR, 0xffe3a6ff},
                 {"seconds_color", CD_SETTING_COLOR, 0xffe3a6ff},
                 {"brightness", CD_SETTING_NUMBER, 0, 100, "", 30, 100},
                 {"glow", CD_SETTING_NUMBER, 0, 18, "", 0, 40}}};
    }
    if (kind == Kind::MusicRound) {
        return {{{"gain", CD_SETTING_NUMBER, 0, 3.5, "", .2, 5},
                 {"release", CD_SETTING_INTEGER, 160, 0, "", 40, 600},
                 {"peaks", CD_SETTING_TOGGLE, 1},
                 {"reduced_motion", CD_SETTING_TOGGLE, 0},
                 {"color", CD_SETTING_COLOR, 0xffb445ff},
                 {"color_top", CD_SETTING_COLOR, 0xffb445ff},
                 {"peak_color", CD_SETTING_COLOR, 0xffe6a9ff},
                 {"brightness", CD_SETTING_NUMBER, 0, 100, "", 30, 100},
                 {"glow", CD_SETTING_NUMBER, 0, 18, "", 0, 40}}};
    }
    return {{{"gain", CD_SETTING_NUMBER, 0, 3.5, "", .2, 5},
             {"release", CD_SETTING_INTEGER, 160, 0, "", 40, 600},
             {"peaks", CD_SETTING_TOGGLE, 1},
             {"reduced_motion", CD_SETTING_TOGGLE, 0},
             {"color", CD_SETTING_COLOR, 0x64bb7aff},
             {"color_middle", CD_SETTING_COLOR, 0xefcb5dff},
             {"color_top", CD_SETTING_COLOR, 0xe87960ff},
             {"brightness", CD_SETTING_NUMBER, 0, 100, "", 30, 100},
             {"glow", CD_SETTING_NUMBER, 0, 18, "", 0, 40}}};
}

std::string clockText(CdEnvironmentSnapshot snapshot, const cdsdk::Settings& settings) {
    const auto& zone = settings.at("timezone").text;
    if (zone != "local") {
        snapshot.utc_offset_minutes = zone == "utc" ? 0 : int32_t(settings.at("utc_offset").integer);
    }
    const auto time = cdsdk::Environment::localTime(snapshot);
    const int hour = settings.at("format").text == "12" ? (time.tm_hour + 11) % 12 + 1 : time.tm_hour;
    char result[12]{};
    if (settings.at("seconds").integer) {
        sprintf_s(result, "%02d:%02d:%02d", hour, time.tm_min, time.tm_sec);
    } else {
        sprintf_s(result, "%02d:%02d", hour, time.tm_min);
    }
    return result;
}

bool clockCell(std::string_view text, unsigned column, unsigned row, bool colonVisible) {
    // Fixed 5x7 pixel glyphs: neither a text font nor a seven-segment display.
    static constexpr uint8_t digits[10][7]{{14, 17, 17, 17, 17, 17, 14}, {4, 12, 4, 4, 4, 4, 14},
                                           {14, 17, 1, 2, 4, 8, 31},     {30, 1, 1, 14, 1, 1, 30},
                                           {2, 6, 10, 18, 31, 2, 2},     {31, 16, 16, 30, 1, 1, 30},
                                           {14, 16, 16, 30, 17, 17, 14}, {31, 1, 2, 4, 8, 8, 8},
                                           {14, 17, 17, 14, 17, 17, 14}, {14, 17, 17, 15, 1, 1, 14}};
    if (!column || row < 1 || row > 7) {
        return false;
    }
    unsigned start = 1;
    for (const char character : text) {
        const unsigned width = character == ':' ? 1 : 5;
        if (column >= start && column < start + width) {
            if (character == ':') {
                return colonVisible && (row == 3 || row == 5);
            }
            return character >= '0' && character <= '9' &&
                   (digits[character - '0'][row - 1] & (1u << (4 - (column - start))));
        }
        start += width + 1;
    }
    return false;
}

std::array<uint32_t, 4> palette(Kind kind, const cdsdk::Settings& settings) {
    if (isClock(kind)) {
        return {uint32_t(settings.at("text_color").integer), uint32_t(settings.at("seconds_color").integer), 0, 0};
    }
    return {uint32_t(settings.at("color").integer),
            kind == Kind::MusicSquare ? uint32_t(settings.at("color_middle").integer) : 0,
            uint32_t(settings.at("color_top").integer),
            kind == Kind::MusicRound ? uint32_t(settings.at("peak_color").integer) : 0};
}

uint32_t cellColor(Kind kind, const cdsdk::Settings& settings, unsigned rowFromBottom, unsigned rows, bool peak) {
    const auto colors = palette(kind, settings);
    if (isClock(kind)) {
        return colors[peak ? 1 : 0];
    }
    if (kind == Kind::MusicRound) {
        if (peak) {
            return colors[3];
        }
        const double fraction = double(rowFromBottom) / std::max(1u, rows - 1);
        uint32_t rgba = 0;
        for (unsigned shift : {0u, 8u, 16u, 24u}) {
            rgba |= uint32_t(std::round(
                        std::lerp(double((colors[0] >> shift) & 255), double((colors[2] >> shift) & 255), fraction)))
                    << shift;
        }
        return rgba;
    }
    const double fraction = double(rowFromBottom + 1) / std::max(1u, rows);
    return fraction <= .4 ? colors[0] : fraction <= .75 ? colors[1] : colors[2];
}

Housing housing(Kind kind) {
    switch (kind) {
        case Kind::ClockSquare:
            return {{2057, 765}, {40, 96, 1976, 591}, {211, 212, 1634, 347}, 75};
        case Kind::ClockRound:
            return {{2060, 763}, {46, 78, 1970, 599}, {194, 204, 1673, 340}, 164};
        case Kind::MusicSquare:
            return {{1997, 788}, {21, 201, 1956, 392}, {133, 258, 1733, 254}, 35};
        case Kind::MusicRound:
            return {{2059, 764}, {20, 166, 2019, 389}, {111, 225, 1836, 251}, 120};
    }
    throw std::invalid_argument("Invalid mechanical pixel kind");
}

Geometry arrange(Kind kind, double width, double height, double dpi, bool seconds) {
    if (!std::isfinite(width) || !std::isfinite(height) || !std::isfinite(dpi) || width <= 0 || height <= 0 ||
        dpi < .5 || dpi > 4) {
        throw std::invalid_argument("Invalid mechanical pixel geometry");
    }
    const auto plate = housing(kind);
    const double margin = std::min(width, height) * .025;
    auto view = cybercanvas::sceneViewport({margin, margin, width - margin * 2, height - margin * 2},
                                           {plate.visible.width, plate.visible.height});
    if (!isClock(kind)) {
        // Short and long music housings share the same end-cap scale. Only the
        // straight center section changes length; screws and feet never squash.
        view.scale =
            std::min((height - margin * 2) / plate.visible.height, (width - margin * 2) / (plate.visible.width * .45));
        view.origin = {(width - plate.visible.width * view.scale) / 2,
                       (height - plate.visible.height * view.scale) / 2};
    }
    const double extension = isClock(kind) ? 0 : width - margin * 2 - plate.visible.width * view.scale;
    const auto map = [&](cybercanvas::Rect source) {
        return cybercanvas::Rect{view.origin.x - extension / 2 + (source.x - plate.visible.x) * view.scale,
                                 view.origin.y + (source.y - plate.visible.y) * view.scale, source.width * view.scale,
                                 source.height * view.scale};
    };
    Geometry result;
    result.body = map({0, 0, plate.source.width, plate.source.height});
    result.aperture = map(plate.aperture);
    result.extension = extension;
    result.aperture.width += extension;
    result.radius = plate.radius * view.scale;
    result.rows = 9;
    const double pad = std::min(result.aperture.height * .1, std::max(1. / dpi, result.aperture.height * .035));
    const double verticalPitch = (result.aperture.height - pad * 2) / result.rows;
    result.columns =
        isClock(kind) ? (seconds ? 41 : 27)
                      : std::clamp(unsigned(std::floor((result.aperture.width - pad * 2) / verticalPitch)), 2u, 256u);
    result.pitch =
        std::min((result.aperture.width - pad * 2) / result.columns, (result.aperture.height - pad * 2) / result.rows);
    result.diameter = result.pitch * .79;
    result.grid = {result.aperture.x + (result.aperture.width - result.columns * result.pitch) / 2,
                   result.aperture.y + (result.aperture.height - result.rows * result.pitch) / 2};
    return result;
}
}  // namespace mechanicalpixels
