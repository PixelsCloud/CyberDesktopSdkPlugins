#include <cmath>
#include <format>
#include <numbers>

#include "controllers/calendar/Calendar.h"
#include "controllers/hourglass/MechanicalHourglass.h"
#include "controllers/lunar/Lunar.h"
#include "controllers/weather/Weather.h"
#include "plugins/redline-desk/Skin.h"

namespace {
using redline::Canvas;
using redline::Rect;
using redline::Skin;

void field(Skin& skin, Canvas& canvas, std::string_view name, const std::wstring& text, double factor = 1.02) {
    const auto r = skin.region(name);
    if (name == "day") {
        factor = 1.30;
    }
    if (name == "temperature" && text.find(L'°') != std::wstring::npos) {
        const auto split = text.find(L'°');
        if (r.width <= 0 || r.height <= 0) {
            return;
        }
        if (skin.folder() != "weather-4x2") {
            skin.clear(canvas, r);
        }
        cybercanvas::TextStyle large, unitStyle;
        large.family = unitStyle.family = L"Bahnschrift";
        large.size = float(r.height * 1.16);
        unitStyle.size = float(r.height * .51);
        large.align = unitStyle.align = cybercanvas::TextAlign::Center;
        large.vertical = unitStyle.vertical = cybercanvas::VerticalAlign::Center;
        const auto digits = text.substr(0, split), unit = text.substr(split);
        auto number = canvas.measureTextInk(digits, large), suffix = canvas.measureTextInk(unit, unitStyle);
        const double gap = r.height * .04;
        const double fit = std::min({1., r.width / (number.width + suffix.width + gap), r.height / number.height});
        large.size *= float(fit * .96);
        unitStyle.size *= float(fit * .96);
        number = canvas.measureTextInk(digits, large);
        suffix = canvas.measureTextInk(unit, unitStyle);
        const double x = r.x + (r.width - number.width - suffix.width - gap) / 2;
        const auto draw = [&](const std::wstring& value, cybercanvas::TextStyle style, Rect area) {
            const auto fitted = canvas.fitTextInk(value, style, area);
            canvas.ink(redline::kIvory);
            canvas.target->DrawTextLayout(D2D1::Point2F(float(fitted.origin.x), float(fitted.origin.y)),
                                          fitted.layout.Get(), canvas.brush.Get());
        };
        canvas.clipped(r, [&] {
            draw(digits, large, {x, r.y, number.width, r.height});
            draw(unit, unitStyle,
                 {x + number.width + gap, r.y + (r.height - number.height) / 2, suffix.width, suffix.height});
        });
        return;
    }
    if (name == "festival" && skin.textured() && skin.rows() < 4 && !text.empty()) {
        auto badge = canvas.path();
        badge.moveTo({r.x, r.y + r.height / 2})
            .lineTo({r.x + r.height * .24, r.y})
            .lineTo({r.x + r.width - r.height * .24, r.y})
            .lineTo({r.x + r.width, r.y + r.height / 2})
            .lineTo({r.x + r.width - r.height * .24, r.y + r.height})
            .lineTo({r.x + r.height * .24, r.y + r.height})
            .close();
        canvas.fill(badge, 0xb52c25ff);
        canvas.stroke(badge, redline::kRuby, {.width = std::max(.7, r.height * .025)});
    }
    skin.label(canvas, text, r, r.height * factor, redline::kIvory,
               name != "festival" && skin.folder() != "weather-4x2");
}

void gridLines(Canvas& canvas, Rect r, unsigned columns, unsigned rows) {
    for (unsigned i = 0; i <= columns; ++i) {
        const double x = r.x + r.width * i / columns;
        canvas.line(x, r.y, x, r.y + r.height, 0xa18b6377, .8);
    }
    for (unsigned i = 0; i <= rows; ++i) {
        const double y = r.y + r.height * i / rows;
        canvas.line(r.x, y, r.x + r.width, y, 0xa18b6377, .8);
    }
}

Rect cell(Rect r, unsigned index, unsigned columns, unsigned rows) {
    return {r.x + (index % columns) * r.width / columns, r.y + (index / columns) * r.height / rows, r.width / columns,
            r.height / rows};
}
}  // namespace

