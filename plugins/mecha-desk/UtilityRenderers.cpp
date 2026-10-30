#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

#include "plugins/mecha-desk/Skin.h"
#include "plugins/mecha-desk/controllers/air/AirQuality.h"
#include "plugins/mecha-desk/controllers/calendar/Calendar.h"
#include "plugins/mecha-desk/controllers/lunar/Lunar.h"
#include "plugins/mecha-desk/controllers/matrix/Pixels.h"
#include "plugins/mecha-desk/controllers/monitor/Monitor.h"
#include "plugins/mecha-desk/controllers/productivity/Productivity.h"
#include "plugins/mecha-desk/controllers/widget/Widget.h"
#include "plugins/widget-suite/AlbumArtwork.h"
#include "plugins/widget-suite/InputCounter.h"

namespace {
using aerisskin::Canvas;
using aerisskin::Rect;
using aerisskin::Skin;

std::wstring number(double value, bool valid = true) {
    return valid && std::isfinite(value) ? std::to_wstring(int(std::round(value))) : L"--";
}

std::wstring clockTime(int64_t seconds) {
    seconds = std::max(int64_t(0), seconds);
    return std::format(L"{:02}:{:02}", seconds / 60, seconds % 60);
}

void label(Skin& skin, Canvas& canvas, std::string_view key, const std::wstring& value, double factor = .77) {
    const auto r = skin.region(key);
    skin.label(canvas, value, r, r.height * factor);
}

void updateControls(Skin& skin, CdRect safe, const std::vector<std::pair<std::string, uint32_t>>& keys,
                    bool enabled = true) {
    std::vector<cdsdk::Controls::Item> items;
    for (const auto& [key, id] : keys) {
        const auto r = skin.region(key);
        if (r.width > 0 && r.height > 0) {
            items.push_back({id, r, key, enabled});
        }
    }
    skin.controls(safe, items);
}

void metalRod(Canvas& canvas, Rect r) {
    const D2D1_GRADIENT_STOP stops[]{{0, cybercanvas::color(0x45698cff)},
                                     {.2f, cybercanvas::color(0xadd6eaff)},
                                     {.39f, cybercanvas::color(0xeff9faff)},
                                     {.6f, cybercanvas::color(0x82aacaff)},
                                     {1, cybercanvas::color(0x35567dff)}};
    const auto metal = canvas.linearGradient({r.x, r.y}, {r.x + r.width, r.y}, stops);
    canvas.target->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(float(r.x), float(r.y), float(r.x + r.width), float(r.y + r.height)),
                          float(r.width * .12), float(r.width * .12)),
        metal.Get());
    canvas.oval(r.x + r.width / 2, r.y + r.width * .06, r.width * .47, r.width * .09, 0xf4f7ebff);
    canvas.line(r.x + r.width * .1, r.y + r.height - 1, r.x + r.width * .9, r.y + r.height - 1, 0x627a92ff, 1.5);
}
}  // namespace

