#include "plugins/mecha-desk/Armour.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>

namespace aeris_spectrum {
std::vector<Actuator> loadActuators(const std::filesystem::path& directory) {
    std::ifstream file(directory / "assets/skin.json");
    const auto config = nlohmann::json::parse(file);
    std::vector<Actuator> result;
    for (const auto& r : config.at("actuators")) {
        result.push_back({r.at("x"), r.at("width"), r.at("height"), r.at("bottom"), r.at("cap")});
    }
    return result;
}

void drawActuators(aerisskin::Skin& skin, cybercanvas::Canvas& c, const std::vector<Actuator>& rods,
                   const widgetplugin::Spectrum& spectrum, bool peaks) {
    const double extra = skin.size().width - skin.columns() * 80;
    for (size_t i = 0; i < rods.size(); ++i) {
        auto r = rods[i];
        r.x += extra * i / std::max(size_t(1), rods.size() - 1);
        if (extra > 0 && rods.size() > 1) {
            const double originalTop = r.bottom - r.height;
            double left = rods.front().x, right = rods.back().x;
            for (unsigned step = 0; step < 18; ++step) {
                const double middle = (left + right) / 2;
                if (skin.reflow({middle, 0}).x < r.x) {
                    left = middle;
                } else {
                    right = middle;
                }
            }
            const double source = (left + right) / 2;
            auto upper = std::upper_bound(rods.begin(), rods.end(), source, [](double x, const Actuator& rod) {
                return x < rod.x;
            });
            if (upper != rods.begin() && upper != rods.end()) {
                const auto& a = *(upper - 1);
                const double t = (source - a.x) / (upper->x - a.x);
                r.bottom = a.bottom + (upper->bottom - a.bottom) * t;
                r.height = r.bottom - originalTop;
            }
        }
        const double band = double(i) * 63 / std::max(size_t(1), rods.size() - 1);
        const double value = std::clamp(double(spectrum.sample(band)), 0., 1.);
        const double height = std::min(r.height, r.cap + 2 + value * (r.height - r.cap - 2)), top = r.bottom - height;
        // Only the current column is painted. Detached peak markers and baked
        // demonstration rods must never remain above a falling live column.
        if (value < .002) {
            continue;
        }
        c.box(r.x - r.width / 2, top, r.width, height, .55, 0x24394cff);
        c.box(r.x - r.width * .32, top + r.cap, r.width * .18, height - r.cap, .2, 0x526b79ff);
        c.box(r.x - r.width * .09, top + r.cap + .5, r.width * .18, std::max(0., height - r.cap - 1), .2, 0x91d7e2ff);
        c.box(r.x - r.width / 2, top, r.width, r.cap, .45, 0xe9eeebff);
        if (peaks) {
            c.line(r.x - r.width * .24, top + .55, r.x + r.width * .24, top + .55, 0x91d7e2ff, .6);
        }
    }
}

std::vector<ArmourPart> loadArmour(const std::filesystem::path& directory) {
    std::ifstream file(directory / "assets/skin.json");
    const auto config = nlohmann::json::parse(file);
    std::vector<ArmourPart> result;
    for (const auto& item : config.at("armour")) {
        const auto& b = item.at("viewport");
        const auto& m = item.at("motion");
        const auto& o = item.at("origin");
        result.push_back({item.at("name"),
                          {b[0], b[1], b[2], b[3]},
                          {o[0], o[1]},
                          {m[0], m[1]},
                          m[2],
                          item.value("z", int(result.size()))});
    }
    std::stable_sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a.z < b.z;
    });
    return result;
}

void drawArmour(aerisskin::Skin& skin, cybercanvas::Canvas& c, const std::vector<ArmourPart>& parts,
                const widgetplugin::Spectrum& spectrum) {
    const auto average = [&](unsigned start, unsigned end) {
        double sum = 0;
        for (unsigned i = start; i < end; ++i) {
            sum += spectrum.sample(i);
        }
        return std::clamp(sum / (end - start), 0., 1.);
    };
    const double bass = average(0, 12), middle = average(12, 40), treble = average(40, 64);
    const bool wings = skin.columns() == 6;
    const double cx = skin.size().width / 2, cy = wings ? 111 : 101;
    // Seams respond only to the shared live FFT. Silence returns every panel to rest.
    for (int side : {-1, 1}) {
        auto trace = c.path();
        trace.moveTo({cx + side * 7., cy})
            .lineTo({cx + side * (wings ? 80. : 48.), cy - 24})
            .lineTo({cx + side * (wings ? 155. : 77.), cy - 46});
        c.stroke(trace, 0x91d7e200 | uint32_t((.12 + treble * .70) * 255), {.width = .8 + treble * .7});
    }
    for (const auto& part : parts) {
        const bool core = part.name == "armour1";
        const bool outer = part.name == "armour4" || part.name == "armour5";
        const double response = core ? bass : outer && wings ? bass : middle;
        const auto b = part.bounds;
        const double px = b.x + b.width * part.origin.x, py = b.y + b.height * part.origin.y;
        c.saved([&] {
            c.translate(px + part.travel.x * response, py + part.travel.y * response);
            c.rotate(part.turn * response);
            skin.part(c, part.name, {b.x - px, b.y - py, b.width, b.height}, true);
        });
    }
}
}  // namespace aeris_spectrum