namespace redline_weather {
Renderer::Renderer(std::filesystem::path assets) : skin_(std::make_unique<Skin>(assets)) {}

Renderer::~Renderer() = default;

void Renderer::bind(const CdCreateContext& context) {
    skin_->bind(context);
}

void Renderer::pause(bool value) {
    skin_->pause(value);
}

bool Renderer::savePng(const std::filesystem::path& path, const CdFrame& frame) {
    return skin_->save(path, frame);
}

bool Renderer::render(Spec, const cdsdk::Settings& settings, const Snapshot& data, bool loading, const CdFrame& frame,
                      CdRect safe) {
    return skin_->render(frame, safe, [&](auto& canvas) {
        const bool imperial = settings.at("units").text == "imperial";
        const bool english = settings.at("language").text == "en";
        field(*skin_, canvas, "city", wide(data.city.empty() ? "天气" : data.city));
        field(*skin_, canvas, "temperature",
              data.valid ? std::format(L"{}°{}", int(std::round(displayTemperature(data.temperature, imperial))),
                                       imperial ? L"F" : L"C")
                         : L"--°");
        field(*skin_, canvas, "humidity", data.valid ? std::format(L"{}%", int(data.humidity)) : L"--%");
        field(*skin_, canvas, "wind",
              data.valid
                  ? std::format(L"{:.1f}{}", imperial ? data.wind * 2.236936292 : data.wind, imperial ? L"mph" : L"m/s")
                  : (imperial ? L"--mph" : L"--m/s"),
              .80);
        field(*skin_, canvas, "condition",
              data.valid ? condition(data.code, english)
              : loading  ? (english ? L"Updating" : L"更新中")
                         : (english ? L"No data" : L"暂无数据"),
              .65);
        field(*skin_, canvas, "humidityLabel", english ? L"Humidity" : L"湿度", .85);
        field(*skin_, canvas, "windLabel", english ? L"Wind" : L"风速", .85);
        if (data.valid && !data.days.empty()) {
            field(*skin_, canvas, "range",
                  std::format(L"{:.0f}° / {:.0f}°", displayTemperature(data.days[0].low, imperial),
                              displayTemperature(data.days[0].high, imperial)),
                  .65);
        }
        skin_->icon(canvas, skin_->region("humidityIcon"), "droplet", 0x568e88ff);
        skin_->icon(canvas, skin_->region("windIcon"), "wind", 0x568e88ff);
        auto icon = skin_->region("icon");
        if (skin_->folder() != "weather-4x2") {
            skin_->clear(canvas, icon);
        }
        skin_->weather(canvas, icon, data.valid ? iconName(data.code, data.day) : "not-available", "current");
        skin_->weather(canvas, skin_->region("conditionIcon"),
                       data.valid ? iconName(data.code, data.day) : "not-available", "current-status");
        for (unsigned i = 0; i < 3; ++i) {
            const auto r = skin_->region("forecast" + std::to_string(i));
            if (r.width <= 0) {
                continue;
            }
            if (skin_->folder() != "weather-4x2") {
                skin_->clear(canvas, r);
            }
            const std::array<std::wstring, 3> days{english ? L"Today" : L"今天", english ? L"Tomorrow" : L"明天",
                                                   english ? L"Day 3" : L"后天"};
            if (r.width > r.height * 1.7) {
                skin_->weather(canvas, {r.x, r.y + r.height * .15, r.height * .65, r.height * .65},
                               data.valid && i < data.days.size() ? iconName(data.days[i].code, true) : "not-available",
                               "forecast" + std::to_string(i));
                const Rect title{r.x + r.height * .75, r.y, r.width - r.height * .75, r.height * .40};
                skin_->label(canvas, days[i], title, r.height * .29, redline::kIvory, false);
                skin_->label(
                    canvas,
                    data.valid && i < data.days.size()
                        ? std::format(L"{} / {}°", int(std::round(displayTemperature(data.days[i].low, imperial))),
                                      int(std::round(displayTemperature(data.days[i].high, imperial))))
                        : L"-- / --°",
                    {title.x, r.y + r.height * .46, title.width, r.height * .43}, r.height * .30, redline::kIvory,
                    false);
                continue;
            }
            skin_->label(canvas, days[i], {r.x, r.y, r.width, r.height * .23}, r.height * .18, redline::kIvory, false);
            skin_->weather(canvas, {r.x + r.width * .19, r.y + r.height * .27, r.width * .62, r.height * .37},
                           data.valid && i < data.days.size() ? iconName(data.days[i].code, true) : "not-available",
                           "forecast" + std::to_string(i));
            skin_->label(canvas,
                         data.valid && i < data.days.size()
                             ? std::format(L"{}/{}°", int(std::round(displayTemperature(data.days[i].low, imperial))),
                                           int(std::round(displayTemperature(data.days[i].high, imperial))))
                             : L"--/--°",
                         {r.x, r.y + r.height * .70, r.width, r.height * .25}, r.height * .19, redline::kIvory, false);
        }
    });
}

bool Renderer::renderReference(Spec spec, const cdsdk::Settings& settings, const Snapshot& data, bool loading,
                               const CdFrame& frame, CdRect safe) {
    return render(spec, settings, data, loading, frame, safe);
}
}  // namespace redline_weather

