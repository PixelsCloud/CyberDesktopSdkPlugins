#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

#include "plugins/vampire-alice/Skin.h"
#include "plugins/vampire-alice/controllers/air/AirQuality.h"
#include "plugins/vampire-alice/controllers/calendar/Calendar.h"
#include "plugins/vampire-alice/controllers/hourglass/MechanicalHourglass.h"
#include "plugins/vampire-alice/controllers/lunar/Lunar.h"
#include "plugins/vampire-alice/controllers/matrix/Pixels.h"
#include "plugins/vampire-alice/controllers/monitor/Monitor.h"
#include "plugins/vampire-alice/controllers/productivity/Productivity.h"
#include "plugins/vampire-alice/controllers/widget/Widget.h"
#include "plugins/widget-suite/AlbumArtwork.h"
#include "plugins/widget-suite/InputCounter.h"

namespace {
using alice::Canvas;
using alice::Rect;
using alice::Skin;

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
    const D2D1_GRADIENT_STOP stops[]{{0, cybercanvas::color(0x463a31ff)},
                                     {.2f, cybercanvas::color(0xb6a488ff)},
                                     {.39f, cybercanvas::color(0xf8edd3ff)},
                                     {.6f, cybercanvas::color(0x98846fff)},
                                     {1, cybercanvas::color(0x40322cff)}};
    const auto metal = canvas.linearGradient({r.x, r.y}, {r.x + r.width, r.y}, stops);
    canvas.target->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(float(r.x), float(r.y), float(r.x + r.width), float(r.y + r.height)),
                          float(r.width * .12), float(r.width * .12)),
        metal.Get());
    canvas.oval(r.x + r.width / 2, r.y + r.width * .06, r.width * .47, r.width * .09, 0xead9bdff);
    canvas.line(r.x + r.width * .1, r.y + r.height - 1, r.x + r.width * .9, r.y + r.height - 1, 0x1e1614ff, 1.5);
}
}  // namespace

namespace alice_matrix {
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

bool Renderer::render(Kind kind, const cdsdk::Settings& settings, std::string_view text, bool colon,
                      const widgetplugin::Spectrum& spectrum, const CdFrame& frame, CdRect safe) {
    return skin_->render(frame, safe, [&](auto& canvas) {
        const auto r = skin_->region("plot");
        skin_->clear(canvas, r);
        const unsigned cols = isClock(kind) ? (text.size() > 5 ? 41u : 27u) : skin_->columns() * 8;
        const unsigned rows = isClock(kind) ? 7 : 5;
        const double pitch = std::min(r.width / cols, r.height / rows), size = pitch * (isClock(kind) ? .87 : .76);
        const double left = r.x + (r.width - cols * pitch) / 2, top = r.y + (r.height - rows * pitch) / 2;
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
                const auto color = active || cap ? dim(base, brightness) : 0x291b23ff;
                if (isClock(kind) && !active) {
                    continue;
                }
                const double cx = left + (x + .5) * pitch, cy = top + (y + .5) * pitch;
                const uint64_t material =
                    uint64_t(color) | (uint64_t(active || cap) << 32) | (uint64_t(isRound(kind)) << 33);
                sprites_.add(canvas, material, {cx, cy}, [&](auto& canvas) {
                    const double cx = 0, cy = 0;
                    if (isRound(kind)) {
                        if ((active || cap) && settings.at("glow").number > 0) {
                            const double glow = settings.at("glow").number / 100;
                            canvas.oval(cx, cy, size * (.5 + glow * .2), size * (.5 + glow * .2),
                                        (color & 0xffffff00) | uint32_t(glow * 45));
                        }
                        const D2D1_GRADIENT_STOP stops[]{
                            {0, cybercanvas::color(active || cap ? dim(color, 1.08) : 0x454047ff)},
                            {.58f, cybercanvas::color(color)},
                            {1, cybercanvas::color(dim(color, .45))}};
                        const auto light =
                            canvas.radialGradient({cx - size * .16, cy - size * .19}, {size * .75, size * .75}, stops);
                        canvas.target->FillEllipse(
                            D2D1::Ellipse(D2D1::Point2F(float(cx), float(cy)), float(size / 2), float(size / 2)),
                            light.Get());
                        canvas.oval(cx - size * .16, cy - size * .18, size * .10, size * .075,
                                    active || cap ? 0xffffff70 : 0xffffff18);
                    } else {
                        const Rect chip{cx - size / 2, cy - size / 2, size, size};
                        canvas.gradientBox(chip, size * .045, active || cap ? color : 0x3e343bff, dim(color, .58));
                        canvas.strokeBox(chip, size * .045, active || cap ? (color & 0xffffff00) | 0x88 : 0x5d535b88,
                                         {.width = size * .045});
                        canvas.line(chip.x + size * .12, chip.y + size * .12, chip.x + size * .88, chip.y + size * .12,
                                    0xffffff33, size * .05);
                    }
                });
            }
        }
        sprites_.flush(canvas);
    });
}
}  // namespace alice_matrix

