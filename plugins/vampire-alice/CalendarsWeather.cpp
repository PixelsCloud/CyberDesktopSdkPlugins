#include <cmath>
#include <format>
#include <numbers>

#include "plugins/vampire-alice/Skin.h"
#include "plugins/vampire-alice/controllers/calendar/Calendar.h"
#include "plugins/vampire-alice/controllers/hourglass/MechanicalHourglass.h"
#include "plugins/vampire-alice/controllers/lunar/Lunar.h"
#include "plugins/vampire-alice/controllers/weather/Weather.h"

namespace {
using alice::Canvas;
using alice::Rect;
using alice::Skin;

void field(Skin& skin, Canvas& canvas, std::string_view name, const std::wstring& text, double factor = .77) {
    const auto r = skin.region(name);
    if (name == "festival" && skin.textured() && skin.rows() < 4 && !text.empty()) {
        auto badge = canvas.path();
        badge.moveTo({r.x, r.y + r.height / 2})
            .lineTo({r.x + r.height * .24, r.y})
            .lineTo({r.x + r.width - r.height * .24, r.y})
            .lineTo({r.x + r.width, r.y + r.height / 2})
            .lineTo({r.x + r.width - r.height * .24, r.y + r.height})
            .lineTo({r.x + r.height * .24, r.y + r.height})
            .close();
        canvas.fill(badge, 0x68101dcc);
        canvas.stroke(badge, 0xd4b481cc, {.width = std::max(.7, r.height * .025)});
    }
    skin.label(canvas, text, r, r.height * factor);
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

namespace alice_weather {
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
        field(*skin_, canvas, "city", wide(data.city.empty() ? "天气" : data.city));
        field(*skin_, canvas, "temperature",
              data.valid ? std::format(L"{}°{}", int(std::round(displayTemperature(data.temperature, imperial))),
                                       imperial ? L"F" : L"C")
                         : L"--°",
              skin_->rows() == 2      ? .95
              : skin_->columns() == 2 ? .80
                                      : .70);
        field(*skin_, canvas, "humidity", data.valid ? std::format(L"{}%", int(data.humidity)) : L"--%");
        field(*skin_, canvas, "wind", data.valid ? std::format(L"{:.1f}m/s", data.wind) : L"--m/s", .60);
        field(*skin_, canvas, "humidityLabel", L"湿度", .68);
        field(*skin_, canvas, "windLabel", L"风速", .68);
        auto icon = skin_->region("icon");
        skin_->clear(canvas, icon);
        skin_->weather(canvas, icon, data.valid ? iconName(data.code, data.day) : "not-available", "current");
        for (unsigned i = 0; i < 3; ++i) {
            const auto r = skin_->region("forecast" + std::to_string(i));
            if (r.width <= 0) {
                continue;
            }
            skin_->clear(canvas, r);
            const std::array<std::wstring, 3> days{L"今天", L"明天", L"后天"};
            skin_->label(canvas, days[i], {r.x, r.y, r.width, r.height * .23}, r.height * .18, alice::kIvory, false);
            skin_->weather(canvas, {r.x + r.width * .19, r.y + r.height * .27, r.width * .62, r.height * .37},
                           data.valid && i < data.days.size() ? iconName(data.days[i].code, true) : "not-available",
                           "forecast" + std::to_string(i));
            skin_->label(canvas,
                         data.valid && i < data.days.size() ? std::format(L"{}/{}°", int(std::round(data.days[i].low)),
                                                                          int(std::round(data.days[i].high)))
                                                            : L"--/--°",
                         {r.x, r.y + r.height * .70, r.width, r.height * .25}, r.height * .19, alice::kIvory, false);
        }
    });
}

bool Renderer::renderReference(Spec spec, const cdsdk::Settings& settings, const Snapshot& data, bool loading,
                               const CdFrame& frame, CdRect safe) {
    return render(spec, settings, data, loading, frame, safe);
}
}  // namespace alice_weather

namespace alice_calendar {
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
        field(*skin_, canvas, "title", std::format(L"{}.{:02}", value.date.year, value.date.month), .9);
        skin_->button(canvas, skin_->region("previous"), "chevron-left", visual.pressed == 1);
        skin_->button(canvas, skin_->region("next"), "chevron-right", visual.pressed == 2);
        const auto r = skin_->region("grid");
        skin_->clear(canvas, r);
        gridLines(canvas, r, 7, value.weeks + 1);
        constexpr std::array<std::wstring_view, 7> days{L"日", L"一", L"二", L"三", L"四", L"五", L"六"};
        for (unsigned i = 0; i < 7; ++i) {
            const auto d = cell(r, i, 7, value.weeks + 1);
            skin_->label(canvas, std::wstring(days[(i + (value.monday ? 1 : 0)) % 7]), d, d.height * .55, alice::kIvory,
                         false);
        }
        for (unsigned i = 0; i < value.weeks * 7; ++i) {
            const auto& day = value.cells[i];
            if (!day.current && !settings.at("adjacent_days").integer) {
                continue;
            }
            const auto d = cell(r, i + 7, 7, value.weeks + 1);
            if (day.today) {
                canvas.box(d.x + 1, d.y + 1, d.width - 2, d.height - 2, 1, 0x7d122fff);
            }
            skin_->label(canvas, std::to_wstring(day.date.day), d, d.height * .65,
                         day.weekend   ? alice::kRuby
                         : day.current ? alice::kIvory
                                       : 0x807079ff,
                         false);
        }
    });
}
}  // namespace alice_calendar

