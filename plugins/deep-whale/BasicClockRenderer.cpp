#include <sdk/WindowsFonts.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

#include "plugins/deep-whale/BasicClock.h"
#include "plugins/deep-whale/BasicSpecs.h"

namespace deep_basic {
const BasicSpec& basicSpec(std::string_view id) {
    for (const auto& spec : kBasicSpecs) {
        if (id == spec.id) {
            return spec;
        }
    }
    throw std::invalid_argument("Unknown Deep Whale basic clock");
}

cdsdk::Settings basicDefaults(const BasicSpec& spec) {
    auto settings = clockplugin::defaults(spec.clock());
    const CdSettingValue values[]{
        {sizeof(CdSettingValue), CD_SETTING_TOGGLE, "theme_colors", 0},
        {sizeof(CdSettingValue), CD_SETTING_TOGGLE, "date", 0},
        {sizeof(CdSettingValue), CD_SETTING_TOGGLE, "weekday", 0},
        {sizeof(CdSettingValue), CD_SETTING_TOGGLE, "seconds", spec.kind == BasicKind::Analog || spec.columns == 4},
        {sizeof(CdSettingValue), CD_SETTING_COLOR, "text_color", 0x3f5b89ff},
        {sizeof(CdSettingValue), CD_SETTING_COLOR, "secondary_color", 0x89a5bcff},
        {sizeof(CdSettingValue), CD_SETTING_COLOR, "seconds_color", 0xbfa57dff},
        {sizeof(CdSettingValue), CD_SETTING_FONT, "font_family", 0, 0, "Bree Serif"},
        {sizeof(CdSettingValue), CD_SETTING_COLOR, "card_color", spec.kind == BasicKind::Lcd ? 0xe1edf1ff : 0xf8f8f8ff},
        {sizeof(CdSettingValue), CD_SETTING_INTEGER, "background_corner_radius", spec.kind == BasicKind::Lcd ? 2 : 5},
        {sizeof(CdSettingValue), CD_SETTING_CHOICE, "format", 0, 0, "24"}};
    if (settings.apply(values, uint32_t(std::size(values))) != CD_OK) {
        throw std::logic_error("Invalid themed clock defaults");
    }
    return settings;
}

namespace {
uint32_t rgba(CdColor c) {
    return (uint32_t(c.r) << 24) | (uint32_t(c.g) << 16) | (uint32_t(c.b) << 8) | c.a;
}
}  // namespace

struct BasicFace::Impl {
    whaleskin::Skin skin;
    const BasicSpec& spec;
    clockplugin::Renderer lcd;

