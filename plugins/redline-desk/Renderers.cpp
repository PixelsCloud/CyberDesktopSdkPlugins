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
#include "plugins/redline-desk/Skin.h"
#include "plugins/widget-suite/AlbumArtwork.h"
#include "plugins/widget-suite/InputCounter.h"

namespace {
using redline::Canvas;
using redline::Rect;
using redline::Skin;

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
    skin.label(canvas, value, r, r.height * factor, redline::kIvory);
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

void record(Skin& skin, Canvas& canvas, const redline_widget::Media& media) {
    const auto r = skin.region("recordCircle");
    if (r.width <= 0 || r.height <= 0) {
        return;
    }
    canvas.saved([&] {
        canvas.translate(r.x + r.width / 2, r.y + r.height / 2);
        canvas.scale(1, r.height / r.width);
        const double radius = r.width / 2;
        canvas.oval(0, 0, radius, radius, 0x090c0fff);
        for (unsigned groove = 0; groove < 28; ++groove) {
            const double ring = radius * (.49 + .49 * groove / 28);
            canvas.oval(0, 0, ring, ring, groove % 3 == 0 ? 0x50555d88 : 0x343b4277, .8);
        }
        canvas.saved([&] {
            canvas.rotate(media.recordDegrees);
            const double size = radius * .94;
            const redline::Rect label{-size / 2, -size / 2, size, size};
            if (media.cover.bitmap) {
                skin.photo(canvas, label, *media.cover.bitmap);
            } else {
                auto mask = canvas.path();
                mask.moveTo({size / 2, 0})
                    .arcTo({-size / 2, 0}, {size / 2, size / 2})
                    .arcTo({size / 2, 0}, {size / 2, size / 2})
                    .close();
                canvas.masked(mask, [&] {
                    skin.part(canvas, "recordLabel", label);
                });
            }
            canvas.line(radius * .65, -radius * .55, radius * .72, -radius * .61, redline::kRuby, radius * .025);
        });
        canvas.oval(0, 0, radius * .035, radius * .035, redline::kSilver);
        canvas.oval(0, 0, radius * .015, radius * .015, redline::kInk);
    });
    // Keep the pickup and its mounting bracket fixed above the rotating platter.
    skin.part(canvas, "tonearmCover", skin.region("tonearmCover"));
}
}  // namespace