namespace aeris_matrix {
Renderer::Renderer(const CdCreateContext& context) : skin_(std::make_unique<Skin>()) {
    skin_->bind(context);
    environment_.bind(context);
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

bool Renderer::render(Kind kind, const cdsdk::Settings& settings, std::string_view text, bool colon,
                      const widgetplugin::Spectrum& spectrum, const CdFrame& frame, CdRect safe) {
    skin_->appearance(settings);
    return skin_->render(frame, safe, [&](auto& canvas) {
        const auto r = skin_->region("plot");
        skin_->clear(canvas, r);
        const unsigned rows = isClock(kind) ? 7 : 5;
        const unsigned cols = isClock(kind)           ? (text.size() > 5 ? 41 : 27)
                              : skin_->columns() == 4 ? 52
                              : skin_->columns() == 6 ? 82
                                                      : 112;
        const double pitch = std::min(r.width / cols, r.height / rows), size = pitch * (isClock(kind) ? .74 : .68);
        const double pitchX = isClock(kind) ? pitch : r.width / cols, pitchY = isClock(kind) ? pitch : r.height / rows;
        const double left = r.x + (r.width - cols * pitchX) / 2, top = r.y + (r.height - rows * pitchY) / 2;
        const double brightness = settings.at("brightness").number / 100;
        sprites_.begin(canvas, size, settings.at("glow").number);
        for (unsigned x = 0; x < cols; ++x) {
            const auto height =
                std::clamp(spectrum.sample(double(x) * (widgetplugin::Spectrum::count - 1) / (cols - 1)), 0.f, 1.f);
            const auto peak = std::clamp(
                spectrum.sample(double(x) * (widgetplugin::Spectrum::count - 1) / (cols - 1), true), 0.f, 1.f);
            for (unsigned y = 0; y < rows; ++y) {
                const bool active =
                    isClock(kind) ? clockCell(text, x, y + 1, colon) : (rows - y <= std::round(height * rows));
                const bool cap =
                    !isClock(kind) && settings.at("peaks").integer && peak > .01 && rows - y == std::round(peak * rows);
                const auto base = cellColor(kind, settings, rows - y - 1, rows, isClock(kind) ? x >= 27 : cap);
                const auto dim = [&](uint32_t rgba, double factor) {
                    const auto channel = [&](unsigned shift) {
                        return uint32_t(std::clamp(((rgba >> shift) & 255) * factor, 0., 255.));
                    };
                    return (channel(24) << 24) | (channel(16) << 16) | (channel(8) << 8) | (rgba & 255);
                };
                const auto color = active || cap ? dim(base, brightness) : 0x153b5dff;
                if (isClock(kind) && !active) {
                    continue;
                }
                const double cx = left + (x + .5) * pitchX, cy = top + (y + .5) * pitchY;
                const uint64_t material =
                    uint64_t(color) | (uint64_t(active || cap) << 32) | (uint64_t(isRound(kind)) << 33);
                sprites_.add(canvas, material, {cx, cy}, [&](auto& canvas) {
                    const double cx = 0, cy = 0;
                    if (isRound(kind)) {
                        canvas.oval(cx, cy, size / 2, size / 2, active || cap ? color : 0x92b5bd1f);
                    } else {
                        canvas.box(cx - size / 2, cy - size / 2, size, size, 0, active || cap ? color : 0x92b5bd1f);
                    }
                });
            }
        }
        sprites_.flush(canvas);
        if (skin_->terminal()) {
            const auto t = cdsdk::Environment::localTime(environment_.snapshot());
            const auto clock = skin_->region("clock");
            skin_->label(canvas, std::format(L"{:02}:{:02}", t.tm_hour, t.tm_min), clock, clock.height * .8);
        }
    });
}
}  // namespace aeris_matrix

namespace aeris_widget {
Rect Canvas::photoRegion(CdRect safe) const {
    return skin_->displayRegion("photo", safe);
}

Canvas::Canvas() : skin_(std::make_unique<Skin>()) {}

Canvas::~Canvas() = default;

void Canvas::bind(const CdCreateContext& context, std::function<void(uint32_t)> action) {
    skin_->bind(context, std::move(action));
}

void Canvas::pause(bool value) {
    skin_->pause(value);
}

void Canvas::input(const CdInput& value) {
    skin_->input(value);
}

void Canvas::cancelInput() {
    skin_->cancelInput();
}

bool Canvas::pendingImages() const {
    return skin_->pending();
}

bool Canvas::savePng(const std::wstring& path, const CdFrame& frame) {
    return skin_->save(path, frame);
}

void Canvas::syncInput(Spec spec, const Media& media, CdRect safe, const Photo& photo, AlbumPlayback album) {
    if (spec.kind == Kind::Player) {
        std::vector<cdsdk::Controls::Item> items;
        const std::array<std::string, 3> keys{"previous", "toggle", "next"};
        constexpr uint32_t flags[]{CD_MEDIA_CAN_PREVIOUS, CD_MEDIA_CAN_TOGGLE, CD_MEDIA_CAN_NEXT};
        for (unsigned i = 0; i < keys.size(); ++i) {
            items.push_back(
                {i + 1, skin_->region(keys[i]), keys[i],
                 !media.source.empty() && bool(media.controls & flags[i]) && media.commandResult != CD_ERROR_BUSY});
        }
        skin_->controls(safe, items);
    } else if (spec.kind == Kind::Album) {
        updateControls(*skin_, safe, {{"previous", 1}, {"toggle", 2}, {"next", 3}},
                       bool(photo.bitmap) && !album.pending);
    }
}

bool Canvas::render(Spec spec, const cdsdk::Settings& settings, const Spectrum& spectrum, const Photo& photo,
                    const Photo& previous, float fade, Date date, int offset, const CdFrame& frame, CdRect safe,
                    const Media& media, int pressed, const CdInputStats& inputStats,
                    const widgetplugin::CounterDrums* counterDrums, AlbumPlayback album) {
    if (safe.width <= 0 || safe.height <= 0) {
        safe = {0, 0, frame.width / frame.device_pixel_ratio, frame.height / frame.device_pixel_ratio};
    }
    syncInput(spec, media, safe, photo, album);
    skin_->appearance(settings);
    return skin_->render(frame, safe, [&](auto& canvas) {
        if (spec.kind == Kind::Player) {
            const auto cover = skin_->region("cover");
            skin_->clear(canvas, cover, aerisskin::kInk, true);
            if (media.cover.bitmap) {
                skin_->photo(canvas, cover, *media.cover.bitmap);
            } else {
                skin_->icon(
                    canvas,
                    {cover.x + cover.width * .25, cover.y + cover.height * .25, cover.width * .5, cover.height * .5},
                    "music-2", aerisskin::kSilver);
            }
            label(*skin_, canvas, "title", media.title.empty() ? L"未在播放" : media.title);
            label(*skin_, canvas, "artist", media.artist);
            const double position = mediaPosition(media, GetTickCount64());
            skin_->progress(canvas, skin_->region("progress"), media.duration > 0 ? position / media.duration : 0);
            label(*skin_, canvas, "elapsed", clockTime(int64_t(position)));
            label(*skin_, canvas, "duration", media.duration > 0 ? clockTime(int64_t(media.duration)) : L"--:--");
            for (unsigned i = 0; i < 3; ++i) {
                const std::array<std::string, 3> keys{"previous", "toggle", "next"};
                const auto icon = i == 0                             ? "skip-back"
                                  : i == 2                           ? "skip-forward"
                                  : media.status == CD_MEDIA_PLAYING ? "pause"
                                                                     : "play";
                skin_->button(canvas, skin_->region(keys[i]), icon, pressed == int(i + 1), !media.source.empty());
            }
        } else if (spec.kind == Kind::Album) {
            const auto r = skin_->region("photo");
            skin_->clipRegion(canvas, "photo", [&] {
                skin_->clear(canvas, r, aerisskin::kInk, true);
                if (previous.bitmap && fade < 1) {
                    canvas.clipped(r, [&] {
                        const auto bounds =
                            album.motionEnabled
                                ? widgetplugin::albumPhotoRect(r, album.previousMotionSeconds,
                                                               settings.at("fit").text == "cover", previous.index)
                                : r;
                        skin_->photo(canvas, bounds, *previous.bitmap, 1, settings.at("fit").text);
                    });
                }
                if (photo.bitmap) {
                    canvas.clipped(r, [&] {
                        const auto bounds = album.motionEnabled ? widgetplugin::albumPhotoRect(
                                                                      r, album.motionSeconds,
                                                                      settings.at("fit").text == "cover", photo.index)
                                                                : r;
                        skin_->photo(canvas, bounds, *photo.bitmap, previous.bitmap ? fade : 1,
                                     settings.at("fit").text);
                    });
                } else {
                    skin_->icon(canvas, {r.x + r.width * .44, r.y + r.height * .28, r.width * .12, r.width * .12},
                                "image", 0xcbdfeeff);
                    skin_->label(canvas, L"请选择相册目录", {r.x, r.y + r.height * .54, r.width, r.height * .20},
                                 r.height * .12, 0xcbdfeeff);
                }
            });
            const bool enabled = bool(photo.bitmap) && !album.pending;
            skin_->button(canvas, skin_->region("previous"), "chevron-left", false, enabled);
            skin_->button(canvas, skin_->region("next"), "chevron-right", false, enabled);
            skin_->button(canvas, skin_->region("toggle"), album.paused ? "play" : "pause", false, enabled);
            if (settings.at("counter").integer) {
                skin_->label(canvas, photo.count ? std::format(L"{} / {}", photo.index + 1, photo.count) : L"-- / --",
                             skin_->region("counter"), 7);
            }
        } else if (spec.kind == Kind::InputStats) {
            const bool valid = inputStats.status & CD_INPUT_STATS_ACTIVE;
            const auto positions =
                counterDrums ? counterDrums->sample(GetTickCount64()) : widgetplugin::CounterDrums::Positions{};
            for (unsigned channel = 0; channel < 2; ++channel) {
                const auto r = skin_->region(channel ? "mouse" : "keyboard");
                const auto icon = skin_->terminal() ? skin_->region(channel ? "mouseIcon" : "keyboardIcon")
                                                    : Rect{r.x - 32, r.y + (r.height - 17) / 2, 22, 17};
                skin_->icon(canvas, icon, channel ? "mouse" : "keyboard");
                if (settings.at("show_total").integer) {
                    skin_->label(canvas, std::to_wstring(channel ? inputStats.mouse_total : inputStats.keyboard_total),
                                 r, r.height * .92);
                    continue;
                }
                const auto digits =
                    widgetplugin::counterDigits(channel ? inputStats.mouse_today : inputStats.keyboard_today);
                for (unsigned i = 0; i < 5; ++i) {
                    const Rect plate{r.x + (i + .08) * r.width / 5, r.y, r.width * .84 / 5, r.height};
                    skin_->clear(canvas, plate);
                    canvas.clipped(plate, [&] {
                        const std::array<std::wstring, 10> references{L"0", L"1", L"2", L"3", L"4",
                                                                      L"5", L"6", L"7", L"8", L"9"};
                        const double position = counterDrums ? positions[channel * 5 + i] : digits[i];
                        const double phase = valid ? position - std::floor(position) : 0;
                        for (unsigned line = 0; line < (phase > 0 ? 2u : 1u); ++line) {
                            const auto digit = unsigned(std::floor(position) + line) % 10;
                            skin_->label(canvas, valid ? std::to_wstring(digit) : L"-",
                                         {plate.x, plate.y + (line - phase) * plate.height, plate.width, plate.height},
                                         plate.height * .95, 0xd9f0e8ff, false, references);
                        }
                    });
                }
            }
        } else {
            const auto r = skin_->region("plot");
            if (spec.kind == Kind::Radial) {
                const double cx = r.x + r.width / 2, cy = r.y + r.height / 2,
                             radius = std::min(r.width, r.height) * .23;
                canvas.oval(cx, cy, radius - 3, radius - 3, 0x8cc8eaff, 3);
                canvas.oval(cx, cy, radius - 6, radius - 6, 0xbaa07bff, 1.5);
                for (unsigned i = 0; i < 48; ++i) {
                    const double a = i * 2 * std::numbers::pi / 48, v = spectrum.sample(double(i) * 62 / 47);
                    const double end = radius + radius * (.08 + .60 * v);
                    canvas.saved([&] {
                        canvas.translate(cx, cy);
                        canvas.rotate(a * 180 / std::numbers::pi + 90);
                        const double rod = std::min(r.width, r.height) * .014;
                        metalRod(canvas, {-rod / 2, -end, rod, end - radius});
                        auto gem = canvas.path();
                        const double gemSize = std::min(r.width, r.height) * .012;
                        gem.moveTo({0, -end - gemSize})
                            .lineTo({gemSize, -end})
                            .lineTo({0, -end + gemSize})
                            .lineTo({-gemSize, -end})
                            .close();
                        canvas.fill(gem, aerisskin::kRuby);
                        canvas.stroke(gem, 0xeee0c3ff, {.width = .65});
                    });
                }
            } else {
                const unsigned count = std::max(24u, unsigned(std::floor(r.width / 4.8)));
                for (unsigned i = 0; i < count; ++i) {
                    const double v = spectrum.sample(double(i) * 62 / (count - 1)),
                                 peak = spectrum.sample(double(i) * 62 / (count - 1), true);
                    const double step = r.width / count, w = step * .58, base = r.y + r.height - 2,
                                 x = r.x + (i + .5) * step;
                    const double h = std::max(1.5, v * std::max(1., r.height - 4));
                    metalRod(canvas, {x - w / 2, base - h, w, h});
                    if (settings.at("peaks").integer) {
                        const double y = base - peak * r.height, side = w * .60;
                        auto gem = canvas.path();
                        gem.moveTo({x, y - side})
                            .lineTo({x + side, y})
                            .lineTo({x, y + side})
                            .lineTo({x - side, y})
                            .close();
                        canvas.fill(gem, aerisskin::kRuby);
                        canvas.stroke(gem, 0xeee0c3ff, {.width = std::max(.6, w * .07)});
                    }
                }
            }
        }
    });
}
}  // namespace aeris_widget

namespace aeris_monitor {
bool Renderer::poll() {
    return skin_->poll();
}

Renderer::Renderer() : skin_(std::make_unique<Skin>()) {}

Renderer::~Renderer() = default;

void Renderer::bind(const CdCreateContext& context, std::function<void()> changed) {
    skin_->bind(context);
    skin_->onChange(std::move(changed));
}

void Renderer::pause(bool value) {
    skin_->pause(value);
}

bool Renderer::savePng(const std::wstring& path, const CdFrame& frame, CdColor) {
    return skin_->save(path, frame);
}

bool Renderer::render(Spec spec, const Options& options, Values values, const CdFrame& frame, CdRect safe) {
    skin_->font(options.fontFamily);
    return skin_->render(frame, safe, [&](auto& canvas) {
        const unsigned count = spec.columns == 1 ? 1 : spec.rows == 1 ? unsigned(spec.columns) : 4;
        const std::array<double, 4> v{values.cpu, values.gpu, values.ram, values.temperature};
        const auto motion = options.interpolate ? options.needles : values;
        const std::array<double, 4> needles{motion.cpu, motion.gpu, motion.ram, motion.temperature};
        const std::array<std::wstring, 4> titles{L"CPU", L"GPU", L"内存", L"温度"};
        const uint32_t flags[]{CD_SYSTEM_CPU_VALID, CD_SYSTEM_GPU_VALID, CD_SYSTEM_MEMORY_VALID,
                               CD_SYSTEM_CPU_TEMPERATURE_VALID};
        for (unsigned i = 0; i < count; ++i) {
            const unsigned primary = options.primary == "ram"           ? 2
                                     : options.primary == "gpu"         ? 1
                                     : options.primary == "temperature" ? 3
                                                                        : 0;
            const unsigned metric = count == 1 ? primary : count == 2 ? (i == 0 ? 0 : 2) : i;
            skin_->gauge(canvas, skin_->region("gauge" + std::to_string(i)), needles[metric] / 100,
                         number(v[metric], values.valid & flags[metric]) + (metric == 3 ? L"°C" : L"%"),
                         titles[metric]);
        }
    });
}
}  // namespace aeris_monitor

namespace aeris_productivity {
Renderer::Renderer() : skin_(std::make_unique<Skin>()) {}

Renderer::~Renderer() = default;

void Renderer::bind(const CdCreateContext& context, std::function<void(uint32_t)> action) {
    skin_->bind(context, std::move(action));
}

void Renderer::pause(bool value) {
    skin_->pause(value);
}

void Renderer::input(const CdInput& event) {
    skin_->input(event);
}

void Renderer::cancelInput() {
    skin_->cancelInput();
}

void Renderer::syncInput(Spec spec, const cdsdk::Settings&, const Data&, CdRect safe) {
    if (spec.kind == Kind::Timer) {
        updateControls(*skin_, safe, {{"toggle", 1}, {"reset", 2}});
    }
}

bool Renderer::savePng(const std::filesystem::path& path, const CdFrame& frame) {
    return skin_->save(path, frame);
}

bool Renderer::render(Spec spec, const cdsdk::Settings& settings, const Data& data, const CdFrame& frame, CdRect safe) {
    syncInput(spec, settings, data, safe);
    skin_->appearance(settings);
    return skin_->render(frame, safe, [&](auto& canvas) {
        if (spec.kind == Kind::Timer) {
            const bool english = settings.at("language").text == "en";
            const auto total = settings.at(data.resting ? "break_minutes" : "focus_minutes").integer * 60;
            skin_->gauge(canvas, skin_->region("gauge"), double(data.remaining) / std::max<int64_t>(1, total), L"",
                         L"");
            auto phase = data.finished                             ? (english ? L"Done" : L"完成")
                         : data.resting                            ? (english ? L"Break" : L"休息")
                         : settings.at("mode").text == "countdown" ? (english ? L"Countdown" : L"倒计时")
                                                                   : (english ? L"Focus" : L"专注");
            auto text = std::wstring(phase);
            if (data.completed > 0) {
                text += L" · " + std::to_wstring(data.completed);
            }
            const auto phaseArea = skin_->region("phase");
            skin_->caption(canvas, text, phaseArea, phaseArea.height * .78);
            skin_->progress(canvas, skin_->region("progress"), double(data.remaining) / std::max<int64_t>(1, total));
            label(*skin_, canvas, "title", clockTime(data.remaining), 1.05);
            skin_->button(canvas, skin_->region("toggle"), data.running ? "pause" : "play", data.running);
            skin_->button(canvas, skin_->region("reset"), "rotate-ccw");
        } else if (spec.kind == Kind::Countdown) {
            const bool english = settings.at("language").text == "en";
            const auto state = data.days < 0    ? (english ? L"Elapsed" : L"已过")
                               : data.days == 0 ? (english ? L"Today" : L"今天")
                                                : (english ? L"Remaining" : L"剩余");
            const auto heading = skin_->region("title");
            if (spec.columns > 2) {
                skin_->caption(canvas,
                               skin_->terminal() ? wide(settings.at("title").text)
                                                 : std::wstring(state) + L" · " + wide(settings.at("title").text),
                               heading, heading.height * .8);
            }
            const uint64_t magnitude = data.days < 0 ? uint64_t(-(data.days + 1)) + 1 : uint64_t(data.days);
            label(*skin_, canvas, "count", std::to_wstring(magnitude), 1.1);
            label(*skin_, canvas, "unit", spec.columns <= 2 ? L"D" : english ? L"days" : L"天", .8);
            const auto target = skin_->region("target");
            const auto date = settings.at("date").text;
            skin_->caption(
                canvas,
                wide((spec.columns <= 2 || (skin_->terminal() && target.width < 50)) && date.size() == 10
                         ? date.substr(5)
                         : date) +
                    (spec.columns > 2 && !(skin_->terminal() && target.width < 50) && settings.at("annual").integer
                         ? (english ? L" · annual" : L" · 每年")
                         : L""),
                target, target.height * .8);
        } else if (spec.kind == Kind::Disk) {
            if (skin_->terminal()) {
                label(*skin_, canvas, "title", wide(settings.at("drive").text));
            }
            const double used = data.total ? 1 - double(data.free) / data.total : 0;
            skin_->gauge(canvas, skin_->region("gauge"), used,
                         data.valid ? std::format(L"{}%", int(std::round(used * 100))) : L"--%",
                         wide(settings.at("drive").text));
            skin_->progress(canvas, skin_->region("progress"), used);
            if (spec.rows > 1) {
                label(*skin_, canvas, "usage",
                      data.valid ? std::format(L"{} / {} GB", (data.total - data.free) >> 30, data.total >> 30)
                                 : L"-- / -- GB");
            }
            label(*skin_, canvas, "free",
                  data.valid
                      ? (spec.rows == 1                         ? std::format(L"{} GB", data.available >> 30)
                         : settings.at("language").text == "en" ? std::format(L"Free {} GB", data.available >> 30)
                                                                : std::format(L"可用 {} GB", data.available >> 30))
                  : settings.at("language").text == "en" ? L"Unavailable"
                                                         : L"不可用");
        } else {
            const auto rate = [](uint64_t value) {
                if (value < 1024 * 1024) {
                    return std::format(L"{} KB/s", value / 1024);
                }
                return std::format(L"{:.1f} MB/s", double(value) / (1024 * 1024));
            };
            for (unsigned channel = 0; channel < 2; ++channel) {
                const auto r = skin_->region(channel ? "up" : "down");
                if (skin_->terminal()) {
                    skin_->icon(canvas, skin_->region(channel ? "upIcon" : "downIcon"),
                                channel ? "arrow-up" : "arrow-down", 0x00e7efff);
                    const auto value = channel ? data.up : data.down;
                    const bool megabytes = value >= 1024 * 1024;
                    const auto text = !data.valid ? L"--"
                                      : megabytes ? std::format(L"{:.1f}", double(value) / (1024 * 1024))
                                                  : std::to_wstring(value / 1024);
                    skin_->metric(canvas, text, megabytes ? L"MB/s" : L"KB/s", r, r.height * .85, r.height * .40);
                } else {
                    skin_->label(canvas, data.valid ? rate(channel ? data.up : data.down) : L"-- MB/s", r,
                                 r.height * .78);
                }
            }
            for (unsigned channel = 0; channel < 2; ++channel) {
                auto r = skin_->region(channel ? "upHistory" : "downHistory");
                skin_->clear(canvas, r);
                if (r.width <= 0 || r.height <= 0) {
                    continue;
                }
                uint64_t maximum = 1;
                for (const auto& sample : data.history) {
                    maximum = std::max(maximum, channel ? sample.second : sample.first);
                }
                if (data.history.size() > 1) {
                    auto path = canvas.path();
                    for (size_t i = 0; i < data.history.size(); ++i) {
                        const auto& sample = data.history[i];
                        const aerisskin::Point point{
                            r.x + r.width * i / (data.history.size() - 1),
                            r.y + r.height * (1 - double(channel ? sample.second : sample.first) / maximum)};
                        if (i) {
                            path.lineTo(point);
                        } else {
                            path.moveTo(point);
                        }
                    }
                    canvas.stroke(path, channel ? 0x7bc5e5ff : aerisskin::kRuby,
                                  {.width = std::max(1., r.height * .08)});
                }
            }
        }
    });
}
}  // namespace aeris_productivity

namespace aeris_air {
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
    skin_->appearance(settings);
    return skin_->render(frame, safe, [&](auto& canvas) {
        const bool english = settings.at("language").text == "en";
        const bool traditional = settings.at("language").text == "zh-TW";
        const int category = std::clamp(level(data.aqi), 0, 5);
        const uint32_t status = data.valid ? levelColor(category) : 0x827577ff;
        const std::array<double, 4> indices{data.aqi, data.pollutants[0], data.pollutants[1], data.pollutants[2]};
        const std::array<std::wstring, 4> titles{L"AQI", L"PM2.5", L"PM10", L"O3"};
        for (unsigned i = 0; i < indices.size(); ++i) {
            skin_->gauge(canvas, skin_->region("gauge" + std::to_string(i)), indices[i] / (i ? 100 : 300),
                         number(indices[i], data.valid && indices[i] >= 0), titles[i]);
        }
        const auto quality = skin_->region("quality");
        constexpr std::array<const wchar_t*, 6> zh{L"优", L"良", L"轻度污染", L"中度污染", L"重度污染", L"严重污染"};
        constexpr std::array<const wchar_t*, 6> tw{L"優", L"良", L"輕度污染", L"中度污染", L"重度污染", L"嚴重污染"};
        constexpr std::array<const wchar_t*, 6> en{L"Good",      L"Moderate",       L"Sensitive",
                                                   L"Unhealthy", L"Very unhealthy", L"Hazardous"};
        constexpr std::array<const wchar_t*, 6> compact{L"Good", L"Mod.", L"Risk", L"High", L"V.High", L"Hazard"};
        const auto state = data.valid ? (english       ? (quality.width < 35 ? compact[category] : en[category])
                                         : traditional ? tw[category]
                                                       : zh[category])
                           : loading  ? (english ? L"Loading" : L"更新中")
                           : data.error == Error::CredentialsMissing || data.error == Error::Unauthorized
                               ? (english       ? L"Setup"
                                  : traditional ? L"未設定"
                                                : L"未配置")
                           : data.error == Error::LocationUnavailable ? (english       ? L"Location"
                                                                         : traditional ? L"未定位"
                                                                                       : L"未定位")
                                                                      : (english       ? L"No data"
                                                                         : traditional ? L"暫無資料"
                                                                                       : L"暂无数据");
        label(*skin_, canvas, "quality", state, .65);
        label(*skin_, canvas, "updated",
              loading       ? (english ? L"Updating" : L"更新中")
              : data.cached ? (english       ? L"Cached"
                               : traditional ? L"快取資料"
                                             : L"缓存数据")
              : data.valid  ? (english ? L"Updated" : L"已更新")
                            : (english ? L"Not updated" : L"尚未更新"),
              .65);
    });
}

bool Renderer::renderReference(Spec spec, const cdsdk::Settings& settings, const Snapshot& data, bool loading,
                               const CdFrame& frame, CdRect safe) {
    return render(spec, settings, data, loading, frame, safe);
}
}  // namespace aeris_air