namespace redline_calendar {
Renderer::Renderer(const CdCreateContext& context) : skin_(std::make_unique<Skin>()) {
    skin_->bind(context);
}

Renderer::~Renderer() = default;

void Renderer::onChange(std::function<void()> callback) {
    skin_->onChange(std::move(callback));
}

void Renderer::pause(bool value) {
    skin_->pause(value);
}

bool Renderer::ready() const {
    return skin_->ready();
}

uint64_t Renderer::cacheBuilds() const {
    return skin_->cacheBuilds();
}

bool Renderer::save(const std::filesystem::path& path, const CdFrame& frame) {
    return skin_->save(path, frame);
}

unsigned Renderer::hit(CdRect safe, cybercanvas::Point point) {
    for (unsigned i = 0; i < 2; ++i) {
        const auto r = skin_->displayRegion(i ? "next" : "previous", safe);
        if (point.x >= r.x && point.y >= r.y && point.x < r.x + r.width && point.y < r.y + r.height) {
            return i + 1;
        }
    }
    return 0;
}

bool Renderer::render(const Month& value, const cdsdk::Settings& settings, const Visual& visual, const CdFrame& frame,
                      CdRect safe) {
    return skin_->render(frame, safe, [&](auto& canvas) {
        canvas.withOpacity(.6 + .4 * std::clamp(visual.progress, 0., 1.), [&] {
            skin_->clear(canvas, skin_->region("body"));
            field(*skin_, canvas, "title", std::format(L"{} / {:02}", value.date.year, value.date.month));
            skin_->button(canvas, skin_->region("previous"), "chevron-left", visual.pressed == 1);
            skin_->button(canvas, skin_->region("next"), "chevron-right", visual.pressed == 2);
            const auto r = skin_->region("grid");
            skin_->clear(canvas, r);
            gridLines(canvas, r, 7, value.weeks + 1);
            constexpr std::array<std::wstring_view, 7> days{L"日", L"一", L"二", L"三", L"四", L"五", L"六"};
            for (unsigned i = 0; i < 7; ++i) {
                const auto d = cell(r, i, 7, value.weeks + 1);
                constexpr std::array<std::wstring_view, 7> englishDays{L"Sun", L"Mon", L"Tue", L"Wed",
                                                                       L"Thu", L"Fri", L"Sat"};
                skin_->label(
                    canvas,
                    std::wstring(
                        (settings.at("language").text == "en" ? englishDays : days)[(i + (value.monday ? 1 : 0)) % 7]),
                    d, d.height * .55, redline::kIvory, false);
            }
            for (unsigned i = 0; i < value.weeks * 7; ++i) {
                const auto& day = value.cells[i];
                if (!day.current && !settings.at("adjacent_days").integer) {
                    continue;
                }
                const auto d = cell(r, i + 7, 7, value.weeks + 1);
                if (day.today) {
                    const Rect selected{d.x + 2, d.y + 2, d.width - 4, d.height - 4};
                    canvas.box(selected.x, selected.y, selected.width, selected.height, 1, 0xc9ddb650);
                    canvas.strokeBox(selected, 1, redline::kRuby, {.width = r.width * .004});
                }
                skin_->label(canvas, std::to_wstring(day.date.day), d, d.height * .65,
                             day.weekend   ? redline::kRuby
                             : day.current ? redline::kIvory
                                           : 0x807079ff,
                             false);
            }
        });
    });
}
}  // namespace redline_calendar