namespace redline_matrix {
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
                const auto color = active || cap ? dim(base, brightness) : 0x242a2fff;
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
                            {0, cybercanvas::color(active || cap ? dim(color, 1.08) : 0x343c43ff)},
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
                        canvas.gradientBox(chip, size * .045, active || cap ? color : 0x343c43ff, dim(color, .58));
                        canvas.strokeBox(chip, size * .045, active || cap ? (color & 0xffffff00) | 0x88 : 0x68737d88,
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
}  // namespace redline_matrix

namespace redline_widget {
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

cybercanvas::Rect Canvas::displayRegion(std::string_view name, CdRect safe) const {
    return skin_->displayRegion(name, safe);
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
        skin_->controls(
            safe, {{1, skin_->region("previous"), "previous", bool(photo.bitmap) && photo.count > 1 && !album.pending},
                   {2, skin_->region("toggle"), "toggle", bool(photo.bitmap)},
                   {3, skin_->region("next"), "next", bool(photo.bitmap) && photo.count > 1 && !album.pending}});
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
        const bool english = settings.at("language").text == "en";
        if (spec.kind == Kind::Player) {
            if (skin_->folder() == "player-4x2") {
                record(*skin_, canvas, media);
            }
            const auto cover = skin_->region("cover");
            if (media.cover.bitmap) {
                skin_->photo(canvas, cover, *media.cover.bitmap);
            }
            label(*skin_, canvas, "title",
                  media.title.empty() ? (english ? L"Not playing" : L"未在播放") : media.title);
            label(*skin_, canvas, "artist", media.artist);
            const double position = mediaPosition(media, GetTickCount64());
            if (settings.at("show_progress").integer) {
                skin_->progress(canvas, skin_->region("progress"), media.duration > 0 ? position / media.duration : 0);
                label(*skin_, canvas, "elapsed", clockTime(int64_t(position)));
                label(*skin_, canvas, "duration", media.duration > 0 ? clockTime(int64_t(media.duration)) : L"--:--");
            }
            for (unsigned i = 0; i < 3; ++i) {
                const std::array<std::string, 3> keys{"previous", "toggle", "next"};
                const auto icon = i == 0                             ? "skip-back"
                                  : i == 2                           ? "skip-forward"
                                  : media.status == CD_MEDIA_PLAYING ? "pause"
                                                                     : "play";
                constexpr uint32_t flags[]{CD_MEDIA_CAN_PREVIOUS, CD_MEDIA_CAN_TOGGLE, CD_MEDIA_CAN_NEXT};
                const bool enabled =
                    !media.source.empty() && (media.controls & flags[i]) && media.commandResult != CD_ERROR_BUSY;
                skin_->button(canvas, skin_->region(keys[i]), icon, pressed == int(i + 1), enabled);
            }
        } else if (spec.kind == Kind::Album) {
            const auto r = skin_->region("photo");
            skin_->clear(canvas, r, redline::kInk, true);
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
                skin_->label(canvas, english ? L"Choose an album folder" : L"请选择相册目录", r, r.height * .055);
            }
            const bool navigate = photo.bitmap && photo.count > 1 && !album.pending;
            skin_->button(canvas, skin_->region("previous"), "chevron-left", false, navigate);
            skin_->button(canvas, skin_->region("next"), "chevron-right", false, navigate);
            skin_->button(canvas, skin_->region("toggle"), album.paused ? "play" : "pause", false, bool(photo.bitmap));
            if (settings.at("counter").integer) {
                label(*skin_, canvas, "counter",
                      photo.count ? std::format(L"{} / {}", photo.index + 1, photo.count) : L"0 / 0", .70);
            }
        } else if (spec.kind == Kind::InputStats) {
            const bool valid = inputStats.status & CD_INPUT_STATS_ACTIVE;
            skin_->icon(canvas, skin_->region("keyboardIcon"), "keyboard", redline::kIvory);
            skin_->icon(canvas, skin_->region("mouseIcon"), "mouse", redline::kIvory);
            const auto positions =
                counterDrums ? counterDrums->sample(GetTickCount64()) : widgetplugin::CounterDrums::Positions{};
            for (unsigned channel = 0; channel < 2; ++channel) {
                const auto r = skin_->region(channel ? "mouse" : "keyboard");
                skin_->label(canvas, channel ? L"今日点击" : L"今日按键",
                             {r.x, r.y + r.height * 1.20, r.width, r.height * .40}, r.height * .32, redline::kSilver,
                             false);
                const auto digits =
                    widgetplugin::counterDigits(channel ? inputStats.mouse_today : inputStats.keyboard_today);
                for (unsigned i = 0; i < digits.size(); ++i) {
                    const Rect plate{r.x + (i + .08) * r.width / 5, r.y, r.width * .84 / 5, r.height};
                    skin_->clear(canvas, plate);
                    canvas.clipped(plate, [&] {
                        const double position = counterDrums ? positions[channel * 5 + i] : digits[i];
                        const double phase = valid ? position - std::floor(position) : 0;
                        for (unsigned line = 0; line < (phase > 0 ? 2u : 1u); ++line) {
                            const auto digit = unsigned(std::floor(position) + line) % 10;
                            skin_->label(canvas, valid ? std::to_wstring(digit) : L"-",
                                         {plate.x, plate.y + (line - phase) * plate.height, plate.width, plate.height},
                                         plate.height * .81, redline::kIvory, false);
                        }
                    });
                }
            }
        } else {
            const auto r = skin_->region("plot");
            const bool radial = spec.kind == Kind::Radial;
            constexpr unsigned count = 48;
            const double cx = r.x + r.width / 2, cy = r.y + r.height / 2;
            const double radius = std::min(r.width, r.height) * .28;
            if (radial) {
                const double face = std::min(r.width, r.height) * .50;
                canvas.oval(cx, cy, face, face, redline::kInk);
            } else {
                skin_->clear(canvas, r, redline::kInk, true);
            }
            for (unsigned i = 0; i < count; ++i) {
                const double v = std::clamp(double(spectrum.sample(double(i) * 62 / (count - 1))), 0., 1.);
                const double peak = std::clamp(double(spectrum.sample(double(i) * 62 / (count - 1), true)), 0., 1.);
                if (radial) {
                    const double a = i * 2 * std::numbers::pi / count;
                    const double end = radius + v * radius * .62;
                    canvas.line(cx + std::sin(a) * radius, cy - std::cos(a) * radius, cx + std::sin(a) * end,
                                cy - std::cos(a) * end, redline::kIvory, r.width * .014);
                    if (settings.at("peaks").integer && peak > .02) {
                        const double tip = radius + peak * radius * .62;
                        canvas.oval(cx + std::sin(a) * tip, cy - std::cos(a) * tip, r.width * .008, r.width * .008,
                                    redline::kRuby);
                    }
                } else {
                    const double w = r.width / count * .65, x = r.x + (i + .5) * r.width / count - w / 2;
                    const double h = std::max(1., v * r.height);
                    canvas.box(x, r.y + r.height - h, w, h, 0, redline::kIvory);
                    if (settings.at("peaks").integer && peak > .01) {
                        canvas.box(x, r.y + r.height - peak * r.height, w, 2, 0, redline::kRuby);
                    }
                }
            }
        }
    });
}
}  // namespace redline_widget

