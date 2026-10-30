#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

#include "controllers/air/AirQuality.h"
#include "controllers/calendar/Calendar.h"
#include "controllers/hourglass/MechanicalHourglass.h"
#include "controllers/lunar/Lunar.h"
#include "controllers/matrix/Pixels.h"
#include "controllers/monitor/Monitor.h"
#include "controllers/productivity/Productivity.h"
#include "controllers/widget/Widget.h"
#include "plugins/songyun-desk/Skin.h"
#include "plugins/widget-suite/AlbumArtwork.h"
#include "plugins/widget-suite/InputCounter.h"

namespace {
using songyun::Canvas;
using songyun::Rect;
using songyun::Skin;

std::wstring number(double value, bool valid = true) {
    return valid && std::isfinite(value) ? std::to_wstring(int(std::round(value))) : L"--";
}

std::wstring clockTime(int64_t seconds) {
    seconds = std::max(int64_t(0), seconds);
    return std::format(L"{:02}:{:02}", seconds / 60, seconds % 60);
}

void label(Skin& skin, Canvas& canvas, std::string_view key, const std::wstring& value, double factor = 1.02) {
    const auto r = skin.region(key);
    if (skin.folder().starts_with("timer-") && key == "title") {
        factor = 1.02;
    } else if (skin.folder().starts_with("disk-")) {
        factor = key == "usage" ? .98 : key == "total" ? .92 : factor;
    } else if (skin.folder().starts_with("air-") && key == "aqi") {
        factor = 1.05;
    }
    skin.label(canvas, value, r, r.height * factor,
               skin.folder().starts_with("player-") ? songyun::kPaper : songyun::kIvory);
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
}  // namespace

namespace songyun_matrix {
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
        const unsigned cols = isClock(kind)           ? (text.size() > 5 ? 41u : 27u)
                              : skin_->columns() == 4 ? 22
                              : skin_->columns() == 6 ? (isRound(kind) ? 39 : 40)
                                                      : 47;
        const unsigned rows = isClock(kind) ? 7 : skin_->columns() == 6 ? 5 : 4;
        const double pitch = std::min(r.width / cols, r.height / rows), size = pitch * (isClock(kind) ? .94 : .76);
        const double pitchX = isClock(kind) ? pitch : r.width / cols;
        const double pitchY = isClock(kind) ? pitch : r.height / rows;
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
                const auto color = active || cap ? dim(base, brightness) : 0x313124ff;
                if (isClock(kind) && !active) {
                    continue;
                }
                const double cx = left + (x + .5) * pitchX, cy = top + (y + .5) * pitchY;
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
                            {0, cybercanvas::color(active || cap ? dim(color, 1.08) : 0x414831ff)},
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
                        canvas.gradientBox(chip, size * .045, active || cap ? color : 0x3a3d25ff, dim(color, .58));
                        canvas.strokeBox(chip, size * .045, active || cap ? (color & 0xffffff00) | 0x88 : 0x63674388,
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
}  // namespace songyun_matrix

namespace songyun_widget {
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
            if (media.cover.bitmap) {
                skin_->photo(canvas, cover, *media.cover.bitmap);
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
            skin_->clear(canvas, r, songyun::kInk, true);
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
            skin_->icon(canvas, skin_->region("keyboardIcon"), "keyboard", songyun::kIvory);
            skin_->icon(canvas, skin_->region("mouseIcon"), "mouse", songyun::kIvory);
            const auto positions =
                counterDrums ? counterDrums->sample(GetTickCount64()) : widgetplugin::CounterDrums::Positions{};
            for (unsigned channel = 0; channel < 2; ++channel) {
                const auto r = skin_->region(channel ? "mouse" : "keyboard");
                const auto digits =
                    std::format(L"{:06}", (channel ? inputStats.mouse_today : inputStats.keyboard_today) % 1000000);
                for (unsigned i = 0; i < 6; ++i) {
                    const Rect plate{r.x + (i + .08) * r.width / 6, r.y, r.width * .84 / 6, r.height};
                    skin_->clear(canvas, plate);
                    canvas.clipped(plate, [&] {
                        const double position =
                            counterDrums && i > 0 ? positions[channel * 5 + i - 1] : digits[i] - L'0';
                        const double phase = valid ? position - std::floor(position) : 0;
                        for (unsigned line = 0; line < (phase > 0 ? 2u : 1u); ++line) {
                            const auto digit = unsigned(std::floor(position) + line) % 10;
                            skin_->label(canvas, valid ? std::to_wstring(digit) : L"-",
                                         {plate.x, plate.y + (line - phase) * plate.height, plate.width, plate.height},
                                         plate.height * .81, songyun::kIvory, false);
                        }
                    });
                }
            }
        } else {
            const auto r = skin_->region("plot");
            if (spec.kind == Kind::Radial) {
                const double cx = r.x + r.width / 2, cy = r.y + r.height / 2;
                const auto hub = skin_->region("hubCircle");
                const double radius =
                    hub.width > 0 ? std::min(hub.width, hub.height) / 2 - 2 : std::min(r.width, r.height) * .20;
                const double limit = std::min(r.width, r.height) / 2 - 11;
                // The axle and resting spokes remain visible without audio. Only the
                // telescoping travel follows the real spectrum; no idle signal is added.
                for (double direction : {-1., 1.}) {
                    const double start = cy + direction * radius, end = cy + direction * (limit + 4);
                    canvas.line(cx, start, cx, end, 0x68491fff, 7);
                    canvas.line(cx - 1.5, start, cx - 1.5, end, 0xf0d69aff, 2);
                }
                constexpr unsigned count = 24;
                for (unsigned i = 0; i < count; ++i) {
                    const double a = i * 2 * std::numbers::pi / count;
                    const double v = std::clamp(double(spectrum.sample(double(i) * 62 / (count - 1))), 0., 1.);
                    const double travel = limit - radius;
                    const double end = radius + travel * (.48 + .52 * v);
                    canvas.saved([&] {
                        canvas.translate(cx, cy);
                        canvas.rotate(a * 180 / std::numbers::pi + 90);
                        canvas.gradientBox({-5.5, -end, 11, end - radius + 2}, 3, 0xf2ce83ff, 0x9b672bff);
                        canvas.line(-2.7, -end + 2, -2.7, -radius, 0xffe7aaff, 1.8);
                        canvas.line(4, -end + 2, 4, -radius, 0x785022ff, 1.2);
                        const D2D1_GRADIENT_STOP stops[]{{0, cybercanvas::color(0xedf0cfff)},
                                                         {.36f, cybercanvas::color(0xbacf9dff)},
                                                         {1, cybercanvas::color(0x607b59ff)}};
                        const auto jade = canvas.radialGradient({-2.5, -end - 3}, {12, 12}, stops);
                        canvas.target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(0, float(-end)), 8, 8), jade.Get());
                        canvas.oval(0, -end, 8, 8, 0x6d8055ff, 1);
                    });
                }
            } else {
                constexpr unsigned count = 32;
                for (unsigned i = 0; i < count; ++i) {
                    const double v = std::clamp(double(spectrum.sample(double(i) * 62 / (count - 1))), 0., 1.);
                    const double peak = std::clamp(double(spectrum.sample(double(i) * 62 / (count - 1), true)), 0., 1.);
                    const double x = r.x + (i + .5) * r.width / count, base = r.y + r.height;
                    const double w = r.width / count * .70, h = std::max(5., v * (r.height - w * .4));
                    canvas.gradientBox({x - w / 2, base - h, w, h}, w * .10, 0xebbd6aff, 0xb47c32ff);
                    canvas.line(x - w * .23, base - h + 2, x - w * .23, base - 1, 0xffd78fff, w * .12);
                    const double y = base - h;
                    canvas.oval(x, y, w * .36, w * .36, 0x8ca886ff);
                    canvas.oval(x - w * .10, y - w * .12, w * .13, w * .10, 0xe4efd7ff);
                    if (settings.at("peaks").integer && peak * r.height > h + w) {
                        const double peakY = base - peak * (r.height - w * .4);
                        canvas.line(x - w * .3, peakY, x + w * .3, peakY, 0xaac8a6aa, w * .12);
                    }
                }
            }
        }
    });
}
}  // namespace songyun_widget

