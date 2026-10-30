#include <cmath>
#include <format>
#include <numbers>

#include "plugins/mecha-desk/controllers/precision/Clock.h"

namespace aeris_precision {
using aerisskin::Canvas;

Renderer::Renderer(Kind kind, const std::filesystem::path& assets)
    : skin_(std::make_unique<aerisskin::Skin>(assets)), kind_(kind) {}

void Renderer::bind(const CdCreateContext& context) {
    skin_->bind(context);
}

void Renderer::appearance(const cdsdk::Settings& settings) {
    skin_->appearance(settings);
    CdSettingValue value{sizeof(CdSettingValue)};
    if (settings.get("hand_color", &value, sizeof(value)) == CD_OK) {
        handColor_ = uint32_t(value.integer);
    }
    if (settings.get("seconds_color", &value, sizeof(value)) == CD_OK) {
        secondsColor_ = uint32_t(value.integer);
    }
}

bool Renderer::poll() {
    return skin_->poll();
}

bool Renderer::pending() const {
    return skin_->pending();
}

void Renderer::pause(bool paused) {
    skin_->pause(paused);
}

cdsdk::ImageSlotStats Renderer::assets() const {
    return skin_->imageStats();
}

aerisskin::View Renderer::view(CdRect safe, cybercanvas::Size world) {
    return skin_->view(safe, world);
}

void Renderer::draw(Canvas& c, const State& state, const Options& options, double scale) {
    if (options.background) {
        skin_->worldBackdrop(c, false);
    }
    const bool light = kind_ == Kind::Dress;
    const uint32_t ink = skin_->foreground(light ? 0x273e4dff : 0xd9f0e8ff);
    if (skin_->terminal() && skin_->region("readout0").width > 0) {
        skin_->artLayer(c, [&](auto& art) {
            const auto r = skin_->region("readout0");
            const int hour = int(state.hour / 30) % 12, minute = int(state.minute / 6) % 60;
            skin_->label(art, std::format(L"{:02}:{:02}", hour ? hour : 12, minute), r, r.height * .85, ink);
        });
        return;
    }
    const double radius = 500;
    if (kind_ == Kind::Skeleton && options.detail) {
        // Preserve the authored metal teeth, spokes and engraving. Each wheel is a
        // cropped native-DPI image with an independent pivot, mask and gear ratio.
        skin_->artLayer(c, [&](auto& art) {
            constexpr std::array<unsigned, 7> teeth{44, 36, 32, 30, 28, 22, 48};
            for (size_t i = 0; i < teeth.size(); ++i) {
                const auto name = "gear" + std::to_string(i);
                const auto r = skin_->region(name);
                if (r.width <= 0) {
                    continue;
                }
                const double radius = r.width / 2;
                art.saved([&] {
                    art.translate(r.x + radius, r.y + radius);
                    auto mask = art.path();
                    mask.moveTo({radius, 0})
                        .arcTo({-radius, 0}, {radius, radius})
                        .arcTo({radius, 0}, {radius, radius})
                        .close();
                    art.masked(mask, [&] {
                        art.rotate(gearAngle(state.mechanismSeconds, teeth[i], i != teeth.size() - 1));
                        skin_->part(art, name, {-radius, -radius, r.width, r.height}, true);
                    });
                });
            }
        });
    }
    for (unsigned i = 0; i < 60; ++i) {
        if (light && i >= 26 && i <= 34) {
            continue;
        }
        const double a = i * std::numbers::pi / 30, inner = i % 5 ? .91 : .84;
        const double tick = radius * (i % 5 ? .009 : .022);
        c.line(500 + std::sin(a) * radius * inner, 500 - std::cos(a) * radius * inner, 500 + std::sin(a) * radius * .96,
               500 - std::cos(a) * radius * .96, ink,
               skin_->terminal() ? std::max((i % 5 ? .65 : 1.) / scale, tick) : tick);
    }
    for (unsigned hour = 1; hour <= 12; ++hour) {
        if ((hour % 3 != 0) || (light && hour == 6) || kind_ == Kind::Skeleton ||
            (hour == 3 && skin_->region("date").width > 0)) {
            continue;
        }
        const double a = hour * std::numbers::pi / 6;
        const double x = 500 + std::sin(a) * radius * .70, y = 500 - std::cos(a) * radius * .70;
        skin_->label(c, std::to_wstring(hour), {x - radius * .20, y - radius * .14, radius * .40, radius * .28},
                     std::max(radius * .18, 13. / scale), ink);
    }
    if (skin_->terminal() && light && options.detail) {
        constexpr double moonX = 500, moonY = 755, moonRadius = 82;
        c.oval(moonX, moonY, moonRadius, moonRadius, 0x172c34ff);
        const double terminator = std::cos(state.lunarPhase * 2 * std::numbers::pi);
        const double side = state.lunarPhase < .5 ? 1 : -1;
        auto moon = c.path();
        moon.moveTo({moonX, moonY - moonRadius});
        for (unsigned i = 1; i <= 32; ++i) {
            const double y = -moonRadius + i * moonRadius / 16;
            moon.lineTo({moonX + side * std::sqrt(std::max(0., moonRadius * moonRadius - y * y)), moonY + y});
        }
        for (int i = 31; i >= 0; --i) {
            const double y = -moonRadius + i * moonRadius / 16;
            moon.lineTo(
                {moonX + side * terminator * std::sqrt(std::max(0., moonRadius * moonRadius - y * y)), moonY + y});
        }
        moon.close();
        c.fill(moon, 0xe2f8ffff);
        c.oval(moonX, moonY, moonRadius, moonRadius, 0x00c8d2ff, 4);
    }
    const auto hand = [&](double angle, double length, double weight, uint32_t color) {
        const double a = angle * std::numbers::pi / 180;
        const double width = std::max(weight < .02 ? .9 / scale : 1.4 / scale, radius * weight);
        if (kind_ == Kind::Skeleton) {
            c.line(500, 500, 500 + std::sin(a) * radius * length, 500 - std::cos(a) * radius * length, 0x142737ff,
                   width + 1.5 / scale);
        }
        c.line(500, 500, 500 + std::sin(a) * radius * length, 500 - std::cos(a) * radius * length, color, width);
    };
    hand(state.hour, .53, .038, handColor_ ? handColor_ : ink);
    hand(state.minute, .78, .026, handColor_ ? handColor_ : ink);
    if (options.seconds) {
        hand(state.second, .88, .009, secondsColor_ ? secondsColor_ : 0x529bacff);
    }
    c.oval(500, 500, radius * .035, radius * .035, 0x529bacff);
    skin_->artLayer(c, [&](auto& art) {
        const auto r = skin_->region("date");
        if (r.width > 0) {
            if (skin_->folder() == "dive-clock-2x2") {
                art.box(r.x, r.y, r.width, r.height, 1.5, 0x19364bff);
                art.strokeBox(r, 1.5, 0x91d7e2ff, {.width = .6});
            }
            skin_->label(art, std::to_wstring(state.day), r, r.height * .8, ink);
        }
    });
}
}  // namespace aeris_precision
