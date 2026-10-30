#include <Windows.h>
#include <sdk/Update.h>
#include <sdk/cpp/Services.h>
#include <sdk/services/ImageViewer.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>

#include "plugins/vampire-alice/Skin.h"
#include "plugins/vampire-alice/controllers/widget/Widget.h"
#include "plugins/widget-suite/AlbumArtwork.h"
#include "plugins/widget-suite/InputCounter.h"

namespace alice_widget {
#define PRESENTATION                                                                                   \
    {"background_corner_radius", CD_SETTING_INTEGER, 5, 0, "", 0, 32},                                 \
        {"font_family", CD_SETTING_FONT, 0, 0, mechanical ? "Microsoft YaHei UI" : "@siyuan-heavy"}, { \
        "language", CD_SETTING_CHOICE, 0, 0, "zh-CN", 0, 0, {                                          \
            "zh-CN", "en"                                                                              \
        }                                                                                              \
    }

cdsdk::Settings defaults(Kind kind, bool mechanical) {
    if (kind == Kind::InputStats && mechanical) {
        return {{{"show_total", CD_SETTING_TOGGLE, 0}, {"reduced_motion", CD_SETTING_TOGGLE, 0}, PRESENTATION}};
    }
    if (kind == Kind::InputStats)
        return {{{"show_background", CD_SETTING_TOGGLE, 1},
                 {"background", CD_SETTING_COLOR, 0x17232BFF},
                 {"foreground", CD_SETTING_COLOR, 0xEDF6F5FF},
                 {"keyboard_color", CD_SETTING_COLOR, 0x4BE3B3FF},
                 {"mouse_color", CD_SETTING_COLOR, 0x67B6FFFF},
                 {"show_total", CD_SETTING_TOGGLE, 1},
                 PRESENTATION}};
    if (kind == Kind::Player)
        return {{{"player",
                  CD_SETTING_CHOICE,
                  0,
                  0,
                  "auto",
                  0,
                  0,
                  {"auto", "cyber_player", "qqmusic", "cloudmusic", "kugou", "spotify"}},
                 {"show_background", CD_SETTING_TOGGLE, 1},
                 {"background", CD_SETTING_COLOR, 0x17232BFF},
                 {"foreground", CD_SETTING_COLOR, 0xEDF6F5FF},
                 {"accent", CD_SETTING_COLOR, 0x4BE3B3FF},
                 {"show_progress", CD_SETTING_TOGGLE, 1},
                 {"reduced_motion", CD_SETTING_TOGGLE, 0},
                 PRESENTATION}};
    if (kind == Kind::Album && mechanical) {
        return {{{"directory", CD_SETTING_TEXT, 0, 0, "", 0, 2048},
                 {"recursive", CD_SETTING_TOGGLE, 0},
                 {"interval", CD_SETTING_INTEGER, 8, 0, "", 2, 300},
                 {"transition", CD_SETTING_NUMBER, 0, .45, "", 0, 2},
                 {"fit", CD_SETTING_CHOICE, 0, 0, "cover", 0, 0, {"cover", "contain"}},
                 {"counter", CD_SETTING_TOGGLE, 1},
                 {"photo_motion", CD_SETTING_TOGGLE, 1},
                 {"reduced_motion", CD_SETTING_TOGGLE, 0},
                 PRESENTATION}};
    }
    if (kind == Kind::Album)
        return {{{"directory", CD_SETTING_TEXT, 0, 0, "", 0, 2048},
                 {"recursive", CD_SETTING_TOGGLE, 0},
                 {"interval", CD_SETTING_INTEGER, 8, 0, "", 2, 300},
                 {"transition", CD_SETTING_NUMBER, 0, .45, "", 0, 2},
                 {"fit", CD_SETTING_CHOICE, 0, 0, "cover", 0, 0, {"cover", "contain"}},
                 {"counter", CD_SETTING_TOGGLE, 1},
                 PRESENTATION}};
    if (kind == Kind::Calendar)
        return {{{"week_start", CD_SETTING_CHOICE, 0, 0, "monday", 0, 0, {"monday", "sunday"}},
                 {"background", CD_SETTING_COLOR, 0xF7F2E9FF},
                 {"accent", CD_SETTING_COLOR, 0xF97819FF},
                 {"adjacent_days", CD_SETTING_TOGGLE, 0},
                 PRESENTATION}};
    if (isMatrix(kind)) {
        const int64_t color = kind == Kind::RoundMatrix ? 0x05DC43FF : 0xFFFF16FF;
        return {{{"gain", CD_SETTING_NUMBER, 0, 3.5, "", .2, 5},
                 {"release", CD_SETTING_INTEGER, 160, 0, "", 40, 600},
                 {"peaks", CD_SETTING_TOGGLE, 1},
                 {"color", CD_SETTING_COLOR, color},
                 {"color_top", CD_SETTING_COLOR, color},
                 {"show_background", CD_SETTING_TOGGLE, 1},
                 {"background", CD_SETTING_COLOR, 0x171219FF},
                 {"matrix_rows", CD_SETTING_INTEGER, 9, 0, "", 6, 24},
                 {"glow_strength", CD_SETTING_NUMBER, 0, 70, "", 0, 100},
                 PRESENTATION}};
    }

    return {{{"gain", CD_SETTING_NUMBER, 0, 3.5, "", .2, 5},
             {"release", CD_SETTING_INTEGER, 160, 0, "", 40, 600},
             {"peaks", CD_SETTING_TOGGLE, 1},
             {"color", CD_SETTING_COLOR, 0x11CADFFF},
             {"color_top", CD_SETTING_COLOR, 0x46E9B1FF},
             {"show_background", CD_SETTING_TOGGLE, 0},
             {"background", CD_SETTING_COLOR, 0x17222CDD},
             PRESENTATION}};
}

#undef PRESENTATION

namespace {
struct Instance {
    Spec spec;
    cdsdk::Settings settings;
    Canvas canvas;
    Spectrum spectrum;
    Media media;
    CdMediaHostApi mediaHost{};
    CdInputStatsHostApi inputHost{};
    CdAudioServiceApi audioService{};
    CdImageViewerApi imageViewer{};
    CdInputStats inputStats{sizeof(CdInputStats)};
    CounterDrums counterDrums;
    uint64_t inputStatsToken = 0;
    std::unique_ptr<Album> album;
    Photo photo, previous;
    Date date = localDate();
    int monthOffset = 0;
    CdHostApi host{};
    void* context = nullptr;
    cdsdk::Timer timer;
    uint64_t audioToken = 0, lastAudio = 0, lastTick = 0, lastPhoto = 0, fadeStart = 0;
    bool paused = false, photoPending = false, slideshowPaused = false;
    double photoMotionSeconds = 0, previousPhotoMotionSeconds = 0;
    uint64_t mediaToken = 0;
    int pressedPlayer = 0;
    int64_t shownPosition = -1;
    float fade = 1;
    CdRect safe{};