namespace songyun_monitor {
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
}  // namespace songyun_monitor

namespace songyun_productivity {
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
                         double(data.remaining) / std::max(1., settings.at("focus_minutes").integer * 60.), L"",
                         data.resting ? L"休息" : L"专注");
            label(*skin_, canvas, "title", clockTime(data.remaining));
            skin_->button(canvas, skin_->region("toggle"), data.running ? "pause" : "play", data.running);
            skin_->button(canvas, skin_->region("reset"), "rotate-ccw");
        } else if (spec.kind == Kind::Disk) {
            const double used = data.total ? 1 - double(data.free) / data.total : 0;
            skin_->gauge(canvas, skin_->region("gauge"), used, L"", L"");
            label(*skin_, canvas, "title", wide(settings.at("drive").text));
            label(*skin_, canvas, "usage", data.valid ? std::format(L"{}", (data.total - data.free) >> 30) : L"--");
            label(*skin_, canvas, "total", data.valid ? std::format(L"/ {} GB", data.total >> 30) : L"/ -- GB");
            label(*skin_, canvas, "free", data.valid ? std::format(L"可用 {} GB", data.available >> 30) : L"不可用");
        } else {
            const auto rate = [](uint64_t value) {
                return std::format(L"{:.1f} MB/s", double(value) / (1024 * 1024));
            };
            label(*skin_, canvas, "down", data.valid ? rate(data.down) : L"-- MB/s");
            label(*skin_, canvas, "up", data.valid ? rate(data.up) : L"-- MB/s");
            skin_->icon(canvas, skin_->region("downIcon"), "arrow-down", 0x568466ff);
            skin_->icon(canvas, skin_->region("upIcon"), "arrow-up", 0xb87b3cff);
            skin_->gauge(canvas, skin_->region("gauge0"), std::min(1., double(data.down) / (10 * 1024 * 1024)), L"",
                         L"");
            skin_->gauge(canvas, skin_->region("gauge1"), std::min(1., double(data.up) / (10 * 1024 * 1024)), L"", L"",
                         0xbc8e45ff);
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
                        const songyun::Point point{
                            r.x + r.width * i / (data.history.size() - 1),
                            r.y + r.height * (1 - double(channel ? sample.second : sample.first) / maximum)};
                        if (i) {
                            path.lineTo(point);
                        } else {
                            path.moveTo(point);
                        }
                    }
                    canvas.stroke(path, channel ? 0x7bc5e5ff : songyun::kRuby, {.width = std::max(1., r.height * .08)});
                }
            }
        }
    });
}
}  // namespace songyun_productivity