namespace redline_lunar {
Renderer::Renderer() : skin_(std::make_unique<Skin>()) {}

Renderer::~Renderer() = default;

void Renderer::bind(const CdCreateContext& context, std::function<void(uint32_t)> action) {
    skin_->bind(context, std::move(action));
}

void Renderer::pause(bool value) {
    skin_->pause(value);
}

bool Renderer::pendingImages() const {
    return skin_->pending();
}

void Renderer::input(const CdInput& value) {
    skin_->input(value);
}

void Renderer::cancelInput() {
    skin_->cancelInput();
}

bool Renderer::save(const std::wstring& path, const CdFrame& frame) {
    return skin_->save(path, frame);
}

void Renderer::syncInput(Spec spec, const cdsdk::Settings& settings, View view, CdRect safe) {
    std::vector<cdsdk::Controls::Item> items;
    for (const auto& pair :
         std::array<std::pair<std::string_view, uint32_t>, 3>{{{"previous", 1}, {"next", 2}, {"today", 3}}}) {
        const auto r = skin_->region(pair.first);
        if (r.width > 0) {
            items.push_back({pair.second, r, std::string(pair.first)});
        }
    }
    if (spec.rows < 4) {
        items.push_back({3, {0, 0, skin_->size().width, skin_->size().height}, "Today"});
    } else {
        const auto month = monthGrid(view.selected, settings.at("week_start").text == "monday");
        const auto r = skin_->region("grid");
        for (unsigned i = 0; i < unsigned(month.rows * 7); ++i) {
            if (month.dates[i].month == view.selected.month || settings.at("adjacent_days").integer) {
                items.push_back({100 + i, cell(r, i, 7, month.rows), "Date " + std::to_string(month.dates[i].day)});
            }
        }
    }
    skin_->controls(safe, items);
}

bool Renderer::render(Spec spec, const cdsdk::Settings& settings, View view, const CdFrame& frame, CdRect safe) {
    syncInput(spec, settings, view, safe);
    return skin_->render(frame, safe, [&](auto& canvas) {
        canvas.withOpacity(.6 + .4 * std::clamp(view.progress, 0., 1.), [&] {
            const auto lunar = convert(view.selected);
            if (spec.rows >= 4) {
                skin_->clear(canvas, skin_->region("body"));
                field(*skin_, canvas, "title", std::format(L"{} / {:02}", view.selected.year, view.selected.month));
                skin_->button(canvas, skin_->region("previous"), "chevron-left");
                skin_->button(canvas, skin_->region("next"), "chevron-right");
                skin_->label(canvas, settings.at("language").text == "en" ? L"Today" : L"今天", skin_->region("today"),
                             skin_->region("today").height * .56, redline::kIvory, false);
                const auto month = monthGrid(view.selected, settings.at("week_start").text == "monday");
                const auto r = skin_->region("grid");
                skin_->clear(canvas, r);
                gridLines(canvas, r, 7, month.rows);
                const auto headings = skin_->region("weekdays");
                constexpr std::array<std::wstring_view, 7> names{L"日", L"一", L"二", L"三", L"四", L"五", L"六"};
                constexpr std::array<std::wstring_view, 7> englishNames{L"Sun", L"Mon", L"Tue", L"Wed",
                                                                        L"Thu", L"Fri", L"Sat"};
                for (unsigned i = 0; i < 7; ++i) {
                    skin_->label(
                        canvas,
                        std::wstring((settings.at("language").text == "en"
                                          ? englishNames
                                          : names)[(i + (settings.at("week_start").text == "monday" ? 1 : 0)) % 7]),
                        cell(headings, i, 7, 1), headings.height * .65, redline::kIvory, false);
                }
                for (unsigned i = 0; i < unsigned(month.rows * 7); ++i) {
                    const auto date = month.dates[i];
                    if (!valid(date) || (date.month != view.selected.month && !settings.at("adjacent_days").integer)) {
                        continue;
                    }
                    auto d = cell(r, i, 7, month.rows);
                    const auto converted = convert(date);
                    if (date == view.selected) {
                        const Rect selected{d.x + 1, d.y + 1, d.width - 2, d.height - 2};
                        canvas.box(selected.x, selected.y, selected.width, selected.height, 1, 0xc9ddb650);
                        canvas.strokeBox(selected, 1, redline::kRuby, {.width = r.width * .003});
                    }
                    skin_->label(canvas, std::to_wstring(date.day), {d.x, d.y, d.width, d.height * .60}, d.height * .46,
                                 redline::kIvory, false);
                    skin_->label(canvas, converted.dayName, {d.x, d.y + d.height * .63, d.width, d.height * .31},
                                 d.height * .23, redline::kSilver, false);
                }
            } else {
                const auto lunarText =
                    settings.at("language").text == "en"
                        ? std::format(L"Lunar {}{}/{}", lunar.leap ? L"L" : L"", lunar.month, lunar.day)
                        : lunar.monthName + lunar.dayName;
                field(*skin_, canvas, "title",
                      spec.rows == 1
                          ? std::format(L"{}.{:02}.{:02}", view.selected.year, view.selected.month, view.selected.day)
                          : std::format(L"{}.{:02}", view.selected.year, view.selected.month));
                field(*skin_, canvas, "day", std::to_wstring(view.selected.day));
                constexpr std::array<std::wstring_view, 7> names{L"星期日", L"星期一", L"星期二", L"星期三",
                                                                 L"星期四", L"星期五", L"星期六"};
                constexpr std::array<std::wstring_view, 7> englishNames{
                    L"Sunday", L"Monday", L"Tuesday", L"Wednesday", L"Thursday", L"Friday", L"Saturday"};
                field(*skin_, canvas, "weekday",
                      std::wstring(
                          (settings.at("language").text == "en" ? englishNames : names)[weekday(view.selected)]));
                if (spec.rows == 2) {
                    const auto r = skin_->region("lunar");
                    skin_->clear(canvas, r);
                    skin_->label(canvas, lunarText, r, r.height * .66, redline::kIvory, false);
                } else {
                    field(*skin_, canvas, "lunar", lunarText, .66);
                }
            }
            field(*skin_, canvas, "festival", lunar.festival.empty() ? lunar.term : lunar.festival, .65);
        });
    });
}
}  // namespace redline_lunar