namespace alice_widget {
cybercanvas::Rect Canvas::photoRegion(CdRect safe) const {
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
    return skin_->render(frame, safe, [&](auto& canvas) {
        if (spec.kind == Kind::Player) {
            const auto cover = skin_->region("cover");
            skin_->clear(canvas, cover, alice::kInk, true);
            if (media.cover.bitmap) {
                skin_->photo(canvas, cover, *media.cover.bitmap);
            } else {
                skin_->icon(
                    canvas,
                    {cover.x + cover.width * .25, cover.y + cover.height * .25, cover.width * .5, cover.height * .5},
                    "music-2", alice::kSilver);
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
            skin_->clear(canvas, r, alice::kInk, true);
            if (previous.bitmap && fade < 1) {
                canvas.clipped(r, [&] {
                    const auto bounds = album.motionEnabled ? widgetplugin::albumPhotoRect(
                                                                  r, album.previousMotionSeconds,
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
                    skin_->photo(canvas, bounds, *photo.bitmap, previous.bitmap ? fade : 1, settings.at("fit").text);
                });
            } else {
                skin_->label(canvas, L"请选择相册目录", r, r.height * .055);
            }
            skin_->button(canvas, skin_->region("previous"), "chevron-left", false, bool(photo.bitmap));
            skin_->button(canvas, skin_->region("next"), "chevron-right", false, bool(photo.bitmap));
            skin_->button(canvas, skin_->region("toggle"), album.paused ? "play" : "pause", false, bool(photo.bitmap));
        } else if (spec.kind == Kind::InputStats) {
            const bool valid = inputStats.status & CD_INPUT_STATS_ACTIVE;
            const auto positions =
                counterDrums ? counterDrums->sample(GetTickCount64()) : widgetplugin::CounterDrums::Positions{};
            for (unsigned channel = 0; channel < 2; ++channel) {
                const auto r = skin_->region(channel ? "mouse" : "keyboard");
                const auto digits =
                    widgetplugin::counterDigits(channel ? inputStats.mouse_today : inputStats.keyboard_today);
                for (unsigned i = 0; i < 5; ++i) {
                    const Rect plate{r.x + (i + .08) * r.width / 5, r.y, r.width * .84 / 5, r.height};
                    skin_->clear(canvas, plate);
                    if (skin_->textured()) {
                        canvas.gradientBox({plate.x + 6, plate.y + 3, plate.width - 12, plate.height - 6}, 1,
                                           0x710a1c44, 0x310c1544);
                    }
                    canvas.clipped(plate, [&] {
                        const double position = counterDrums ? positions[channel * 5 + i] : digits[i];
                        const double phase = valid ? position - std::floor(position) : 0;
                        for (unsigned line = 0; line < (phase > 0 ? 2u : 1u); ++line) {
                            const auto digit = unsigned(std::floor(position) + line) % 10;
                            skin_->label(canvas, valid ? std::to_wstring(digit) : L"-",
                                         {plate.x, plate.y + (line - phase) * plate.height, plate.width, plate.height},
                                         plate.height * .95, alice::kIvory, false);
                        }
                    });
                }
            }
        } else {
            const auto r = skin_->region("plot");
            if (spec.kind == Kind::Radial) {
                const double cx = r.x + r.width / 2, cy = r.y + r.height / 2,
                             radius = std::min(r.width, r.height) * .23;
                canvas.oval(cx, cy, radius - 3, radius - 3, 0x89132dff, 3);
                canvas.oval(cx, cy, radius - 6, radius - 6, 0xbaa07bff, 1.5);
                for (unsigned i = 0; i < 48; ++i) {
                    const double a = i * 2 * std::numbers::pi / 48, v = spectrum.sample(double(i) * 62 / 47);
                    const double end = radius + radius * (.08 + .60 * v);
                    canvas.saved([&] {
                        canvas.translate(cx, cy);
                        canvas.rotate(a * 180 / std::numbers::pi + 90);
                        metalRod(canvas, {-3, -end, 6, end - radius});
                        auto gem = canvas.path();
                        gem.moveTo({0, -end - 6})
                            .lineTo({4, -end - 1})
                            .lineTo({0, -end + 4})
                            .lineTo({-4, -end - 1})
                            .close();
                        canvas.fill(gem, alice::kRuby);
                        canvas.stroke(gem, 0xe9a4aaff, {.width = .65});
                    });
                }
            } else {
                for (unsigned i = 0; i < 16; ++i) {
                    const double v = spectrum.sample(double(i) * 62 / 15),
                                 peak = spectrum.sample(double(i) * 62 / 15, true);
                    const double x = r.x + (i + .5) * r.width / 16, base = r.y + r.height;
                    const double w = r.width / 46, h = std::max(2., v * r.height);
                    metalRod(canvas, {x - w / 2, base - h, w, h});
                    if (settings.at("peaks").integer) {
                        const double y = base - peak * r.height, side = w * .60;
                        auto gem = canvas.path();
                        gem.moveTo({x, y - side})
                            .lineTo({x + side, y})
                            .lineTo({x, y + side})
                            .lineTo({x - side, y})
                            .close();
                        canvas.fill(gem, alice::kRuby);
                        canvas.stroke(gem, 0xe9a4aaff, {.width = std::max(.6, w * .07)});
                    }
                }
            }
        }
    });
}
}  // namespace alice_widget

namespace alice_monitor {
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
    return skin_->render(frame, safe, [&](auto& canvas) {
        const unsigned count = spec.columns == 1 ? 1 : spec.rows == 1 ? unsigned(spec.columns) : 4;
        const std::array<double, 4> v{values.cpu, values.gpu, values.ram, values.temperature};
        const auto motion = options.interpolate ? options.needles : values;
        const std::array<double, 4> needles{motion.cpu, motion.gpu, motion.ram, motion.temperature};
        const std::array<std::wstring, 4> titles{L"CPU", L"GPU", L"RAM", L"TEMP"};
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
}  // namespace alice_monitor

namespace alice_productivity {
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
    return skin_->render(frame, safe, [&](auto& canvas) {
        if (spec.kind == Kind::Timer) {
            skin_->gauge(canvas, skin_->region("gauge"),
                         double(data.remaining) / std::max(1., settings.at("focus_minutes").integer * 60.), L"", L"");
            label(*skin_, canvas, "title", clockTime(data.remaining), 1.05);
            skin_->button(canvas, skin_->region("toggle"), data.running ? "pause" : "play", data.running);
            skin_->button(canvas, skin_->region("reset"), "rotate-ccw");
        } else if (spec.kind == Kind::Disk) {
            const double used = data.total ? 1 - double(data.free) / data.total : 0;
            skin_->gauge(canvas, skin_->region("gauge"), used, L"", L"");
            label(*skin_, canvas, "title", wide(settings.at("drive").text));
            label(*skin_, canvas, "usage",
                  data.valid ? std::format(L"{} / {} GB", (data.total - data.free) >> 30, data.total >> 30)
                             : L"-- / -- GB");
            label(*skin_, canvas, "free", data.valid ? std::format(L"可用 {} GB", data.available >> 30) : L"不可用");
        } else {
            const auto rate = [](uint64_t value) {
                return std::format(L"{:.1f} MB/s", double(value) / (1024 * 1024));
            };
            label(*skin_, canvas, "down", data.valid ? rate(data.down) : L"-- MB/s");
            label(*skin_, canvas, "up", data.valid ? rate(data.up) : L"-- MB/s");
            for (unsigned channel = 0; channel < 2; ++channel) {
                auto r = skin_->region(channel ? "upHistory" : "downHistory");
                skin_->clear(canvas, r);
                uint64_t maximum = 1;
                for (const auto& sample : data.history) {
                    maximum = std::max(maximum, channel ? sample.second : sample.first);
                }
                if (data.history.size() > 1) {
                    auto path = canvas.path();
                    for (size_t i = 0; i < data.history.size(); ++i) {
                        const auto& sample = data.history[i];
                        const alice::Point point{
                            r.x + r.width * i / (data.history.size() - 1),
                            r.y + r.height * (1 - double(channel ? sample.second : sample.first) / maximum)};
                        if (i) {
                            path.lineTo(point);
                        } else {
                            path.moveTo(point);
                        }
                    }
                    canvas.stroke(path, channel ? 0x7bc5e5ff : alice::kRuby, {.width = std::max(1., r.height * .08)});
                }
            }
        }
    });
}
}  // namespace alice_productivity

namespace alice_air {
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

bool Renderer::render(Spec, const cdsdk::Settings&, const Snapshot& data, bool loading, const CdFrame& frame,
                      CdRect safe) {
    return skin_->render(frame, safe, [&](auto& canvas) {
        const uint32_t status = !data.valid       ? 0x827577ff
                                : data.aqi <= 50  ? 0x79ce33ff
                                : data.aqi <= 100 ? 0xe1bd3aff
                                                  : 0xe95065ff;
        if (skin_->textured() && skin_->columns() == 4 && skin_->rows() == 2) {
            const auto arc = [&](std::string_view name, double start, double span) {
                const auto r = skin_->region(name);
                auto line = canvas.path();
                for (unsigned i = 0; i <= 40; ++i) {
                    const double a = (start + span * i / 40) * std::numbers::pi / 180;
                    const alice::Point p{r.x + r.width / 2 + std::cos(a) * r.width / 2,
                                         r.y + r.height / 2 + std::sin(a) * r.height / 2};
                    if (i) {
                        line.lineTo(p);
                    } else {
                        line.moveTo(p);
                    }
                }
                canvas.stroke(line, 0x11100cff, {.width = r.width * .09});
                canvas.stroke(line, status, {.width = r.width * .055});
            };
            arc("aqiArc", 150, 76);
            arc("pm25Arc", 139, 80);
            arc("pm10Arc", -40, 80);
        }
        const auto quality = skin_->region("quality");
        if (skin_->textured() && quality.width > 0) {
            canvas.gradientBox(quality, quality.height / 2, (status & 0xffffff00) | 0x80, 0x183510f0);
            canvas.strokeBox(quality, quality.height / 2, (status & 0xffffff00) | 0xb0, {.width = .7});
        }
        for (unsigned i = 0; i < 2; ++i) {
            const auto r = skin_->region(i ? "pm10Bar" : "pm25Bar");
            if (r.width <= 0) {
                continue;
            }
            canvas.box(r.x, r.y, r.width, r.height, r.height / 2, 0x19110fff);
            if (data.valid && data.pollutants[i] >= 0) {
                const double value = std::clamp(data.pollutants[i] / 100, 0., 1.);
                const auto color = level(data.pollutants[i]) == 0 ? 0x83d42eff : levelColor(level(data.pollutants[i]));
                canvas.gradientBox({r.x, r.y, r.width * value, r.height}, r.height / 2, 0xe8f8aaff, color);
            }
            for (unsigned j = 1; j < 5; ++j) {
                const double x = r.x + r.width * j / 5;
                canvas.line(x, r.y, x, r.y + r.height, 0x8f7750bb, .8);
            }
        }
        label(*skin_, canvas, "city", wide(data.city.empty() ? "空气质量" : data.city));
        label(*skin_, canvas, "aqi", number(data.aqi, data.valid), 1.05);
        label(*skin_, canvas, "quality",
              loading      ? L"更新中"
              : data.valid ? (data.aqi <= 50    ? L"优"
                              : data.aqi <= 100 ? L"良"
                                                : L"污染")
                           : L"暂无数据",
              .65);
        label(*skin_, canvas, "pm25", number(data.pollutants[0], data.valid && data.pollutants[0] >= 0), 1.05);
        label(*skin_, canvas, "pm10", number(data.pollutants[1], data.valid && data.pollutants[1] >= 0), 1.05);
        label(*skin_, canvas, "updated", data.cached ? L"缓存数据" : data.valid ? L"已更新" : L"尚未更新", .65);
    });
}

bool Renderer::renderReference(Spec spec, const cdsdk::Settings& settings, const Snapshot& data, bool loading,
                               const CdFrame& frame, CdRect safe) {
    return render(spec, settings, data, loading, frame, safe);
}
}  // namespace alice_air
