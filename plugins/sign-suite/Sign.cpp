#include "plugins/sign-suite/Sign.h"

namespace neon {
uint32_t opacity(uint32_t color, double value) {
    return (color & 0xFFFFFF00u) | uint32_t(std::clamp(value, 0., 1.) * (color & 255));
}

Sign::Sign(const CdCreateContext& context)
    : minimal_(context.plugin_id && std::string_view(context.plugin_id).ends_with(".1x4")) {
    redraw_.bind(context);
    canvas_.bindGpu(context);
    updates_.bind(context);
    updates_.onInvalidate([this] {
        invalidate();
    });
    updates_.onUpdate([this](cybercanvas::AnimationTick tick) {
        const auto effect = effectFromName(settings.at("effect").text);
        const int before = effect == Effect::Flicker ? faultAt(seconds_) : glitchAt(seconds_);
        seconds_ = std::fmod(seconds_ + tick.delta * settings.at("speed").number, 360.);
        const int after = effect == Effect::Flicker ? faultAt(seconds_) : glitchAt(seconds_);
        const bool sparse = effect == Effect::Flicker || effect == Effect::Glitch;
        const bool changed = !sparse || before != after;
        return cdsdk::UpdateStep{changed, true};
    });
    if (updates_.start(interval(settings)) != CD_OK) {
        throw std::runtime_error("Cannot start neon sign timer");
    }
}

auto Sign::invalidate() -> void {
    redraw_.invalidate();
}

auto Sign::layoutChanged(const CdLayout& layout) -> void {
    safe_ = layout.safe_content;
}

auto Sign::setPaused(bool paused) -> void {
    redraw_.pause(paused);
    updates_.pause(paused);
}

auto Sign::applySettings(const CdSettingValue* values, uint32_t count) -> CdResult {
    auto next = settings;
    const auto result = next.apply(values, count);
    if (result != CD_OK) {
        return result;
    }
    if (interval(next)) {
        const auto started = updates_.start(interval(next));
        if (started != CD_OK) {
            return started;
        }
    } else {
        updates_.stop();
        seconds_ = 0;
    }
    if (next.at("effect").text != settings.at("effect").text) {
        seconds_ = 0;
    }
    settings = std::move(next);
    return CD_OK;
}

auto Sign::render(const CdFrame& frame) -> CdResult {
    if (!canvas_.valid(frame)) {
        return CD_ERROR_ARGUMENT;
    }
    redraw_.beginFrame();
    updates_.beginFrame();
    canvas_.render(frame, safe_, [&](auto&, auto area) {
        if (area.width < 8 || area.height < 8) {
            return;
        }
        paint(area.width, area.height);
    });
    return CD_OK;
}

auto Sign::savePreview(const std::wstring& file, const CdFrame& frame) -> bool {
    return canvas_.save(file, frame);
}

auto Sign::interval(const cdsdk::Settings& settings) -> uint32_t {
    if (!settings.at("animation").integer || !settings.at("intensity").integer) {
        return 0;
    }
    const auto effect = effectFromName(settings.at("effect").text);
    if (effect == Effect::Flicker) {
        return settings.at("damage").integer ? 33 : 0;
    }
    return 33;
}

auto Sign::plate(double damage) -> void {
    auto& draw = canvas_;
    auto shape = draw.path();
    shape.moveTo({20, 16})
        .lineTo({324, 16})
        .lineTo({342, 34})
        .lineTo({342, 99})
        .lineTo({326, 115})
        .lineTo({36, 115})
        .lineTo({18, 97})
        .lineTo({18, 18})
        .close();
    draw.fill(shape, 0x0A111FFF);
    draw.stroke(shape, 0x4B596CFF, {.width = 1.5});
    draw.box(25, 23, 310, 80, 0, 0x101826FF);
    draw.box(30, 31, 300, 67, 0, 0x060D17FF);
    for (int row = 0; row < 20; ++row) {
        draw.box(31, 33 + row * 3, 298, .4, 0, 0x7591AF10);
    }
    for (int i = 0; i < int(damage * 46); ++i) {
        const int x = 31 + (i * 67 + 17) % 293;
        const int y = 26 + (i * 31 + 9) % 75;
        draw.box(x, y, 2 + i % 9, .7, 0, i % 3 ? 0x8697AE25 : 0x05070CBB);
    }
    // Flush metal fasteners and their scored heads are part of the artwork.
    for (auto point : {cybercanvas::Point{25, 24}, {329, 31}, {31, 100}, {328, 103}}) {
        draw.box(point.x - 2, point.y - 2, 5, 5, 0, 0x667185FF);
        draw.line(point.x - 1, point.y + 1, point.x + 2, point.y - 1, 0x18212DFF, .9);
    }
    if (damage > 0) {
        // Stepped chips, exposed copper and hairline cracks; never obscure the name.
        draw.box(286, 15, 15 * damage, 6, 0, 0x03070CFF);
        draw.box(292, 21, 5 * damage, 4, 0, 0x735447FF);
        draw.box(18, 74, 6, 18 * damage, 0, 0x03070CFF);
        auto crack = draw.path();
        crack.moveTo({319, 96}).lineTo({313, 103}).lineTo({318, 106}).lineTo({306, 114});
        draw.stroke(crack, opacity(0xA6B4CAFF, damage * .5), {.width = .7});
    }
}

auto Sign::lamp(double x, double y, double w, double h, uint32_t ink, double power, double glow) -> void {
    auto& draw = canvas_;
    for (int layer = 3; layer > 0 && glow > 0; --layer) {
        const double spread = layer * 1.25;
        draw.box(x - spread, y - spread, w + spread * 2, h + spread * 2, 1, opacity(ink, power * glow * .055));
    }
    draw.box(x, y, w, h, 0, opacity(ink, power));
    if (power > .2) {
        draw.box(x + .4, y + .35, std::max(.2, w - .8), std::min(.55, h * .3), 0, opacity(0xF2FCFFFF, power * .63));
    }
}

auto Sign::paint(double width, double height) -> void {
    auto& draw = canvas_;
    const double damage = settings.at("damage").integer / 100.;
    const double brightness = settings.at("brightness").integer / 100.;
    const double glow = settings.at("glow").integer / 100.;
    const uint32_t pink = uint32_t(settings.at("pink").integer), cyan = uint32_t(settings.at("cyan").integer);
    const auto effect = effectFromName(settings.at("effect").text);
    const double strength = settings.at("animation").integer ? settings.at("intensity").integer / 100. : 0;
    const bool fault =
        strength > 0 && damage > 0 && (effect == Effect::Flicker || effect == Effect::Mixed) && faultAt(seconds_);
    if (minimal_) {
        if (settings.at("plate").integer) {
            const double left = 1, top = 1, right = width - 3, bottom = height - 4;
            const double cut = std::min({9., (height - 5) * .18, (width - 4) * .08});
            auto outline = draw.path();
            outline.moveTo({left + cut, top});
            if (damage > 0 && width > 100) {
                // A real chipped-out edge, including transparency in the missing metal.
                outline.lineTo({right - 43, top})
                    .lineTo({right - 43, top + 2 * damage})
                    .lineTo({right - 43 + 10 * damage, top + 2 * damage})
                    .lineTo({right - 43 + 10 * damage, top});
            }
            outline.lineTo({right - cut, top})
                .lineTo({right, top + cut})
                .lineTo({right, bottom - cut})
                .lineTo({right - cut, bottom})
                .lineTo({left + cut, bottom})
                .lineTo({left, bottom - cut})
                .lineTo({left, top + cut})
                .close();
            draw.fill(outline, 0x09111EEB);
            draw.stroke(outline, 0x50607790, {.width = .7});
        }
        lamp(12, 9, 35, 1, pink, brightness * .6, glow);
        lamp(width - 48, height - 13, 30 - damage * 8, 1, cyan, brightness * .6, glow);
    } else {
        // Stretch only the metalwork to the card bounds, never the square pixels.
        draw.saved([&] {
            draw.translate(1, 1);
            draw.scale((width - 4) / 324., (height - 5) / 99.);
            draw.translate(-18, -16);
            if (settings.at("plate").integer) {
                plate(damage);
            }
            // Asymmetric neon rails, with a disconnected tail on the right.
            lamp(32, 21, 113, 2, pink, brightness, glow);
            lamp(149, 21, 9, 2, pink, brightness * .3, glow);
            lamp(170, 21, 146 - damage * 24, 2, cyan, brightness, glow);
            lamp(335, 39, 2, 34, cyan, brightness, glow);
            lamp(335, 78, 2, 15, cyan, brightness * (fault ? .12 : .7), glow);
            lamp(43, 109, 70, 2, pink, brightness * .6, glow);
            lamp(247, 109, 68, 2, cyan, brightness * .6, glow);
            if (damage > .15) {
                draw.line(337, 94, 343, 101, 0x382B35FF, 1);
                draw.line(343, 101, 339, 111, 0x99624CFF, .8);
                lamp(339, 112, 2, 5, pink, brightness * (fault ? .1 : .4), glow);
            }
        });
    }
    constexpr double pitch = 3.95;
    constexpr double wordWidth = 70 * pitch + 3.35, wordHeight = 6 * pitch + 3.35;
    const double textScale =
        std::max(.01, std::min((width - 24) / wordWidth, (minimal_ ? height - 30 : height * .42) / wordHeight));
    draw.saved([&] {
        draw.translate((width - wordWidth * textScale) / 2, (height - wordHeight * textScale) / 2);
        draw.scale(textScale, textScale);
        for (size_t letter = 0; letter < lettering.size(); ++letter) {
            const uint32_t ink = letter < 5 ? pink : cyan;
            for (int row = 0; row < 7; ++row) {
                for (int col = 0; col < 5; ++col) {
                    if (!(lettering[letter][row] & (1u << (4 - col)))) {
                        continue;
                    }
                    const auto light = sampleLight(effect, seconds_, (letter * 6 + col) / 71., row, strength, damage);
                    const double x = (letter * 6 + col) * pitch + light.offset, y = row * pitch;
                    const unsigned seed = unsigned(letter * 113 + row * 29 + col * 71);
                    const bool broken = seed % 101 < damage * 7;
                    draw.box(x + .7, y + 1, 3.35, 3.35, 0, 0x020407DD);
                    if (broken) {
                        draw.box(x, y, 3.35, 3.35, 0, opacity(ink, .10));
                        draw.box(x, y + 2.6, 1.4, .6, 0, 0x7B536655);
                        continue;
                    }
                    const double power = brightness * light.power * (seed % 13 == 0 ? 1 - damage * .65 : 1);
                    if (std::abs(light.offset) > .1) {
                        draw.box(x - light.offset * 2, y, 3.35, 3.35, 0,
                                 opacity(letter < 5 ? cyan : pink, strength * .35));
                    }
                    lamp(x, y, 3.35, 3.35, ink, power, glow);
                    if (light.highlight > .01) {
                        draw.box(x + .2, y + .2, 2.95, 2.95, 0,
                                 opacity(0xE8FCFFFF, brightness * light.highlight * .65));
                    }
                }
            }
        }
    });
    if (minimal_) {
        return;
    }
    cybercanvas::TextStyle micro;
    micro.family = L"Consolas";
    micro.size = std::clamp(width / 60., 5., 9.);
    micro.align = cybercanvas::TextAlign::Start;
    draw.text(L"NIGHT DISTRICT  /  TERMINAL 01", micro, {18, height * .13, width - 36, 12}, 0x788FA8FF);
    draw.text(L"CD-2077   //   INDEPENDENT DESKTOP", micro, {18, height * .76, width - 95, 12}, 0x8698AEFF);
    draw.box(width - 66, height * .76 + 4, 4, 4, 0, opacity(0xFFB75CFF, brightness * (fault ? .25 : 1)));
    draw.text(L"ONLINE", micro, {width - 56, height * .76, 48, 12}, 0xFFBC75FF);
    if (height < 100) {
        return;
    }
    for (int i = 0; i < 15; ++i) {
        draw.box(width * .32 + i * 2.5, height - 12, i % 3 == 0 ? 1.5 : .6, 5, 0, 0x73859B77);
    }
    draw.text(L"HIGH VOLTAGE", micro, {width * .52, height - 16, width * .25, 12}, 0xA1AAB8FF);
}
}  // namespace neon
