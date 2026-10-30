#include "plugins/mechanical-desk/BasicClock.h"

#include <sdk/Language.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

#include "plugins/mechanical-desk/BasicClockLayouts.h"
#include "plugins/mechanical-desk/Skin.h"

namespace mechanicaldesk {
const BasicSpec& basicSpec(std::string_view id) {
    for (const auto& spec : kBasicClocks) {
        if (id == spec.id) {
            return spec;
        }
    }
    throw std::invalid_argument("Unknown mechanical clock identity");
}

const BasicSpec& basicFolder(std::string_view folder) {
    for (const auto& spec : kBasicClocks) {
        if (folder == spec.folder) {
            return spec;
        }
    }
    throw std::invalid_argument("Unknown mechanical clock layout");
}

cdsdk::Settings basicDefaults(const BasicSpec& spec) {
    auto result = clockplugin::defaults(spec.clock());
    const bool light = spec.kind == BasicKind::Analog || spec.kind == BasicKind::Lcd;
    const CdSettingValue values[]{
        {sizeof(CdSettingValue), CD_SETTING_TOGGLE, "theme_colors", 0},
        {sizeof(CdSettingValue), CD_SETTING_TOGGLE, "date", 0},
        {sizeof(CdSettingValue), CD_SETTING_TOGGLE, "weekday", 0},
        {sizeof(CdSettingValue), CD_SETTING_TOGGLE, "seconds", spec.kind == BasicKind::Analog || spec.columns == 4},
        {sizeof(CdSettingValue), CD_SETTING_COLOR, "text_color", light ? kInk : kIvory},
        {sizeof(CdSettingValue), CD_SETTING_COLOR, "secondary_color", light ? kInk : kSteel},
        {sizeof(CdSettingValue), CD_SETTING_COLOR, "seconds_color", 0xb18145ff},
        {sizeof(CdSettingValue), CD_SETTING_FONT, "font_family", 0, 0, "Bahnschrift"},
        {sizeof(CdSettingValue), CD_SETTING_CHOICE, "format", 0, 0, "24"},
    };
    if (result.apply(values, uint32_t(std::size(values))) != CD_OK) {
        throw std::logic_error("Mechanical clock defaults rejected");
    }
    return result;
}

namespace {
void hand(Canvas& canvas, Point center, double angle, double radius, double width, uint32_t color) {
    const double a = (angle - 90) * std::numbers::pi / 180;
    canvas.line(center.x - std::cos(a) * radius * .12, center.y - std::sin(a) * radius * .12,
                center.x + std::cos(a) * radius, center.y + std::sin(a) * radius, color, width);
}

void lcd(Canvas& canvas, std::string_view value, Rect area, uint32_t color) {
    constexpr unsigned codes[]{0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f};
    double units = -.2;
    for (const auto c : value) {
        units += c == ':' ? .45 : 1.25;
    }
    const double u = std::min(area.width / units, area.height / 2), t = u * .12;
    double x = area.x + (area.width - units * u) / 2;
    const double y = area.y + (area.height - 2 * u) / 2;
    for (const auto c : value) {
        if (c == ':') {
            canvas.oval(x + .15 * u, y + .65 * u, t / 2, t / 2, color);
            canvas.oval(x + .15 * u, y + 1.35 * u, t / 2, t / 2, color);
            x += .45 * u;
            continue;
        }
        const std::array<Rect, 7> segments{{{x + t, y, u - 2 * t, t},
                                            {x + u - t, y + t, t, u - t},
                                            {x + u - t, y + u, t, u - t},
                                            {x + t, y + 2 * u - t, u - 2 * t, t},
                                            {x, y + u, t, u - t},
                                            {x, y + t, t, u - t},
                                            {x + t, y + u - t / 2, u - 2 * t, t}}};
        for (unsigned i = 0; i < segments.size(); ++i) {
            if (c >= '0' && c <= '9' && codes[c - '0'] & (1u << i)) {
                canvas.box(segments[i].x, segments[i].y, segments[i].width, segments[i].height, t * .35, color);
            }
        }
        x += 1.25 * u;
    }
}

Rect inset(Rect area, double amount) {
    return {area.x + area.width * (1 - amount) / 2, area.y + area.height * (1 - amount) / 2, area.width * amount,
            area.height * amount};
}
}  // namespace

struct BasicFace::Impl {
    const BasicSpec& spec;
    Skin skin;
    CdColor themeText{244, 237, 218, 255};

