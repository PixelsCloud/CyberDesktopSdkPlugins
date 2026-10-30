#include <sdk/WindowsFonts.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

#include "plugins/mecha-desk/BasicClock.h"
#include "plugins/mecha-desk/BasicSpecs.h"

namespace aeris_basic {
const BasicSpec& basicSpec(std::string_view id) {
    for (const auto& spec : kBasicSpecs) {
        if (id == spec.id) {
            return spec;
        }
    }
    throw std::invalid_argument("Unknown AERIS POLAR basic clock");
}

cdsdk::Settings basicDefaults(const BasicSpec& spec) {
    auto settings = clockplugin::defaults(spec.clock());
    const CdSettingValue values[]{
        {sizeof(CdSettingValue), CD_SETTING_TOGGLE, "theme_colors", 0},
        {sizeof(CdSettingValue), CD_SETTING_TOGGLE, "date", spec.columns > 1},
        {sizeof(CdSettingValue), CD_SETTING_TOGGLE, "weekday", spec.columns > 1},
        {sizeof(CdSettingValue), CD_SETTING_TOGGLE, "seconds",
         !(spec.kind == BasicKind::Digital && spec.columns == 2 && spec.rows == 1)},
        {sizeof(CdSettingValue), CD_SETTING_COLOR, "text_color", 0x273e4dff},
        {sizeof(CdSettingValue), CD_SETTING_COLOR, "secondary_color", 0x89a5bcff},
        {sizeof(CdSettingValue), CD_SETTING_COLOR, "seconds_color", 0x91d7e2ff},
        {sizeof(CdSettingValue), CD_SETTING_FONT, "font_family", 0, 0, "Chakra Petch"},
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
    aerisskin::Skin skin;
    const BasicSpec& spec;

    explicit Impl(const CdCreateContext& context, const BasicSpec& value) : spec(value) {
        skin.bind(context);
    }
};

BasicFace::BasicFace(const CdCreateContext& c, const BasicSpec& s) : impl_(std::make_unique<Impl>(c, s)) {}

BasicFace::~BasicFace() = default;

void BasicFace::onChange(std::function<void()> c) {
    impl_->skin.onChange(std::move(c));
}

void BasicFace::pause(bool p) {
    impl_->skin.pause(p);
}

bool BasicFace::render(const cdsdk::Settings& settings, clockplugin::Time t, const clockplugin::FlipPose& pose,
                       const CdFrame& frame, CdRect safe) {
    auto& skin = impl_->skin;
    skin.appearance(settings);
    const auto options = clockplugin::options(settings, {39, 62, 77, 255});
    const int hour = options.twelveHour ? (t.hour % 12 ? t.hour % 12 : 12) : t.hour;
    return skin.render(frame, safe, [&](auto& c) {
        const auto r = skin.region("plot");
        const double second = t.second + (options.smooth ? t.millisecond / 1000. : 0);
        if (impl_->spec.kind == BasicKind::Analog) {
            const double radius = std::min(r.width, r.height) / 2, cx = r.x + r.width / 2, cy = r.y + r.height / 2;
            for (unsigned i = 0; i < 60; ++i) {
                const double a = i * std::numbers::pi / 30;
                const double inner = i % 5 ? .91 : .84;
                const double tick = radius * (i % 5 ? .009 : .022);
                c.line(cx + std::sin(a) * radius * inner, cy - std::cos(a) * radius * inner,
                       cx + std::sin(a) * radius * .96, cy - std::cos(a) * radius * .96, skin.foreground(0x273e4dcc),
                       skin.terminal() ? std::max(i % 5 ? .65 : 1., tick) : tick);
            }
            const auto hand = [&](double turn, double length, double weight, uint32_t color) {
                const double a = turn * 2 * std::numbers::pi;
                c.line(cx, cy, cx + std::sin(a) * radius * length, cy - std::cos(a) * radius * length,
                       skin.foreground(color), std::max(weight < .02 ? .8 : 1.15, radius * weight));
            };
            hand((hour % 12 + (t.minute + second / 60) / 60) / 12., .53, .038, 0x273e4dff);
            hand((t.minute + second / 60) / 60, .78, .026, 0x273e4dff);
            if (options.seconds) {
                hand(second / 60, .88, .009, 0x529bacff);
            }
            c.oval(cx, cy, radius * .035, radius * .035, 0x529bacff);
        }
        const auto full = std::format(L"{:02}:{:02}:{:02}", hour, t.minute, t.second);
        const auto shortTime = std::format(L"{:02}:{:02}", hour, t.minute);
        if (impl_->spec.kind == BasicKind::Lcd) {
            constexpr unsigned bits[]{0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f};
            constexpr double vertices[7][6][2]{
                {{.14, .07}, {.24, 0}, {.77, 0}, {.87, .07}, {.75, .16}, {.25, .16}},
                {{.88, .10}, {.96, .18}, {.94, .86}, {.86, .96}, {.78, .85}, {.80, .22}},
                {{.86, 1.04}, {.94, 1.13}, {.92, 1.83}, {.82, 1.91}, {.75, 1.80}, {.77, 1.16}},
                {{.12, 1.94}, {.22, 1.85}, {.74, 1.85}, {.84, 1.94}, {.73, 2}, {.23, 2}},
                {{.03, 1.12}, {.12, 1.04}, {.20, 1.15}, {.18, 1.80}, {.10, 1.91}, {.02, 1.83}},
                {{.05, .18}, {.15, .10}, {.22, .22}, {.20, .85}, {.12, .96}, {.04, .86}},
                {{.13, 1}, {.24, .92}, {.74, .92}, {.85, 1}, {.74, 1.08}, {.24, 1.08}}};
            const auto text = options.seconds ? full : shortTime;
            const double units = (options.seconds ? 6 : 4) * 1.14 + (options.seconds ? 2 : 1) * .35 - .14;
            const double w = std::min(r.height / 2, (r.width - 2) / units), h = w * 2;
            const double total = units * w;
            double x = r.x + (r.width - total) / 2;
            for (wchar_t digit : text) {
                if (digit == L':') {
                    for (double y : {.32, .68}) {
                        c.box(x, r.y + (r.height - h) / 2 + h * y, w * .13, w * .13, .5, skin.foreground(0x273e4dff));
                    }
                    x += w * .35;
                    continue;
                }
                for (unsigned i = 0; i < 7; ++i) {
                    auto path = c.path();
                    bool first = true;
                    for (const auto& p : vertices[i]) {
                        const cybercanvas::Point q{x + p[0] * w, r.y + (r.height - h) / 2 + p[1] * h / 2};
                        if (first) {
                            path.moveTo(q);
                            first = false;
                        } else {
                            path.lineTo(q);
                        }
                    }
                    path.close();
                    c.fill(path,
                           bits[digit - L'0'] & (1 << i) ? (skin.terminal() ? 0xf3f8faff : 0x193448ff) : 0xa5b5b91c);
                }
                x += w * 1.14;
            }
        } else if (impl_->spec.kind == BasicKind::Flip && skin.region("cell0").width > 0) {
            for (unsigned i = 0; i < 6; ++i) {
                if (i >= 4 && !options.seconds) {
                    continue;
                }
                const auto area = skin.region("cell" + std::to_string(i));
                const auto& transition = pose.values[i / 2];
                const auto digit = [i](int value) {
                    return i % 2 ? value % 10 : value / 10;
                };
                const double centre = area.y + area.height / 2;
                const double progress = transition.progress;
                const bool moving = progress > 0 && progress < 1;
                const auto half = [&](int value, bool upper, double projection = 1.) {
                    c.clipped({area.x, upper ? area.y : centre, area.width, area.height / 2}, [&] {
                        c.saved([&] {
                            c.translate(0, centre);
                            c.scale(1, std::max(.015, projection));
                            c.translate(0, -centre);
                            skin.label(c, std::to_wstring(digit(value)), area, area.height * .85, 0xf3f8faff);
                        });
                    });
                };
                half(transition.after, true);
                half(moving ? transition.before : transition.after, false);
                if (moving) {
                    const bool upper = progress < .5;
                    half(upper ? transition.before : transition.after, upper,
                         std::abs(std::cos(progress * std::numbers::pi)));
                }
            }
        } else {
            const std::array<int, 3> values{hour, t.minute, t.second};
            const auto first = skin.region("readout0");
            const bool groups = skin.region("readout1").width > 0;
            for (unsigned i = 0; i < (groups ? 3u : 1u); ++i) {
                if (i == 2 && !options.seconds) {
                    continue;
                }
                const auto area = skin.region("readout" + std::to_string(i));
                if (area.width <= 0) {
                    continue;
                }
                if (impl_->spec.kind == BasicKind::Analog) {
                    skin.label(c, options.seconds ? full : shortTime, area, area.height * .95);
                } else if (impl_->spec.kind == BasicKind::Flip) {
                    const auto& p = pose.values[i];
                    const double center = area.y + area.height / 2, progress = p.progress;
                    const bool moving = progress > 0 && progress < 1;
                    const auto half = [&](int value, bool upper, double projection = 1.) {
                        c.clipped({area.x, upper ? area.y : center, area.width, area.height / 2}, [&] {
                            c.saved([&] {
                                c.translate(0, center);
                                c.scale(1, std::max(.015, projection));
                                c.translate(0, -center);
                                skin.label(c, std::format(L"{:02}", value), area, area.height * .85, 0xd9f0e8ff);
                            });
                        });
                    };
                    half(p.after, true);
                    half(moving ? p.before : p.after, false);
                    if (moving) {
                        const bool upper = progress < .5;
                        half(upper ? p.before : p.after, upper, std::abs(std::cos(progress * std::numbers::pi)));
                    }
                } else {
                    const auto text = groups ? std::format(L"{:02}", values[i]) : (options.seconds ? full : shortTime);
                    skin.label(c, text, area, area.height * .85, 0xd9f0e8ff);
                }
            }
            if (first.width <= 0 && impl_->spec.kind != BasicKind::Analog) {
                skin.label(c, options.seconds ? full : shortTime, r, r.height * .85, 0xe9eeebff);
            }
        }
        constexpr const wchar_t* days[]{L"日", L"一", L"二", L"三", L"四", L"五", L"六"};
        for (unsigned i = 0; i < 2; ++i) {
            const auto f = skin.region("footer" + std::to_string(i));
            if ((!options.date && !options.weekday) || f.width <= 0) {
                continue;
            }
            std::wstring footer;
            if (i == 0 && options.date) {
                footer = skin.terminal() && f.width < 36 ? std::format(L"{:02}", t.day)
                                                         : std::format(L"{:02}/{:02}", t.month, t.day);
            }
            if (options.weekday && (i == 1 || (f.width >= 100 && skin.region("footer1").width <= 0) || !options.date)) {
                if (!footer.empty()) {
                    footer += L" · ";
                }
                footer += std::format(L"周{}", days[t.weekday % 7]);
            }
            // Compact clocks devote their tiny footer to the date alone.
            skin.label(c, footer, f, 11);
        }
    });
}
}  // namespace aeris_basic
