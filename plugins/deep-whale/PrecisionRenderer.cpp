#include <cmath>
#include <format>
#include <numbers>

#include "controllers/precision/Clock.h"

namespace deep_precision {
using whaleskin::Canvas;

Renderer::Renderer(Kind kind, const std::filesystem::path& assets)
    : skin_(std::make_unique<whaleskin::Skin>(assets)), kind_(kind) {}

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

whaleskin::View Renderer::view(CdRect safe, cybercanvas::Size world) {
    return skin_->view(safe, world);
}

void Renderer::draw(Canvas& c, const State& state, const Options& options, double) {
    if (options.background) {
        skin_->worldBackdrop(c, false);
    }
    const bool dress = kind_ == Kind::Dress, diver = kind_ == Kind::Green;
    const uint32_t metal = dress ? 0xd5a681ff : 0xc3d6dcff, ink = dress ? 0x795e50ff : 0x34546bff;
    const double radius = diver ? 376 : 478;
    for (unsigned i = 0; i < 60; ++i) {
        const double a = i * std::numbers::pi / 30;
        const double length = i % 5 ? 14 : 48;
        c.line(500 + std::sin(a) * (radius - length), 500 - std::cos(a) * (radius - length), 500 + std::sin(a) * radius,
               500 - std::cos(a) * radius, i % 5 ? ink : metal, i % 5 ? 3.2 : 9.5);
    }
    if (dress) {
        skin_->label(c, L"PATEK PHILIPPE", {275, 265, 450, 42}, 38, 0x55515bff, false);
        skin_->label(c, L"GENEVE", {365, 314, 270, 30}, 24, 0x55515bff, false);
        if (options.detail) {
            c.oval(500, 760, 118, 90, 0x243754ff);
            c.clipped({383, 672, 234, 88}, [&] {
                const double offset = (state.lunarPhase * 2 - 1) * 155;
                c.oval(500 + offset, 725, 43, 43, 0xe4ca91ff);
                c.oval(515 + offset, 714, 40, 40, 0x243754ff);
            });
            skin_->label(c, std::to_wstring(state.day), {410, 820, 180, 135}, 125, ink, false);
        }
    } else if (diver) {
        skin_->label(c, L"ROLEX", {330, 306, 340, 44}, 42, 0xe6eee1ff, false);
        skin_->label(c, L"SUBMARINER", {300, 632, 400, 32}, 27, 0xe6eee1ff, false);
        if (options.detail) {
            c.gradientBox({715, 430, 150, 140}, 5, 0xfffff1ff, 0xcfded9ff);
            skin_->label(c, std::to_wstring(state.day), {720, 435, 140, 130}, 125, 0x34546bff, false);
        }
    } else if (options.detail) {
        constexpr std::array<cybercanvas::Point, 6> centers{
            {{290, 310}, {465, 255}, {643, 350}, {690, 540}, {477, 645}, {300, 540}}};
        constexpr std::array<unsigned, 6> teeth{45, 30, 44, 47, 28, 40};
        for (unsigned i = 0; i < centers.size(); ++i) {
            c.saved([&] {
                const auto p = centers[i];
                c.translate(p.x, p.y);
                c.rotate(gearAngle(state.mechanismSeconds, teeth[i], i % 2));
                const double r = i % 2 ? 85 : 108;
                skin_->part(c, "gear", {-r, -r, r * 2, r * 2});
            });
        }
    }
    const auto hand = [&](double angle, double length, double width, uint32_t color) {
        c.saved([&] {
            c.translate(500, 500);
            c.rotate(angle);
            auto p = c.path();
            p.moveTo({0, -length})
                .lineTo({width, -length * .17})
                .lineTo({width * .4, 48})
                .lineTo({-width * .4, 48})
                .lineTo({-width, -length * .17})
                .close();
            c.fill(p, color);
            c.stroke(p, ink, {.width = 3});
            c.line(0, -length + 12, 0, 20, 0xfff8e0ff, 2.5);
            if (diver && length < 280) {
                c.oval(0, -170, 31, 31, 0xe9f1dfff, 5);
            }
        });
    };
    hand(state.hour, diver ? 252 : 269, diver ? 18 : 17, handColor_ ? handColor_ : metal);
    hand(state.minute, diver ? 332 : 390, diver ? 11 : 12, handColor_ ? handColor_ : metal);
    if (options.seconds) {
        c.saved([&] {
            c.translate(500, 500);
            c.rotate(state.second);
            c.line(0, -(diver ? 354 : 425), 0, 64,
                   secondsColor_ ? secondsColor_
                   : dress       ? 0x96705fff
                                 : 0xe5b29dff,
                   4.2);
            c.oval(0, 52, 11, 11, metal);
        });
    }
    c.oval(500, 500, 17.5, 17.5, metal);
    c.oval(497, 496, 5.5, 5.5, 0xfffaf0ff);
}
}  // namespace deep_precision