    explicit Instance(Spec value) : spec(value), settings(alice_widget::defaults(value.kind, value.mechanical)) {
        if (spec.kind == Kind::Album)
            album = std::make_unique<Album>();
    }

    ~Instance() {
        timer.stop();
    }

    bool music() const {
        return isMusic(spec.kind);
    }

    void redraw() {
        host.request_redraw(context);
    }

    void requestPhoto(int step = 0, bool rescan = false) {
        album->request(settings.at("directory").text, settings.at("recursive").integer != 0, step, rescan);
        photoPending = true;
    }
};

void CD_CALL audio(void* pointer, const float* values, uint32_t size, uint32_t, uint32_t) noexcept {
    auto& self = *static_cast<Instance*>(pointer);
    if (self.paused || !values)
        return;
    self.spectrum.feed(values, size);
    self.lastAudio = GetTickCount64();
}

std::wstring wide(const char* text) {
    if (!text)
        return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (length <= 1 || length > 4096)
        return {};
    std::wstring result(size_t(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, result.data(), length);
    result.pop_back();
    return result;
}

void CD_CALL tick(void* pointer, uint64_t) noexcept;

bool recordMotion(const Instance& self) {
    BOOL enabled = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0);
    return !self.settings.at("reduced_motion").integer && enabled;
}

bool albumMotion(const Instance& self) {
    return self.spec.mechanical && self.settings.at("photo_motion").integer && recordMotion(self);
}

CdResult albumTimer(Instance& self) {
    if (!self.album || self.paused) {
        return CD_OK;
    }
    const auto interval = widgetplugin::albumTimerInterval(
        self.photo, self.fade, albumMotion(self), self.slideshowPaused || self.photoPending,
        uint64_t(self.settings.at("interval").integer) * 1000, self.lastPhoto, GetTickCount64());
    if (!interval) {
        self.timer.stop();
        return CD_OK;
    }
    return self.timer.replace(interval, tick, &self);
}

CdResult playerTimer(Instance& self) {
    if (self.paused) {
        return CD_OK;
    }
    const uint32_t interval =
        self.media.status == CD_MEDIA_PLAYING && !self.media.source.empty() && recordMotion(self) ? 33 : 1000;
    if (self.timer && self.timer.interval() == interval) {
        return CD_OK;
    }
    const auto result = self.timer.replace(interval, tick, &self);
    if (result != CD_OK) {
        return result;
    }
    self.lastTick = GetTickCount64();
    return CD_OK;
}

void CD_CALL mediaChanged(void* pointer, const CdMediaInfo* info) noexcept {
    try {
        if (!pointer || !info || info->struct_size < sizeof(CdMediaInfo))
            return;
        auto& self = *static_cast<Instance*>(pointer);
        if (self.paused)
            return;
        Media media;
        media.source = wide(info->source_id);
        media.title = wide(info->title);
        media.artist = wide(info->artist);
        media.album = wide(info->album);
        media.status = info->status;
        media.controls = info->controls;
        media.position = std::isfinite(info->position_seconds) ? std::max(0.0, info->position_seconds) : 0;
        media.duration = std::isfinite(info->duration_seconds) ? std::max(0.0, info->duration_seconds) : 0;
        media.rate = std::isfinite(info->playback_rate) ? std::clamp(info->playback_rate, 0.0, 16.0) : 1;
        media.sampledAt = info->sampled_at_ms;
        media.coverGeneration = info->cover_generation;
        media.commandResult = info->command_result;
        media.recordDegrees = self.media.recordDegrees;
        const bool sameTrack = media.source == self.media.source && media.title == self.media.title &&
                               media.artist == self.media.artist && media.album == self.media.album;
        media.tonearmProgress = sameTrack            ? self.media.tonearmProgress
                                : media.duration > 0 ? std::clamp(media.position / media.duration, 0., 1.)
                                                     : .1;
        media.tonearmSeconds = sameTrack ? self.media.tonearmSeconds : 0;
        if (media.coverGeneration == self.media.coverGeneration && media.source == self.media.source)
            media.cover = self.media.cover;
        else if (info->cover_rgba && info->cover_width && info->cover_height && info->cover_width <= 256 &&
                 info->cover_height <= 256 && info->cover_stride >= info->cover_width * 4) {
            const auto width = info->cover_width, height = info->cover_height;
            std::vector<uint8_t> pixels(size_t(width) * height * 4);
            for (uint32_t y = 0; y < height; ++y) {
                for (uint32_t x = 0; x < width; ++x) {
                    const auto* in = info->cover_rgba + size_t(y) * info->cover_stride + x * 4;
                    auto* out = pixels.data() + (size_t(y) * width + x) * 4;
                    out[0] = in[2];
                    out[1] = in[1];
                    out[2] = in[0];
                    out[3] = in[3];
                }
            }
            media.cover.bitmap = cybercanvas::ImageData::fromPremultipliedBgra(width, height, std::move(pixels));
        }
        const int64_t position = int64_t(mediaPosition(media, GetTickCount64()));
        const bool changed = media.source != self.media.source || media.title != self.media.title ||
                             media.artist != self.media.artist || media.album != self.media.album ||
                             media.controls != self.media.controls || media.status != self.media.status ||
                             media.cover.bitmap != self.media.cover.bitmap || media.duration != self.media.duration ||
                             media.commandResult != self.media.commandResult ||
                             (self.settings.at("show_progress").integer && position != self.shownPosition);
        self.media = std::move(media);
        self.shownPosition = position;
        playerTimer(self);
        if (changed)
            self.redraw();
    } catch (...) {
    }
}

void CD_CALL inputStatsChanged(void* pointer, const CdInputStats* stats) noexcept {
    if (!pointer || !stats || stats->struct_size < sizeof(CdInputStats))
        return;
    auto& self = *static_cast<Instance*>(pointer);
    if (self.paused)
        return;
    if (self.spec.mechanical) {
        const auto now = GetTickCount64();
        const bool changed = self.counterDrums.retarget(*stats, now, recordMotion(self));
        self.inputStats = *stats;
        if (self.counterDrums.active(now)) {
            if (self.timer.replace(16, tick, &self) != CD_OK) {
                self.counterDrums.finish();
            }
        } else {
            self.timer.stop();
        }
        if (changed) {
            self.redraw();
        }
        return;
    }
    const auto& old = self.inputStats;
    const bool changed = old.day != stats->day || old.status != stats->status ||
                         old.keyboard_today != stats->keyboard_today || old.mouse_today != stats->mouse_today ||
                         (self.settings.at("show_total").integer &&
                          (old.keyboard_total != stats->keyboard_total || old.mouse_total != stats->mouse_total));
    self.inputStats = *stats;
    if (changed)
        self.redraw();
}

void CD_CALL tick(void* pointer, uint64_t) noexcept {
    try {
        auto& self = *static_cast<Instance*>(pointer);
        if (self.paused)
            return;
        const auto now = GetTickCount64();
        const float seconds = self.lastTick ? float(std::min<uint64_t>(250, now - self.lastTick)) * .001f : .033f;
        self.lastTick = now;
        if (self.spec.kind == Kind::InputStats && self.spec.mechanical) {
            if (!recordMotion(self) || !self.counterDrums.active(now)) {
                self.counterDrums.finish();
                self.timer.stop();
            }
            self.redraw();
        } else if (self.spec.kind == Kind::Player) {
            const int64_t position = int64_t(mediaPosition(self.media, now));
            const bool spinning =
                self.media.status == CD_MEDIA_PLAYING && !self.media.source.empty() && recordMotion(self);
            if (spinning) {
                self.media.recordDegrees = std::fmod(self.media.recordDegrees + seconds * 18, 360.0);
                advanceTonearm(self.media, seconds, mediaPosition(self.media, now));
            }
            if (spinning || (self.settings.at("show_progress").integer && position != self.shownPosition)) {
                self.shownPosition = position;
                self.redraw();
            }
            playerTimer(self);
        } else if (self.music()) {
            const auto peakMotion = self.spec.mechanical && self.spec.kind == Kind::Bars
                                        ? Spectrum::PeakMotion::FastBall
                                        : Spectrum::PeakMotion::Default;
            if (self.spectrum.advance(seconds, !self.lastAudio || now - self.lastAudio > 200,
                                      self.settings.at("gain").number, double(self.settings.at("release").integer),
                                      peakMotion))
                self.redraw();
        } else if (self.album) {
            Photo result;
            bool changed = false;
            if (self.album->take(result)) {
                self.photoPending = false;
                self.lastPhoto = now;
                const bool newImage = result.bitmap != self.photo.bitmap;
                if (newImage) {
                    self.previousPhotoMotionSeconds = self.photoMotionSeconds;
                    self.photoMotionSeconds = 0;
                    self.previous = self.photo;
                    self.photo = std::move(result);
                    self.fadeStart = now;
                    self.fade = self.previous.bitmap ? 0.f : 1.f;
                    changed = true;
                } else if (result.index != self.photo.index || result.count != self.photo.count) {
                    self.photo = std::move(result);
                    changed = true;
                }
            }
            if (self.fade < 1) {
                const double duration = self.settings.at("transition").number;
                self.fade = duration <= 0 ? 1 : std::clamp(float((now - self.fadeStart) / (duration * 1000)), 0.f, 1.f);
                if (self.fade >= 1)
                    self.previous = {};
                changed = true;
            }
            if (self.photo.bitmap && !self.slideshowPaused && albumMotion(self) && seconds > 0) {
                self.photoMotionSeconds += seconds;
                changed = true;
            }
            if (!self.photoPending) {
                if (!self.slideshowPaused && self.photo.count > 1 &&
                    now - self.lastPhoto >= uint64_t(self.settings.at("interval").integer) * 1000) {
                    self.requestPhoto(1);
                }
            }
            if (changed)
                self.redraw();
            albumTimer(self);
        } else {
            const auto date = localDate();
            if (date != self.date) {
                self.date = date;
                self.redraw();
            }
        }
    } catch (...) {
    }
}

void stop(Instance& self) {
    if (self.album) {
        self.album->pause(true);
    }
    if (self.audioToken)
        self.host.unsubscribe(self.context, self.audioToken);
    self.timer.stop();
    self.counterDrums.reset();
    self.audioToken = 0;
    if (self.mediaToken)
        self.host.unsubscribe(self.context, self.mediaToken);
    self.mediaToken = 0;
    if (self.inputStatsToken)
        self.host.unsubscribe(self.context, self.inputStatsToken);
    self.inputStatsToken = 0;
}

CdResult start(Instance& self) {
    if (self.album) {
        self.album->pause(false);
        return albumTimer(self);
    }
    if (self.spec.kind == Kind::InputStats)
        return self.inputHost.subscribe_input_stats(self.context, inputStatsChanged, &self, &self.inputStatsToken);
    self.lastTick = GetTickCount64();
    const auto result = self.spec.kind == Kind::Player
                            ? playerTimer(self)
                            : self.timer.replace(self.spec.kind == Kind::Calendar ? 1000 : 33, tick, &self);
    if (result != CD_OK)
        return result;
    if (self.spec.kind == Kind::Player) {
        const auto subscribed = self.mediaHost.subscribe_media(self.context, self.settings.at("player").text.c_str(),
                                                               mediaChanged, &self, &self.mediaToken);
        if (subscribed != CD_OK) {
            stop(self);
            return subscribed;
        }
    }
    if (self.music()) {
        const CdAudioOptions options{sizeof(CdAudioOptions), 63, 30, 0, nullptr};
        const auto subscribed =
            self.audioService.subscribe
                ? self.audioService.subscribe(self.context, &options, audio, &self, &self.audioToken)
                : self.host.subscribe_audio(self.context, 63, audio, &self, &self.audioToken);
        if (subscribed != CD_OK) {
            stop(self);
            return subscribed;
        }
    }
    return CD_OK;
}

void CD_CALL destroy(void* pointer) noexcept {
    if (auto self = std::unique_ptr<Instance>(static_cast<Instance*>(pointer))) {
        stop(*self);
    }
}

void CD_CALL layout(void* pointer, const CdLayout* layout) noexcept {
    if (!pointer || !layout || layout->struct_size < sizeof(CdLayout))
        return;
    auto& self = *static_cast<Instance*>(pointer);
    self.safe = layout->safe_content;
    if (self.safe.width <= 0 || self.safe.height <= 0)
        self.safe = {0, 0, layout->logical_width, layout->logical_height};
    self.canvas.cancelInput();
    self.redraw();
}

void CD_CALL pause(void* pointer, uint32_t paused) noexcept {
    try {
        if (!pointer)
            return;
        auto& self = *static_cast<Instance*>(pointer);
        if (self.paused == (paused != 0))
            return;
        self.paused = paused != 0;
        self.canvas.pause(self.paused);
        self.canvas.cancelInput();
        if (self.paused) {
            stop(self);
            return;
        }
        self.lastAudio = 0;
        if (self.music())
            self.spectrum = {};
        self.lastPhoto = GetTickCount64();
        self.fadeStart = self.lastPhoto;
        self.fade = 1;
        self.previous = {};
        self.date = localDate();
        if (start(self) != CD_OK) {
            self.paused = true;
            stop(self);
        }
        self.redraw();
    } catch (...) {
    }
}

void CD_CALL input(void* pointer, const CdInput* event) noexcept {
    try {
        if (!pointer || !event || event->struct_size < sizeof(CdInput))
            return;
        auto& self = *static_cast<Instance*>(pointer);
        if (self.paused)
            return;
        self.canvas.syncInput(self.spec, self.media, self.safe, self.photo, {self.slideshowPaused, self.photoPending});
        self.canvas.input(*event);
        if (self.spec.kind == Kind::Player) {
            const auto layout = arrangePlayer(self.spec, float(self.safe.width), float(self.safe.height));
            const int hit = playerHit(layout, event->x - self.safe.x, event->y - self.safe.y, self.media.controls);
            if (event->kind == CD_INPUT_PRESS && (event->buttons & CD_BUTTON_LEFT)) {
                self.pressedPlayer = self.media.commandResult == CD_ERROR_BUSY || hit != 4 ? 0 : hit;
                if (self.pressedPlayer)
                    self.redraw();
            } else if (event->kind == CD_INPUT_LEAVE) {
                if (self.pressedPlayer)
                    self.redraw();
                self.pressedPlayer = 0;
            } else if (event->kind == CD_INPUT_RELEASE) {
                const int pressed = self.pressedPlayer;
                self.pressedPlayer = 0;
                if (pressed && hit == pressed) {
                    constexpr uint32_t commands[]{0, CD_MEDIA_PREVIOUS, CD_MEDIA_TOGGLE, CD_MEDIA_NEXT, CD_MEDIA_SEEK};
                    const double value =
                        hit == 4 ? std::clamp((event->x - self.safe.x - layout.timeline.x) / layout.timeline.width, 0.0,
                                              1.0) *
                                       self.media.duration
                                 : 0;
                    const auto result =
                        self.mediaHost.media_command(self.context, self.mediaToken, commands[hit], value);
                    if (result != CD_OK)
                        self.media.commandResult = result;
                }
                if (pressed)
                    self.redraw();
            }
        } else if (self.spec.kind == Kind::Calendar) {
            if (event->kind == CD_INPUT_DOUBLE_CLICK) {
                self.monthOffset = 0;
                self.redraw();
            }
        } else if (self.album && event->kind == CD_INPUT_DOUBLE_CLICK && !self.photoPending) {
            if (!self.spec.mechanical && self.photo.count > 1) {
                self.requestPhoto(1);
            } else if (self.spec.mechanical && (event->buttons & CD_BUTTON_LEFT) && self.photo.bitmap &&
                       !self.photo.filepath.empty() && self.imageViewer.open) {
                const auto geometry = arrangeAlbum(self.safe.width, self.safe.height);
                const double x = event->x - self.safe.x, y = event->y - self.safe.y;
                const auto contains = [&](cybercanvas::Rect r) {
                    return x >= r.x && y >= r.y && x < r.x + r.width && y < r.y + r.height;
                };
                if (contains({0, 0, self.safe.width, self.safe.height}) && !contains(geometry.dock) &&
                    (!self.settings.at("counter").integer || !contains(geometry.counter))) {
                    const auto utf8 = std::filesystem::path(self.photo.filepath).u8string();
                    const auto result = self.imageViewer.open(self.context, reinterpret_cast<const char*>(utf8.data()),
                                                              uint32_t(utf8.size()));
                    if (result != CD_OK && self.host.log) {
                        constexpr char message[] = "Cannot open the current album photo in CyberImage";
                        self.host.log(self.context, 2, message, sizeof(message) - 1);
                    }
                }
            }
        }
    } catch (...) {
    }
}

CdResult CD_CALL get(void* pointer, const char* id, CdSettingValue* value, uint32_t size) noexcept {
    return pointer ? static_cast<Instance*>(pointer)->settings.get(id, value, size) : CD_ERROR_ARGUMENT;
}

CdResult CD_CALL apply(void* pointer, const CdSettingValue* values, uint32_t count) noexcept {
    try {
        if (!pointer)
            return CD_ERROR_ARGUMENT;
        auto& self = *static_cast<Instance*>(pointer);
        auto changed = self.settings;
        const auto result = changed.apply(values, count);
        if (result != CD_OK)
            return result;
        const bool folderChanged =
            self.album && (changed.at("directory").text != self.settings.at("directory").text ||
                           changed.at("recursive").integer != self.settings.at("recursive").integer);
        const bool playerChanged =
            self.spec.kind == Kind::Player && changed.at("player").text != self.settings.at("player").text;
        uint64_t replacement = 0;
        if (playerChanged && !self.paused) {
            const auto subscribed = self.mediaHost.subscribe_media(self.context, changed.at("player").text.c_str(),
                                                                   mediaChanged, &self, &replacement);
            if (subscribed != CD_OK)
                return subscribed;
        }
        // Validate and allocate the complete settings value before committing it.
        if (folderChanged)
            self.album->request(changed.at("directory").text, changed.at("recursive").integer != 0, 0, true);
        self.settings = std::move(changed);
        if (self.spec.kind == Kind::InputStats && self.spec.mechanical && !recordMotion(self)) {
            self.counterDrums.finish();
            self.timer.stop();
        }
        if (self.spec.kind == Kind::Player) {
            playerTimer(self);
        }
        self.canvas.cancelInput();
        if (playerChanged) {
            if (self.mediaToken)
                self.host.unsubscribe(self.context, self.mediaToken);
            self.mediaToken = replacement;
            self.media = {};
            self.pressedPlayer = 0;
            self.shownPosition = -1;
            playerTimer(self);
        }
        if (folderChanged) {
            self.photoPending = true;
            self.photo = {};
            self.previous = {};
            self.fade = 1;
            self.photoMotionSeconds = self.previousPhotoMotionSeconds = 0;
        }
        albumTimer(self);
        self.redraw();
        return CD_OK;
    } catch (...) {
        return CD_ERROR_INTERNAL;
    }
}

CdResult CD_CALL save(void* pointer, uint8_t* bytes, uint64_t capacity, uint64_t* size) noexcept {
    if (!pointer || !size)
        return CD_ERROR_ARGUMENT;
    *size = 8;
    if (!bytes && !capacity)
        return CD_OK;
    if (!bytes || capacity < 8)
        return CD_ERROR_BUFFER;
    const auto& self = *static_cast<Instance*>(pointer);
    bytes[0] = 'W';
    bytes[1] = 'G';
    bytes[2] = 1;
    bytes[3] = uint8_t(self.spec.kind);
    const int32_t value = self.album ? int32_t(self.slideshowPaused) : self.monthOffset;
    std::memcpy(bytes + 4, &value, 4);
    return CD_OK;
}

CdResult CD_CALL restore(void* pointer, const uint8_t* bytes, uint64_t size) noexcept {
    if (!pointer || (size && !bytes))
        return CD_ERROR_ARGUMENT;
    auto& self = *static_cast<Instance*>(pointer);
    int32_t value = 0;
    if (size) {
        if (size != 8 || bytes[0] != 'W' || bytes[1] != 'G' || bytes[2] != 1 || bytes[3] != uint8_t(self.spec.kind))
            return CD_ERROR_ARGUMENT;
        std::memcpy(&value, bytes + 4, 4);
        if (value < -1200 || value > 1200 || (self.album && value != 0 && value != 1))
            return CD_ERROR_ARGUMENT;
    }
    if (self.album) {
        self.slideshowPaused = value != 0;
        self.lastPhoto = GetTickCount64();
        albumTimer(self);
    } else {
        self.monthOffset = value;
    }
    albumTimer(self);
    self.redraw();
    return CD_OK;
}

CdResult CD_CALL render(void* pointer, const CdFrame* frame) noexcept {
    try {
        if (!pointer || !frame || frame->struct_size < sizeof(CdFrame) || !frame->rgba || !frame->width ||
            !frame->height || frame->width > 4096 || frame->height > 4096 || frame->stride < frame->width * 4 ||
            frame->capacity < uint64_t(frame->stride) * frame->height || !std::isfinite(frame->device_pixel_ratio) ||
            frame->device_pixel_ratio <= 0 || frame->device_pixel_ratio > 8)
            return CD_ERROR_ARGUMENT;
        auto& self = *static_cast<Instance*>(pointer);
        if (self.album) {
            const auto safe =
                self.safe.width > 0 && self.safe.height > 0
                    ? self.safe
                    : CdRect{0, 0, frame->width / frame->device_pixel_ratio, frame->height / frame->device_pixel_ratio};
            const auto photoRegion = self.canvas.photoRegion(safe);
            if (photoRegion.width > 0 && photoRegion.height > 0) {
                self.album->pixelEdge(uint32_t(
                    std::clamp(std::ceil(std::max(photoRegion.width, photoRegion.height) * frame->device_pixel_ratio),
                               1.0, 2048.0)));
            }
        }
        return self.canvas.render(self.spec, self.settings, self.spectrum, self.photo, self.previous, self.fade,
                                  self.date, self.monthOffset, *frame, self.safe, self.media, self.pressedPlayer,
                                  self.inputStats, &self.counterDrums,
                                  {self.slideshowPaused, self.photoPending, self.photoMotionSeconds,
                                   self.previousPhotoMotionSeconds, self.album ? albumMotion(self) : false})
                   ? CD_OK
                   : CD_ERROR_INTERNAL;
    } catch (...) {
        return CD_ERROR_INTERNAL;
    }
}
}  // namespace

CdResult create(Spec spec, const CdCreateContext* context, CdInstanceApi* out, uint32_t size) {
    try {
        if (!context || context->struct_size < sizeof(CdCreateContext) ||
            context->abi_version != CD_PLUGIN_ABI_VERSION || !context->host ||
            context->host->struct_size < sizeof(CdHostApi) || context->host->abi_version != CD_PLUGIN_ABI_VERSION ||
            !context->host->request_redraw || !context->host->subscribe_timer || !context->host->unsubscribe || !out ||
            size < sizeof(CdInstanceApi) || (isMusic(spec.kind) && !context->host->subscribe_audio))
            return CD_ERROR_ARGUMENT;
        if (spec.kind == Kind::Player && context->host->struct_size < sizeof(CdMediaHostApi))
            return CD_ERROR_UNSUPPORTED;
        if (spec.kind == Kind::InputStats && context->host->struct_size < sizeof(CdInputStatsHostApi))
            return CD_ERROR_UNSUPPORTED;
        auto self = std::make_unique<Instance>(spec);
        self->host = *context->host;
        self->context = context->host_context;
        if (self->album && spec.mechanical) {
            cdsdk::Services(context->host, self->context).query(CD_SERVICE_IMAGE_VIEWER, self->imageViewer);
        }
        self->canvas.bind(*context, [ptr = self.get()](uint32_t id) {
            if (ptr->paused) {
                return;
            }
            if (ptr->spec.kind == Kind::Player && id >= 1 && id <= 3 && ptr->media.commandResult != CD_ERROR_BUSY) {
                constexpr uint32_t commands[]{CD_MEDIA_PREVIOUS, CD_MEDIA_TOGGLE, CD_MEDIA_NEXT};
                const auto result = ptr->mediaHost.media_command(ptr->context, ptr->mediaToken, commands[id - 1], 0);
                if (result != CD_OK) {
                    ptr->media.commandResult = result;
                }
            } else if (ptr->album && ptr->spec.mechanical && id >= 1 && id <= 3 && ptr->photo.bitmap &&
                       (id == 2 || ptr->photo.count > 1)) {
                if (id == 2) {
                    ptr->slideshowPaused = !ptr->slideshowPaused;
                    ptr->lastPhoto = GetTickCount64();
                } else if (!ptr->photoPending) {
                    ptr->requestPhoto(id == 1 ? -1 : 1);
                }
            } else if (ptr->spec.kind == Kind::Calendar) {
                ptr->monthOffset = std::clamp(ptr->monthOffset + (id == 1 ? -1 : 1), -1200, 1200);
            }
            if (ptr->album) {
                albumTimer(*ptr);
            }
            ptr->redraw();
        });
        self->timer.bind(*context);
        if (self->album) {
            self->album->bind(*context, [ptr = self.get()] {
                tick(ptr, 0);
            });
        }
        if (self->music() && context->host->struct_size >= sizeof(CdServiceHostApi)) {
            const auto result = cdsdk::Services(context->host, self->context).query(CD_SERVICE_FFT, self->audioService);
            if (result != CD_OK)
                return result;
        }
        if (spec.kind == Kind::InputStats) {
            self->inputHost = *reinterpret_cast<const CdInputStatsHostApi*>(context->host);
            if (context->host->struct_size >= sizeof(CdServiceHostApi)) {
                CdInputServiceApi service{};
                const auto result = cdsdk::Services(context->host, self->context).query(CD_SERVICE_INPUT, service);
                if (result != CD_OK)
                    return result;
                self->inputHost.subscribe_input_stats = service.subscribe;
            }
            if (!self->inputHost.subscribe_input_stats)
                return CD_ERROR_UNSUPPORTED;
        }
        if (spec.kind == Kind::Player) {
            self->mediaHost = *reinterpret_cast<const CdMediaHostApi*>(context->host);
            if (context->host->struct_size >= sizeof(CdServiceHostApi)) {
                CdMediaServiceApi service{};
                const auto result = cdsdk::Services(context->host, self->context).query(CD_SERVICE_MEDIA, service);
                if (result != CD_OK)
                    return result;
                self->mediaHost.subscribe_media = service.subscribe;
                self->mediaHost.media_command = service.command;
            }
            if (!self->mediaHost.subscribe_media || !self->mediaHost.media_command)
                return CD_ERROR_UNSUPPORTED;
        }
        if (self->album) {
            self->lastPhoto = GetTickCount64();
            self->requestPhoto(0, true);
        }
        const auto result = start(*self);
        if (result != CD_OK) {
            stop(*self);
            return result;
        }
        *out = {sizeof(CdInstanceApi),
                CD_PLUGIN_ABI_VERSION,
                self.get(),
                destroy,
                render,
                input,
                layout,
                pause,
                save,
                restore,
                get,
                apply};
        self.release();
        return CD_OK;
    } catch (...) {
        return CD_ERROR_INTERNAL;
    }
}
}  // namespace alice_widget