namespace songyun_air {
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
        label(*skin_, canvas, "city", wide(data.city.empty() ? "空气质量" : data.city));
        label(*skin_, canvas, "aqi", number(data.aqi, data.valid));
        const auto quality = skin_->region("quality");
        if (data.valid && !loading) {
            canvas.gradientBox(quality, quality.height * .16, 0x65aa76ff, 0x2b7751ff);
            skin_->label(canvas,
                         data.aqi <= 50    ? L"优"
                         : data.aqi <= 100 ? L"良"
                                           : L"污染",
                         quality, quality.height * .72, songyun::kPaper, false);
        } else {
            label(*skin_, canvas, "quality", loading ? L"更新中" : L"暂无数据", .65);
        }
        label(*skin_, canvas, "pm25", number(data.pollutants[0], data.valid && data.pollutants[0] >= 0));
        label(*skin_, canvas, "pm10", number(data.pollutants[1], data.valid && data.pollutants[1] >= 0));
        label(*skin_, canvas, "updated", data.cached ? L"缓存数据" : data.valid ? L"已更新" : L"尚未更新", .65);
        label(*skin_, canvas, "aqiLabel", skin_->rows() == 1 ? L"AQI" : L"空气质量", .66);
        label(*skin_, canvas, "pm25Label", L"PM2.5", .63);
        label(*skin_, canvas, "pm10Label", L"PM10", .63);
        skin_->progress(canvas, skin_->region("progress0"), data.valid ? data.aqi / 500. : 0);
        skin_->progress(canvas, skin_->region("progress1"), data.valid ? data.pollutants[0] / 150. : 0);
        skin_->progress(canvas, skin_->region("progress2"), data.valid ? data.pollutants[1] / 200. : 0);
        skin_->gauge(canvas, skin_->region("gauge"), data.valid ? data.aqi / 500. : 0, L"", L"");
        skin_->gauge(canvas, skin_->region("pm25Gauge"), data.valid ? data.pollutants[0] / 150. : 0, L"", L"");
        skin_->gauge(canvas, skin_->region("pm10Gauge"), data.valid ? data.pollutants[1] / 200. : 0, L"", L"");
    });
}

bool Renderer::renderReference(Spec spec, const cdsdk::Settings& settings, const Snapshot& data, bool loading,
                               const CdFrame& frame, CdRect safe) {
    return render(spec, settings, data, loading, frame, safe);
}
}  // namespace songyun_air