namespace redline_monitor {
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
        const bool english = options.language == "en";
        const std::array<std::wstring, 4> titles{L"CPU", L"GPU", english ? L"RAM" : L"内存",
                                                 english ? L"TEMP" : L"温度"};
        const uint32_t flags[]{CD_SYSTEM_CPU_VALID, CD_SYSTEM_GPU_VALID, CD_SYSTEM_MEMORY_VALID,
                               CD_SYSTEM_CPU_TEMPERATURE_VALID};
        for (unsigned i = 0; i < count; ++i) {
            const unsigned primary = options.primary == "ram"           ? 2
                                     : options.primary == "gpu"         ? 1
                                     : options.primary == "temperature" ? 3
                                                                        : 0;
            const unsigned metric = count == 1 ? primary : count == 2 ? (i == 0 ? 0 : 2) : i;
            skin_->gauge(
                canvas, skin_->region("gauge" + std::to_string(i)), needles[metric] / (metric == 3 ? 120 : 100),
                number(v[metric], values.valid & flags[metric]) + (metric == 3 ? L"°C" : L"%"), titles[metric]);
        }
    });
}
}  // namespace redline_monitor

namespace redline_productivity {
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
        const bool english = settings.at("language").text == "en";
        if (spec.kind == Kind::Timer) {
            const bool focus = settings.at("mode").text == "focus";
            const int64_t duration = settings.at(data.resting ? "break_minutes" : "focus_minutes").integer * 60;
            skin_->gauge(canvas, skin_->region("gauge"), double(data.remaining) / std::max<int64_t>(1, duration), L"",
                         L"");
            label(*skin_, canvas, "phase",
                  !focus         ? (english ? L"Countdown" : L"倒计时")
                  : data.resting ? (english ? L"Rest" : L"休息")
                                 : (english ? L"Focus" : L"专注"),
                  .75);
            const int round = data.resting ? std::max(1, data.completed) : data.completed + 1;
            if (focus) {
                label(*skin_, canvas, "round",
                      english ? std::format(L"Round {}", round) : std::format(L"第 {} 轮", round), .65);
                label(*skin_, canvas, "upcoming",
                      data.resting ? (english ? L"Next: focus" : L"下次：专注")
                                   : (english ? L"Next: rest" : L"下次：休息"),
                      .55);
            }
            label(*skin_, canvas, "title", clockTime(data.remaining));
            skin_->button(canvas, skin_->region("toggle"), data.running ? "pause" : "play", data.running);
            skin_->button(canvas, skin_->region("reset"), "rotate-ccw");
        } else if (spec.kind == Kind::Disk) {
            const double used = data.valid && data.total ? std::clamp(1 - double(data.free) / data.total, 0., 1.) : 0;
            skin_->icon(canvas, skin_->region("driveIcon"), "hard-drive", redline::kSilver);
            label(*skin_, canvas, "title", wide(settings.at("drive").text));
            label(*skin_, canvas, "usage", data.valid ? std::format(L"{}", (data.total - data.free) >> 30) : L"--");
            label(*skin_, canvas, "total", data.valid ? std::format(L"/ {} GiB", data.total >> 30) : L"/ -- GiB");
            label(*skin_, canvas, "free",
                  data.valid ? (english ? std::format(L"Free {} GiB", data.available >> 30)
                                        : std::format(L"可用 {} GiB", data.available >> 30))
                             : (english ? L"Unavailable" : L"不可用"));
            label(*skin_, canvas, "percent", data.valid ? std::format(L"{:.0f}%", used * 100) : L"--%", .85);
            const auto bar = skin_->region("capacity");
            const double animated = data.needles[0] >= 0 ? std::clamp(data.needles[0], 0., 1.) : used;
            constexpr unsigned segments = 20;
            const double pitch = bar.width / segments, gap = pitch * .25;
            for (unsigned i = 0; i < segments; ++i) {
                const redline::Rect cell{bar.x + i * pitch, bar.y, pitch - gap, bar.height};
                canvas.box(cell.x, cell.y, cell.width, cell.height, gap / 2, 0x343c43ff);
                const double fill = data.valid ? std::clamp(animated * segments - i, 0., 1.) : 0;
                if (fill > 0) {
                    canvas.box(cell.x, cell.y, cell.width * fill, cell.height, gap / 2,
                               used >= .95 ? redline::kRuby : 0xd15a52ff);
                }
            }
        } else {
            const auto rate = [](uint64_t value) {
                return std::format(L"{:.1f} MB/s", double(value) / (1024 * 1024));
            };
            label(*skin_, canvas, "down", data.valid ? rate(data.down) : L"-- MB/s");
            label(*skin_, canvas, "up", data.valid ? rate(data.up) : L"-- MB/s");
            skin_->icon(canvas, skin_->region("downIcon"), "arrow-down", redline::kRuby);
            skin_->icon(canvas, skin_->region("upIcon"), "arrow-up", redline::kIvory);
            skin_->gauge(canvas, skin_->region("gauge0"), std::min(1., double(data.down) / (10 * 1024 * 1024)), L"",
                         L"");
            skin_->gauge(canvas, skin_->region("gauge1"), std::min(1., double(data.up) / (10 * 1024 * 1024)), L"", L"",
                         0xbc8e45ff);
            for (unsigned channel = 0; channel < 2; ++channel) {
                if (!settings.at("history").integer) {
                    continue;
                }
                auto r = skin_->region(channel ? "upHistory" : "downHistory");
                skin_->clear(canvas, r);
                uint64_t maximum = 1;
                for (const auto& sample : data.history) {
                    maximum = std::max(maximum, channel ? sample.second : sample.first);
                }
                if (data.history.size() > 1) {
                    const double pad = std::max(1., r.height * .08);
                    auto path = canvas.path();
                    for (size_t i = 0; i < data.history.size(); ++i) {
                        const auto& sample = data.history[i];
                        const redline::Point point{
                            r.x + pad + (r.width - 2 * pad) * i / (data.history.size() - 1),
                            r.y + pad +
                                (r.height - 2 * pad) * (1 - double(channel ? sample.second : sample.first) / maximum)};
                        if (i) {
                            path.lineTo(point);
                        } else {
                            path.moveTo(point);
                        }
                    }
                    canvas.stroke(path, channel ? 0x7bc5e5ff : redline::kRuby, {.width = std::max(1., r.height * .08)});
                }
            }
        }
    });
}
}  // namespace redline_productivity