namespace redline_hourglass {
Renderer::Renderer(const CdCreateContext& context) : skin_(std::make_unique<Skin>()) {
    skin_->bind(context);
}

Renderer::~Renderer() = default;

void Renderer::onChange(std::function<void()> callback) {
    skin_->onChange(std::move(callback));
}

void Renderer::pause(bool value) {
    skin_->pause(value);
}

bool Renderer::ready() const {
    return skin_->ready();
}

uint64_t Renderer::cacheBuilds() const {
    return skin_->cacheBuilds();
}

bool Renderer::save(const std::filesystem::path& path, const CdFrame& frame) {
    return skin_->save(path, frame);
}

bool Renderer::render(bool timer, const Model& model, const PhysicalSand* sand, const cdsdk::Settings& settings,
                      const CdFrame& frame, CdRect safe) {
    return skin_->render(frame, safe, [&](auto& canvas) {
        const auto r = skin_->region("glass");
        canvas.saved([&] {
            canvas.translate(r.x + r.width / 2, r.y + r.height / 2);
            const double projection = std::cos(model.angle() * std::numbers::pi / 180);
            canvas.scale(1, std::abs(projection) < .005 ? .005 : projection);
            const double progress = std::clamp(model.progress(), 0., 1.);
            const auto tint = uint32_t(settings.at("sand_color").integer);
            const auto shade = [&](double factor) {
                const auto c = [&](unsigned shift) {
                    return uint32_t(std::clamp(double((tint >> shift) & 255) * factor, 0., 255.));
                };
                return (c(24) << 24) | (c(16) << 16) | (c(8) << 8) | 255;
            };
            const auto texture = [&](auto& shape, Rect bounds) {
                canvas.masked(shape, [&] {
                    canvas.gradientBox(bounds, 0, shade(1.05), shade(.78));
                    // Sample the approved photographic grains at their native scale.
                    // The cached source atlas is decoded once; only this small pile moves.
                    canvas.withOpacity(tint == 0xe8c67dff ? .82 : .28, [&] {
                        for (double y = std::floor(bounds.y / 16) * 16; y < bounds.y + bounds.height; y += 16) {
                            for (double x = std::floor(bounds.x / 16) * 16; x < bounds.x + bounds.width; x += 16) {
                                skin_->part(canvas, "sandTexture", {x, y, 16, 16});
                            }
                        }
                    });
                });
            };
            // Continuous piles express the metered model volume; actual Box2D grains
            // remain visible at the neck and surface, rather than sparse oversized dots.
            if (progress < 1) {
                const double level = -r.height * .31 * std::sqrt(1 - progress);
                const double half = r.width * .43 * std::pow(1 - progress, .36);
                auto upper = canvas.path();
                upper.moveTo({-half, level}).lineTo({half, level});
                upper.cubicTo({half * .90, level * .45}, {r.width * .04, -r.height * .055}, {0, 0});
                upper.cubicTo({-r.width * .04, -r.height * .055}, {-half * .90, level * .45}, {-half, level}).close();
                texture(upper, {-half, level, half * 2, -level});
                canvas.line(-half, level, half, level, 0xffe7b8dd, r.height * .006);
            }
            if (progress > 0) {
                const double peak = r.height * (.43 - .40 * std::sqrt(progress));
                auto lower = canvas.path();
                lower.moveTo({-r.width * .42, r.height * .43});
                lower.cubicTo({-r.width * .25, r.height * .41}, {-r.width * .09, peak}, {0, peak});
                lower.cubicTo({r.width * .09, peak}, {r.width * .25, r.height * .41}, {r.width * .42, r.height * .43});
                lower.lineTo({-r.width * .42, r.height * .43}).close();
                texture(lower, {-r.width * .42, peak, r.width * .84, r.height * .43 - peak});
            }
            {
                auto path = canvas.path();
                path.moveTo({-r.width * .43, -r.height * .47});
                path.cubicTo({-r.width * .50, -r.height * .15}, {-r.width * .04, -r.height * .08},
                             {-r.width * .035, 0});
                path.cubicTo({-r.width * .04, r.height * .08}, {-r.width * .50, r.height * .15},
                             {-r.width * .43, r.height * .47});
                canvas.stroke(path, redline::kSilver, {.width = r.width * .025});
                auto other = canvas.path();
                other.moveTo({r.width * .43, -r.height * .47});
                other.cubicTo({r.width * .50, -r.height * .15}, {r.width * .04, -r.height * .08}, {r.width * .035, 0});
                other.cubicTo({r.width * .04, r.height * .08}, {r.width * .50, r.height * .15},
                              {r.width * .43, r.height * .47});
                canvas.stroke(other, redline::kSilver, {.width = r.width * .025});
                canvas.line(-r.width * .43, -r.height * .47, r.width * .43, -r.height * .47, redline::kSilver,
                            r.width * .025);
                canvas.line(-r.width * .43, r.height * .47, r.width * .43, r.height * .47, redline::kSilver,
                            r.width * .025);
            }
            const auto dot = [&](double x, double y) {
                const auto tint = uint32_t(settings.at("sand_color").integer);
                const double glow = settings.at("glow").integer / 100.;
                if (glow > 0) {
                    canvas.oval(x, y, std::max(.8, r.width * .011), std::max(.8, r.width * .011),
                                (tint & 0xffffff00) | uint32_t(glow * 90));
                }
                canvas.oval(x, y, std::max(.35, r.width * .004), std::max(.35, r.width * .004),
                            uint32_t(settings.at("sand_color").integer));
            };
            if (sand) {
                for (const auto& grain : sand->sample()) {
                    const auto p =
                        mechanicalhourglass::grainPosition(grain, {-r.width / 2, -r.height / 2, r.width, r.height});
                    dot(p.x, p.y);
                }
            } else {
                const auto state = model.sample();
                for (const auto* grains : {&state.upper, &state.lower, &state.falling}) {
                    for (const auto& grain : *grains) {
                        dot((grain.x / 18. - .5) * r.width * .86, (grain.y / 28. - .5) * r.height * .90);
                    }
                }
            }
        });
        if (timer) {
            const auto text = remainingText(model.remaining());
            const auto title = skin_->region("title");
            skin_->label(canvas, std::wstring(text.begin(), text.end()), title, title.height,
                         uint32_t(settings.at("text_color").integer));
            skin_->progress(canvas, skin_->region("progress"), 1 - model.progress());
        }
    });
}
}  // namespace redline_hourglass