    explicit Impl(const CdCreateContext& context, const BasicSpec& value) : spec(value) {
        skin.bind(context);
        if (spec.kind == BasicKind::Lcd) {
            lcd.privateFonts(std::filesystem::u8path(context.plugin_directory) / "assets/BreeSerif-Regular.ttf",
                             cdsdk::bundledFontPath(L"assets/fonts/DeepWhale/LXGWWenKaiGBScreen.ttf"), "Bree Serif");
        }
    }
};

BasicFace::BasicFace(const CdCreateContext& context, const BasicSpec& spec)
    : impl_(std::make_unique<Impl>(context, spec)) {}

BasicFace::~BasicFace() = default;

void BasicFace::onChange(std::function<void()> callback) {
    impl_->skin.onChange(std::move(callback));
}

void BasicFace::pause(bool paused) {
    impl_->skin.pause(paused);
}

bool BasicFace::render(const cdsdk::Settings& settings, clockplugin::Time time, const clockplugin::FlipPose& pose,
                       const CdFrame& frame, CdRect safe) {
    impl_->skin.appearance(settings);
    const auto options = clockplugin::options(settings, {63, 91, 137, 255});
    return impl_->skin.render(frame, safe, [&](auto& c) {
        auto& skin = impl_->skin;
        const auto full = skin.region("plot");
        auto r = full;
        const double artScale = std::max(.001, std::min((safe.width - 14) / (impl_->spec.columns * 80. - 14),
                                                        (safe.height - 14) / (impl_->spec.rows * 100. - 14)));
        const double displayHeight = full.height * artScale;
        const bool footer =
            (options.date || options.weekday) && displayHeight >= (impl_->spec.kind == BasicKind::Analog ? 60 : 48);
        const bool compactFooter = displayHeight < 80;
        if (footer && impl_->spec.kind != BasicKind::Analog) {
            r.height *= compactFooter ? .66 : .78;
        }
        const auto drawFooter = [&] {
            if (footer) {
                const bool analog = impl_->spec.kind == BasicKind::Analog;
                const cybercanvas::Rect area =
                    analog ? cybercanvas::Rect{full.x + full.width * .16, full.y + full.height * .69, full.width * .68,
                                               full.height * .16}
                           : cybercanvas::Rect{full.x, full.y + full.height * (compactFooter ? .70 : .80), full.width,
                                               full.height * (compactFooter ? .30 : .20)};
                if (analog) {
                    c.gradientBox(area, 1, 0xfffcf3ee, 0xe1edf3ee);
                }
                auto text =
                    clockplugin::dateCaption(time, options, analog || impl_->spec.columns <= 2 || full.width < 150);
                if (analog && impl_->spec.columns == 1) {
                    const auto weekday = text.find(L"  周");
                    if (weekday != std::wstring::npos) {
                        text.replace(weekday, 3, L"(");
                        text += L")";
                    }
                }
                skin.caption(c, text, area, analog ? full.height * .115 : area.height * .80, rgba(options.secondary));
            }
        };
        const auto ink = rgba(options.primary), secondary = rgba(options.secondary), accent = rgba(options.accent);
        const int hour = options.twelveHour ? (time.hour % 12 ? time.hour % 12 : 12) : time.hour;
        if (impl_->spec.kind == BasicKind::Analog) {
            const double radius = std::min(r.width, r.height) * .48 * std::min(1., options.sizePercent / 100),
                         cx = r.x + r.width / 2, cy = r.y + r.height / 2;
            for (unsigned i = 0; i < 60; ++i) {
                const double a = i * std::numbers::pi / 30, inner = i % 5 ? .91 : .79;
                c.line(cx + std::sin(a) * radius * inner, cy - std::cos(a) * radius * inner, cx + std::sin(a) * radius,
                       cy - std::cos(a) * radius, i % 5 ? secondary : accent, i % 5 ? .35 : .75);
            }
            drawFooter();
            const double second = time.second + (options.smooth ? time.millisecond / 1000. : 0);
            const auto hand = [&](double turns, double length, double width, uint32_t color) {
                c.saved([&] {
                    c.translate(cx, cy);
                    c.rotate(turns * 360);
                    auto path = c.path();
                    path.moveTo({0, -radius * length})
                        .lineTo({width, 0})
                        .lineTo({0, radius * .12})
                        .lineTo({-width, 0})
                        .close();
                    if (options.reliefPercent > 0) {
                        c.saved([&] {
                            c.translate(radius * .012, radius * .012);
                            c.fill(path, 0x16365300 | uint32_t(options.reliefPercent * .65));
                        });
                    }
                    c.fill(path, color);
                });
            };
            hand((time.hour % 12 + (time.minute + second / 60) / 60) / 12., .57, radius * .045, ink);
            hand((time.minute + second / 60) / 60., .85, radius * .032, ink);
            if (options.seconds) {
                hand(second / 60., .91, std::max(.23, radius * .009), accent);
            }
            c.oval(cx, cy, radius * .065, radius * .065, accent);
            return;
        }
        if (impl_->spec.kind == BasicKind::Lcd) {
            auto display = options;
            display.themeColors = false;
            display.date = display.weekday = false;
            impl_->lcd.drawElectronic(c, impl_->spec.clock(), display, time, r);
            drawFooter();
            return;
        }
        if (impl_->spec.kind == BasicKind::Flip) {
            const unsigned count = options.seconds ? 6 : 4, groups = count / 2;
            const double separator = r.width * .032, unit = (r.width - (groups - 1) * separator) / count;
            const std::array<std::wstring, 10> references{L"0", L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8", L"9"};
            for (unsigned i = 0; i < count; ++i) {
                const auto& pair = pose.values[i / 2];
                const auto digit = [&](int value) {
                    return std::to_wstring(i % 2 ? value % 10 : value / 10);
                };
                const cybercanvas::Rect card{r.x + unit * (i + .06) + (i / 2) * separator, r.y, unit * .88, r.height};
                const double center = card.y + card.height / 2;
                const auto half = [&](const std::wstring& value, bool upper, double projection = 1.) {
                    c.clipped({card.x, upper ? card.y : center, card.width, card.height / 2}, [&] {
                        c.saved([&] {
                            c.translate(0, center);
                            c.scale(1, std::max(.003, projection));
                            c.translate(0, -center);
                            c.gradientBox(card, std::min(4., options.cornerRadius * .3), rgba(options.cardColor),
                                          rgba(options.cardColor));
                            skin.label(c, value, card, card.height * .87 * options.sizePercent / 100, ink, false,
                                       references);
                        });
                    });
                };
                const double progress = pair.progress;
                const bool moving = progress > 0 && progress < 1;
                half(digit(pair.after), true);
                half(digit(moving ? pair.before : pair.after), false);
                if (moving) {
                    const bool upper = progress < .5;
                    half(digit(upper ? pair.before : pair.after), upper,
                         std::abs(std::cos(progress * std::numbers::pi)));
                }
                c.strokeBox(card, 1.5, 0xcbb086ff, {.width = .6});
                c.line(card.x, center, card.x + card.width, center, 0x8aa4bd99, .45);
            }
            for (unsigned group = 1; group < groups; ++group) {
                const double x = r.x + group * 2 * unit + (group - .5) * separator;
                for (int side : {-1, 1}) {
                    c.oval(x, r.y + r.height * (.5 + side * .13), std::max(.65, separator * .18),
                           std::max(.65, separator * .18), secondary);
                }
            }
            drawFooter();
            return;
        }
        auto clock = r;
        const bool stacked = options.layout == "stacked" || (options.layout == "auto" && impl_->spec.columns == 1);
        const bool showSeconds = options.seconds && (impl_->spec.columns != 1 || stacked || !options.adaptiveSeconds);
        if (stacked) {
            const unsigned count = showSeconds ? 3 : 2;
            const std::array<int, 3> numbers{hour, time.minute, time.second};
            const std::array<std::wstring, 1> reference{L"88"};
            for (unsigned i = 0; i < count; ++i) {
                skin.label(c, std::format(L"{:02}", numbers[i]),
                           {clock.x, clock.y + i * clock.height / count, clock.width, clock.height / count},
                           clock.height / count * .9 * options.sizePercent / 100, ink, false, reference);
            }
        } else {
            const auto text = std::format(L"{:02}:{:02}", hour, time.minute) +
                              (showSeconds ? std::format(L":{:02}", time.second) : L"");
            const std::array<std::wstring, 1> reference{showSeconds ? L"88:88:88" : L"88:88"};
            skin.label(c, text, clock, clock.height * .87 * options.sizePercent / 100, ink, false, reference);
        }
        drawFooter();
    });
}
}  // namespace deep_basic