namespace redline_air {
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
        const bool english = settings.at("language").text == "en";
        label(*skin_, canvas, "city", wide(data.city.empty() ? "空气质量" : data.city));
        label(*skin_, canvas, "aqi", number(data.aqi, data.valid));
        const auto quality = skin_->region("quality");
        if (data.valid && !loading) {
            canvas.gradientBox(quality, quality.height * .16, 0x65aa76ff, 0x2b7751ff);
            skin_->label(canvas,
                         data.aqi <= 50    ? (english ? L"Good" : L"优")
                         : data.aqi <= 100 ? (english ? L"Fair" : L"良")
                                           : (english ? L"Poor" : L"污染"),
                         quality, quality.height * .72, redline::kPaper, false);
        } else {
            label(*skin_, canvas, "quality",
                  loading ? (english ? L"Updating" : L"更新中") : (english ? L"No data" : L"暂无数据"), .65);
        }
        label(*skin_, canvas, "pm25", number(data.pollutants[0], data.valid && data.pollutants[0] >= 0));
        label(*skin_, canvas, "pm10", number(data.pollutants[1], data.valid && data.pollutants[1] >= 0));
        label(*skin_, canvas, "ozone", number(data.pollutants[2], data.valid && data.pollutants[2] >= 0));
        label(*skin_, canvas, "updated", data.cached ? L"缓存数据" : data.valid ? L"已更新" : L"尚未更新", .65);
        label(*skin_, canvas, "aqiLabel", english || skin_->rows() == 1 ? L"AQI" : L"空气质量", .66);
        label(*skin_, canvas, "pm25Label", L"PM2.5", .63);
        label(*skin_, canvas, "pm10Label", L"PM10", .63);
        label(*skin_, canvas, "ozoneLabel", L"O3", .63);
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
}  // namespace redline_air