    Impl(const CdCreateContext& context, const BasicSpec& value) : spec(value) {
        skin.bind(context);
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

void BasicFace::themeText(CdColor color) {
    impl_->themeText = color;
}

bool BasicFace::ready() const {
    return impl_->skin.ready();
}

uint64_t BasicFace::cacheBuilds() const {
    return impl_->skin.revision();
}

double BasicFace::panelExtension() const {
    return 0;
}

bool BasicFace::save(const std::filesystem::path& path, const CdFrame& frame) {
    return impl_->skin.save(path, frame);
}

bool BasicFace::renderContent(const CdFrame& frame, CdRect safe, const std::function<void(Canvas&)>& draw,
                              const std::function<void(Canvas&)>& overlay) {
    return impl_->skin.render(frame, safe, [&](Canvas& canvas) {
        canvas.saved([&] {
            canvas.translate(-impl_->spec.visible.x, -impl_->spec.visible.y);
            draw(canvas);
        });
        if (overlay) {
            overlay(canvas);
        }
    });
}

bool BasicFace::render(const cdsdk::Settings& settings, clockplugin::Time time, const clockplugin::FlipPose& pose,
                       const CdFrame& frame, CdRect safe) {
    const cdsdk::LanguageScope language(settings.at("language").text);
    const auto options = clockplugin::options(settings, impl_->themeText);
    const auto& spec = impl_->spec;
    const auto packed = [](CdColor color) {
        return uint32_t(color.r) << 24 | uint32_t(color.g) << 16 | uint32_t(color.b) << 8 | color.a;
    };
    const auto primary = packed(options.primary);
    const auto secondary = packed(options.secondary);
    const int hour = options.twelveHour ? (time.hour % 12 ? time.hour % 12 : 12) : time.hour;
    const auto family = impl_->skin.font(settings.at("font_family").text);
    return renderContent(frame, safe, [&](Canvas& canvas) {
        const auto label = [&](const std::wstring& value, Rect bounds, double size, uint32_t color = 0) {
            impl_->skin.label(canvas, value, bounds, size, color ? color : primary, true, family);
        };
        if (spec.kind == BasicKind::Analog) {
            const double second = time.second + (options.smooth ? time.millisecond / 1000. : 0);
            const double f = options.sizePercent / 100;
            hand(canvas, spec.center, (hour % 12 + time.minute / 60.) * 30, spec.radius * .59 * f, spec.radius * .029,
                 primary);
            hand(canvas, spec.center, (time.minute + second / 60.) * 6, spec.radius * .82 * f, spec.radius * .021,
                 primary);
            if (options.seconds) {
                hand(canvas, spec.center, second * 6, spec.radius * .88 * f, spec.radius * .007,
                     uint32_t(settings.at("seconds_color").integer));
            }
            canvas.oval(spec.center.x, spec.center.y, spec.radius * .035, spec.radius * .035, kBrass);
            canvas.oval(spec.center.x, spec.center.y, spec.radius * .013, spec.radius * .013, kInk);
            if (options.date || options.weekday) {
                label(clockplugin::dateCaption(time, options, true),
                      {spec.center.x - spec.radius * .5, spec.center.y + spec.radius * .35, spec.radius,
                       spec.radius * .2},
                      spec.radius * .15, secondary);
            }
            return;
        }
        if (spec.kind == BasicKind::Flip) {
            for (unsigned i = 0; i < spec.readoutCount; ++i) {
                const auto channel = i / 2;
                if (channel == 2 && !options.seconds) {
                    continue;
                }
                const auto area = inset(spec.readouts[i], options.sizePercent / 100);
                const auto& value = pose.values[channel];
                const auto glyph = [&](int number) {
                    return std::wstring(1, wchar_t(L'0' + (i % 2 ? number % 10 : number / 10)));
                };
                const auto paint = [&](int number) {
                    label(glyph(number), area, area.height * .95);
                };
                const double middle = area.y + area.height / 2;
                if (value.before == value.after || value.progress >= 1) {
                    paint(value.after);
                } else {
                    canvas.clipped({area.x, area.y, area.width, area.height / 2}, [&] {
                        paint(value.after);
                    });
                    canvas.clipped({area.x, middle, area.width, area.height / 2}, [&] {
                        paint(value.before);
                    });
                    const bool front = value.progress < .5;
                    const double stretch = std::abs(std::cos(value.progress * std::numbers::pi));
                    if (stretch > .001) {
                        canvas.clipped({area.x, front ? area.y : middle, area.width, area.height / 2}, [&] {
                            canvas.saved([&] {
                                canvas.translate(0, middle);
                                canvas.scale(1, stretch);
                                canvas.translate(0, -middle);
                                canvas.box(area.x, area.y, area.width, area.height, 0, 0x101415ff);
                                paint(front ? value.before : value.after);
                            });
                        });
                    }
                }
                canvas.line(area.x, middle, area.x + area.width, middle, 0x030506ff, 1);
            }
            return;
        }
        auto area = inset(spec.readouts[0], options.sizePercent / 100);
        const auto caption = clockplugin::dateCaption(time, options, spec.columns < 4);
        if (!caption.empty()) {
            const double h = area.height * .22;
            label(caption, {area.x, area.y + area.height - h, area.width, h}, h * .85, secondary);
            area.height -= h * 1.2;
        }
        const bool stacked = spec.columns == 1 || options.layout == "stacked";
        if (stacked) {
            const unsigned rows = options.seconds ? 3 : 2;
            const int values[]{hour, time.minute, time.second};
            for (unsigned i = 0; i < rows; ++i) {
                label(std::format(L"{:02}", values[i]),
                      {area.x, area.y + i * area.height / rows, area.width, area.height / rows * .9},
                      area.height / rows * .88);
            }
        } else {
            const auto value = options.seconds ? std::format("{:02}:{:02}:{:02}", hour, time.minute, time.second)
                                               : std::format("{:02}:{:02}", hour, time.minute);
            if (spec.kind == BasicKind::Lcd) {
                lcd(canvas, value, area, primary);
            } else {
                label(std::wstring(value.begin(), value.end()), area, area.height * .9);
            }
        }
    });
}
}  // namespace mechanicaldesk

namespace mechanicaldesk {
auto BasicSpec::clock() const -> clockplugin::Spec {
    return {id,
            kind == BasicKind::Analog,
            int(rows),
            int(columns),
            kind == BasicKind::Flip,
            kind == BasicKind::Lcd ? clockplugin::Display::Lcd : clockplugin::Display::Classic};
}
}  // namespace mechanicaldesk