namespace alice_lunar {
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
        const auto lunar = convert(view.selected);
        if (spec.rows >= 4) {
            field(*skin_, canvas, "title", std::format(L"{}年 {}月", view.selected.year, view.selected.month));
            skin_->button(canvas, skin_->region("previous"), "chevron-left");
            skin_->button(canvas, skin_->region("next"), "chevron-right");
            skin_->label(canvas, L"今天", skin_->region("today"), skin_->region("today").height * .56, alice::kIvory,
                         false);
            const auto month = monthGrid(view.selected, settings.at("week_start").text == "monday");
            const auto r = skin_->region("grid");
            skin_->clear(canvas, skin_->region("body"));
            skin_->clear(canvas, r);
            gridLines(canvas, r, 7, month.rows);
            const auto headings = skin_->region("weekdays");
            constexpr std::array<std::wstring_view, 7> names{L"日", L"一", L"二", L"三", L"四", L"五", L"六"};
            for (unsigned i = 0; i < 7; ++i) {
                skin_->label(canvas,
                             std::wstring(names[(i + (settings.at("week_start").text == "monday" ? 1 : 0)) % 7]),
                             cell(headings, i, 7, 1), headings.height * .65, alice::kIvory, false);
            }
            for (unsigned i = 0; i < unsigned(month.rows * 7); ++i) {
                const auto date = month.dates[i];
                if (!valid(date) || (date.month != view.selected.month && !settings.at("adjacent_days").integer)) {
                    continue;
                }
                auto d = cell(r, i, 7, month.rows);
                const auto converted = convert(date);
                if (date == view.selected) {
                    canvas.box(d.x + 1, d.y + 1, d.width - 2, d.height - 2, 1, 0x7d122fff);
                }
                skin_->label(canvas, std::to_wstring(date.day), {d.x, d.y, d.width, d.height * .60}, d.height * .46,
                             alice::kIvory, false);
                skin_->label(canvas, converted.dayName, {d.x, d.y + d.height * .63, d.width, d.height * .31},
                             d.height * .23, alice::kSilver, false);
            }
        } else {
            field(*skin_, canvas, "title",
                  spec.rows == 1
                      ? std::format(L"{}.{:02}.{:02}", view.selected.year, view.selected.month, view.selected.day)
                      : std::format(L"{}.{:02}", view.selected.year, view.selected.month),
                  1.05);
            field(*skin_, canvas, "day", std::to_wstring(view.selected.day), 1.2);
            constexpr std::array<std::wstring_view, 7> names{L"星期日", L"星期一", L"星期二", L"星期三",
                                                             L"星期四", L"星期五", L"星期六"};
            field(*skin_, canvas, "weekday", std::wstring(names[weekday(view.selected)]));
            if (spec.rows == 2) {
                const auto r = skin_->region("lunar");
                skin_->clear(canvas, r, 0xe5d5baff);
                skin_->label(canvas, lunar.monthName + lunar.dayName, r, r.height * .66, 0x301d1dff, false);
            } else {
                field(*skin_, canvas, "lunar", lunar.monthName + lunar.dayName, .66);
            }
        }
        field(*skin_, canvas, "festival", lunar.festival.empty() ? lunar.term : lunar.festival, .65);
    });
}
}  // namespace alice_lunar

namespace alice_hourglass {
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
        skin_->clear(canvas, r, 0x160a12ff);
        canvas.saved([&] {
            canvas.translate(r.x + r.width / 2, r.y + r.height / 2);
            const double projection = std::cos(model.angle() * std::numbers::pi / 180);
            canvas.scale(1, std::abs(projection) < .005 ? .005 : projection);
            if (!skin_->textured()) {
                auto path = canvas.path();
                path.moveTo({-r.width * .43, -r.height * .47});
                path.cubicTo({-r.width * .50, -r.height * .15}, {-r.width * .04, -r.height * .08},
                             {-r.width * .035, 0});
                path.cubicTo({-r.width * .04, r.height * .08}, {-r.width * .50, r.height * .15},
                             {-r.width * .43, r.height * .47});
                canvas.stroke(path, alice::kSilver, {.width = r.width * .025});
                auto other = canvas.path();
                other.moveTo({r.width * .43, -r.height * .47});
                other.cubicTo({r.width * .50, -r.height * .15}, {r.width * .04, -r.height * .08}, {r.width * .035, 0});
                other.cubicTo({r.width * .04, r.height * .08}, {r.width * .50, r.height * .15},
                              {r.width * .43, r.height * .47});
                canvas.stroke(other, alice::kSilver, {.width = r.width * .025});
            }
            const auto dot = [&](double x, double y) {
                const auto tint = uint32_t(settings.at("sand_color").integer);
                const double glow = settings.at("glow").integer / 100.;
                if (glow > 0) {
                    canvas.oval(x, y, std::max(.8, r.width * .011), std::max(.8, r.width * .011),
                                (tint & 0xffffff00) | uint32_t(glow * 90));
                }
                canvas.oval(x, y, std::max(.6, r.width * .019), std::max(.6, r.width * .019),
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
            skin_->label(canvas, std::wstring(text.begin(), text.end()), title, title.height * .77,
                         uint32_t(settings.at("text_color").integer));
            skin_->progress(canvas, skin_->region("progress"), 1 - model.progress());
        }
    });
}
}  // namespace alice_hourglass
