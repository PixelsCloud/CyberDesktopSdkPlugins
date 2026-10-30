#include <CyberCanvas/Image.h>
#include <Windows.h>
#include <sdk/services/ImageViewer.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <thread>

#include "WidgetCatalog.h"
#include "plugins/widget-suite/AlbumArtwork.h"
#include "plugins/widget-suite/PlayerArtwork.h"
#include "plugins/widget-suite/Widget.h"

namespace {
void check(bool value, const char* label) {
    if (!value)
        throw std::runtime_error(label);
}

struct Host {
    struct Timer {
        uint64_t token;
        CdTimerCallback callback;
        void* user;
        uint32_t interval;
    };

    std::vector<Timer> timers;
    CdAudioCallback audio = nullptr;
    void* audioUser = nullptr;
    uint64_t next = 1, audioToken = 0;
    int redraws = 0;
    CdMediaCallback media = nullptr;
    void* mediaUser = nullptr;
    uint64_t mediaToken = 0;
    uint32_t lastCommand = 0;
    int commands = 0;
    std::string player;
    std::string openedPhoto;
    int imageOpens = 0;
    CdInputStatsCallback inputStats = nullptr;
    void* inputStatsUser = nullptr;
    uint64_t inputStatsToken = 0;

    static void CD_CALL redraw(void* p) {
        ++static_cast<Host*>(p)->redraws;
    }

    static CdResult CD_CALL subscribe(void* p, uint32_t bands, CdAudioCallback cb, void* user, uint64_t* token) {
        auto& self = *static_cast<Host*>(p);
        if (bands != 63)
            return CD_ERROR_ARGUMENT;
        self.audio = cb;
        self.audioUser = user;
        *token = self.audioToken = self.next++;
        return CD_OK;
    }

    static CdResult CD_CALL timer(void* p, uint32_t interval, CdTimerCallback cb, void* user, uint64_t* token) {
        auto& self = *static_cast<Host*>(p);
        *token = self.next++;
        self.timers.push_back({*token, cb, user, interval});
        return CD_OK;
    }

    static void CD_CALL stop(void* p, uint64_t token) {
        auto& self = *static_cast<Host*>(p);
        std::erase_if(self.timers, [&](const Timer& t) {
            return t.token == token;
        });
        if (self.audioToken == token) {
            self.audio = nullptr;
            self.audioUser = nullptr;
            self.audioToken = 0;
        }
        if (self.mediaToken == token) {
            self.mediaToken = 0;
            self.media = nullptr;
        }
        if (self.inputStatsToken == token) {
            self.inputStatsToken = 0;
            self.inputStats = nullptr;
        }
    }

    static CdResult CD_CALL subscribeMedia(void* p, const char* player, CdMediaCallback callback, void* user,
                                           uint64_t* token) {
        auto& self = *static_cast<Host*>(p);
        self.media = callback;
        self.mediaUser = user;
        self.player = player;
        *token = self.mediaToken = self.next++;
        return CD_OK;
    }

    static CdResult CD_CALL command(void* p, uint64_t token, uint32_t command, double) {
        auto& self = *static_cast<Host*>(p);
        if (token != self.mediaToken || !self.media)
            return CD_ERROR_ARGUMENT;
        ++self.commands;
        self.lastCommand = command;
        return CD_OK;
    }

    static CdResult CD_CALL subscribeInputStats(void* p, CdInputStatsCallback callback, void* user, uint64_t* token) {
        auto& self = *static_cast<Host*>(p);
        self.inputStats = callback;
        self.inputStatsUser = user;
        *token = self.inputStatsToken = self.next++;
        return CD_OK;
    }

    static CdResult CD_CALL openImage(void* pointer, const char* path, uint32_t length) {
        auto& self = *static_cast<Host*>(pointer);
        self.openedPhoto.assign(path, length);
        ++self.imageOpens;
        return CD_OK;
    }

    static CdResult CD_CALL imageService(void*, const char* id, uint32_t version, void* output, uint32_t capacity) {
        if (std::string_view(id) != CD_SERVICE_IMAGE_VIEWER) {
            return CD_ERROR_UNSUPPORTED;
        }
        if (version != 1 || capacity < sizeof(CdImageViewerApi)) {
            return CD_ERROR_BUFFER;
        }
        *static_cast<CdImageViewerApi*>(output) = {sizeof(CdImageViewerApi), 1, openImage};
        return CD_OK;
    }

    CdInputStatsHostApi extended{
        {{sizeof(CdInputStatsHostApi), CD_PLUGIN_ABI_VERSION, redraw, nullptr, nullptr, subscribe, stop, timer},
         subscribeMedia,
         command},
        subscribeInputStats};
    CdMediaHostApi& api = extended.media;

    void tick() {
        const auto batch = timers;
        for (const auto& t : batch)
            t.callback(t.user, GetTickCount64());
    }
};

std::string playerMaterialDirectory(const std::string& id = "com.cyberdesktop.music.player.mechanical.2x4") {
    wchar_t executable[32768]{};
    const auto length = GetModuleFileNameW(nullptr, executable, DWORD(std::size(executable)));
    check(length && length < std::size(executable), "Test executable path");
    const auto path = std::filesystem::path(executable).parent_path() / "plugins" / id;
    const auto utf8 = path.u8string();
    return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
}

void matrixSurfaceRendering() {
    using namespace widgetplugin;
    Canvas reused;
    Spectrum signal;
    for (size_t i = 0; i < Spectrum::count; ++i) {
        signal.level[i] = i % 2 ? .15f : .85f;
        signal.peak[i] = std::min(1.f, signal.level[i] + .22f);
    }
    for (auto kind : {Kind::RoundMatrix, Kind::SquareMatrix}) {
        for (double dpi : {1., 1.25, 1.5, 2., 1.}) {
            for (bool background : {false, true}) {
                for (int rows : {6, 9, 24}) {
                    const int columns = rows == 6 ? 4 : rows == 9 ? 6 : 8;
                    const Spec spec{"matrix-surface", kind, 1, columns};
                    const uint32_t width = uint32_t((columns * 72 + 16) * dpi), height = uint32_t(124 * dpi);
                    std::vector<uint8_t> pixels(size_t(width) * height * 4), reference(pixels.size());
                    CdFrame frame{sizeof(frame), width, height, width * 4, pixels.size(), pixels.data(), dpi};
                    auto expected = frame;
                    expected.rgba = reference.data();
                    auto settings = defaults(kind);
                    const CdSettingValue values[]{
                        {sizeof(CdSettingValue), CD_SETTING_INTEGER, "matrix_rows", rows},
                        {sizeof(CdSettingValue), CD_SETTING_TOGGLE, "show_background", background},
                        {sizeof(CdSettingValue), CD_SETTING_NUMBER, "glow_strength", 0, rows == 6 ? 0. : 100.},
                        {sizeof(CdSettingValue), CD_SETTING_COLOR, "background",
                         background ? 0xEAE4D7FFLL : 0x192630FFLL},
                        {sizeof(CdSettingValue), CD_SETTING_COLOR, "color", background ? 0x228C5AFFLL : 0x00D8B8FFLL}};
                    check(settings.apply(values, 5) == CD_OK, "Matrix surface settings");
                    const CdRect safe{8, 8, double(columns * 72), 108};
                    Canvas fresh;
                    check(fresh.render(spec, settings, signal, {}, {}, 1, {}, 0, expected, safe),
                          "Fresh matrix surface");
                    check(reused.render(spec, settings, signal, {}, {}, 1, {}, 0, frame, safe) && pixels == reference,
                          "Matrix cache matches a fresh canvas after DPI, density, shape and palette changes");
                    check(pixels[3] == 0 && pixels[pixels.size() - 1] == 0, "Matrix cache respects host padding");
                    check(reused.render(spec, settings, {}, {}, {}, 1, {}, 0, frame, safe) && pixels != reference,
                          "Silence removes previous matrix lights");
                    check(reused.render(spec, settings, signal, {}, {}, 1, {}, 0, frame, safe) && pixels == reference,
                          "Retained matrix sprites survive repeated composition");
                }
            }
        }
    }
}

void albumMaskRendering() {
    using namespace widgetplugin;
    const auto solid = [](uint32_t width, uint32_t height, bool red) {
        Photo photo;
        std::vector<uint8_t> pixels(size_t(width) * height * 4);
        for (size_t i = 0; i < pixels.size(); i += 4) {
            pixels[i] = red ? 0 : 255;
            pixels[i + 2] = red ? 255 : 0;
            pixels[i + 3] = 255;
        }
        photo.bitmap = cybercanvas::ImageData::fromPremultipliedBgra(width, height, std::move(pixels));
        return photo;
    };
    const auto previous = solid(120, 60, false), current = solid(30, 60, true);
    Canvas canvas;
    const Spec spec{"album-mask", Kind::Album, 2, 4};
    for (double dpi : {1., 1.5, 2.}) {
        const uint32_t width = uint32_t(200 * dpi), height = uint32_t(120 * dpi);
        std::vector<uint8_t> pixels(size_t(width) * height * 4);
        CdFrame frame{sizeof(frame), width, height, width * 4, pixels.size(), pixels.data(), dpi};
        const auto channel = [&](int x, int y, int c) {
            return int(pixels[(size_t(y * dpi) * width + size_t(x * dpi)) * 4 + c]);
        };
        for (const char* fit : {"cover", "contain"}) {
            auto settings = defaults(Kind::Album);
            const CdSettingValue values[]{{sizeof(CdSettingValue), CD_SETTING_CHOICE, "fit", 0, 0, fit},
                                          {sizeof(CdSettingValue), CD_SETTING_TOGGLE, "counter", 0},
                                          {sizeof(CdSettingValue), CD_SETTING_INTEGER, "background_corner_radius", 14}};
            check(settings.apply(values, 3) == CD_OK, "Album mask settings");
            for (float fade : {0.f, .25f, .5f, 1.f}) {
                check(canvas.render(spec, settings, {}, current, previous, fade, {}, 0, frame, {8, 8, 184, 104}),
                      "Album crossfade render");
                check(std::abs(channel(100, 50, 0) - int(255 * fade)) <= 1 &&
                          std::abs(channel(100, 50, 2) - int(255 * (1 - fade))) <= 1 && channel(100, 50, 3) == 255,
                      "Album crossfade must blend previous and current photos without dimming the group");
                check(channel(0, 0, 3) == 0 && channel(9, 9, 3) == 0 && channel(199, 119, 3) == 0,
                      "Album image must stay inside rounded mask and host padding");
                if (fade == .5f) {
                    check(fit == std::string("cover") ? channel(20, 50, 0) >= 127 : channel(20, 50, 0) == 0,
                          "Album cover and contain preserve image fitting under mask");
                }
            }
            check(canvas.render(spec, settings, {}, {}, {}, 1, {}, 0, frame, {8, 8, 184, 104}),
                  "Empty album after crossfade");
            check(channel(0, 0, 3) == 0 && channel(9, 9, 3) == 0, "Empty album retains rounded clipping");
        }
    }
}

void vinylRendering() {
    using namespace widgetplugin;
    Media media;
    std::vector<uint8_t> imagePixels(64 * 48 * 4);
    for (size_t y = 0; y < 48; ++y) {
        for (size_t x = 0; x < 64; ++x) {
            const size_t offset = (y * 64 + x) * 4;
            imagePixels[offset] = uint8_t(x * 4);
            imagePixels[offset + 1] = uint8_t(y * 5);
            imagePixels[offset + 2] = 180;
            imagePixels[offset + 3] = 255;
        }
    }
    const auto image = cybercanvas::ImageData::fromPremultipliedBgra(64, 48, std::move(imagePixels));
    auto settings = defaults(Kind::Player);
    const CdSettingValue transparent[]{{sizeof(CdSettingValue), CD_SETTING_TOGGLE, "show_background", 0},
                                       {sizeof(CdSettingValue), CD_SETTING_TOGGLE, "show_progress", 0}};
    check(settings.apply(transparent, 2) == CD_OK, "Transparent vinyl settings");
    Canvas canvas;
    for (const auto spec :
         {Spec{"vinyl", Kind::Player, 1, 4}, Spec{"vinyl", Kind::Player, 2, 2}, Spec{"vinyl", Kind::Player, 2, 4}}) {
        for (const double dpi : {1., 1.5, 2.}) {
            for (const int cellHeight : {72, 100, 150}) {
                const float w = float(spec.columns * 72), h = float(spec.rows * cellHeight);
                const auto layout = arrangePlayer(spec, w, h);
                const auto& cover = layout.cover;
                const uint32_t width = uint32_t(w * dpi), height = uint32_t(h * dpi);
                std::vector<uint8_t> pixels(size_t(width) * height * 4);
                CdFrame frame{sizeof(frame), width, height, width * 4, pixels.size(), pixels.data(), dpi};
                for (const bool artwork : {false, true}) {
                    media.cover.bitmap = artwork ? image : nullptr;
                    media.recordDegrees = 0;
                    check(canvas.render(spec, settings, {}, {}, {}, 1, {}, 0, frame, {}, media), "Vinyl first frame");
                    const auto first = pixels;
                    const size_t corner = (size_t((cover.y + 1) * dpi) * width + size_t((cover.x + 1) * dpi)) * 4;
                    check(first[corner + 3] == 0, "Vinyl must have transparent corners instead of a square cover");
                    media.recordDegrees = 90;
                    check(canvas.render(spec, settings, {}, {}, {}, 1, {}, 0, frame, {}, media), "Vinyl rotated frame");
                    size_t changed = 0;
                    for (uint32_t y = 0; y < height; ++y) {
                        for (uint32_t x = 0; x < width; ++x) {
                            const size_t offset = (size_t(y) * width + x) * 4;
                            if (std::equal(first.begin() + offset, first.begin() + offset + 4,
                                           pixels.begin() + offset)) {
                                continue;
                            }
                            ++changed;
                            check(x / dpi >= cover.x - 1 && x / dpi <= cover.x + cover.width + 1 &&
                                      y / dpi >= cover.y - 1 && y / dpi <= cover.y + cover.height + 1,
                                  "Record rotation changed controls or text outside the cover");
                        }
                    }
                    check(changed > 20, "Artwork and placeholder labels must visibly rotate at every size and DPI");
                }
            }
        }
    }
}

void turntableMotion() {
    using namespace widgetplugin;
    double previousRadius = 1000;
    for (int step = 0; step <= 100; ++step) {
        const auto pose = tonearmPose(step / 100.);
        check(std::abs(pose.rotation) <= 1.5, "Original PNG arm must only rotate slightly");
        const double radius = std::hypot(pose.contact.x - 279, pose.contact.y - 300);
        check(radius > 201 * .5 + 10 && radius < 201 - 10, "Stylus stays on vinyl, clear of cover and rim");
        check(radius <= previousRadius, "Song progress moves the stylus inward");
        check(std::abs(std::hypot(pose.contact.x - pose.pivot.x, pose.contact.y - pose.pivot.y) - pose.length) < 1e-8,
              "Tonearm must rotate rigidly around its bearing");
        previousRadius = radius;
    }
    Media media;
    media.source = L"test-player";
    media.status = CD_MEDIA_PLAYING;
    media.duration = 180;
    media.tonearmProgress = .2;
    for (int step = 0; step < 20; ++step) {
        const double before = media.tonearmProgress;
        advanceTonearm(media, .1, 170);
        check(media.tonearmProgress > before && media.tonearmProgress - before <= .0035 + 1e-9,
              "Seeking glides the arm without snapping");
    }
    media.status = CD_MEDIA_PAUSED;
    const auto held = media.tonearmProgress, seconds = media.tonearmSeconds;
    advanceTonearm(media, 10, 180);
    check(media.tonearmProgress == held && media.tonearmSeconds == seconds, "Paused tonearm holds its exact position");
    media.status = CD_MEDIA_PLAYING;
    media.duration = 0;
    for (int step = 0; step < 2500; ++step) {
        const double before = media.tonearmProgress;
        advanceTonearm(media, .1, 0);
        check(std::isfinite(media.tonearmProgress) && media.tonearmProgress >= 0 && media.tonearmProgress <= 1 &&
                  std::abs(media.tonearmProgress - before) <= .0035 + 1e-9,
              "Duration-free streams sweep continuously without wrapping or leaving the record");
    }

    for (const auto spec :
         {Spec{"mechanical-cover", Kind::Player, 1, 4, true}, Spec{"mechanical-cover", Kind::Player, 2, 2, true},
          Spec{"mechanical-cover", Kind::Player, 2, 4, true}}) {
        auto settings = defaults(Kind::Player, true);
        const CdSettingValue transparent[]{{sizeof(CdSettingValue), CD_SETTING_TOGGLE, "show_background", 0},
                                           {sizeof(CdSettingValue), CD_SETTING_TOGGLE, "show_progress", 0}};
        check(settings.apply(transparent, 2) == CD_OK, "Mechanical cover settings");
        std::vector<uint8_t> cover(64 * 64 * 4);
        for (size_t i = 0; i < cover.size(); i += 4) {
            cover[i] = 10;
            cover[i + 1] = 210;
            cover[i + 2] = 20;
            cover[i + 3] = 255;
        }
        media.cover.bitmap = cybercanvas::ImageData::fromPremultipliedBgra(64, 64, std::move(cover));
        media.tonearmProgress = .5;
        Host host;
        const auto pluginId = "com.cyberdesktop.music.player.mechanical." + std::to_string(spec.rows) + "x" +
                              std::to_string(spec.columns);
        const auto directory = playerMaterialDirectory(pluginId);
        const CdCreateContext context{sizeof(context),
                                      CD_PLUGIN_ABI_VERSION,
                                      pluginId.c_str(),
                                      "material-test",
                                      directory.c_str(),
                                      &host.api.base,
                                      &host};
        Canvas canvas;
        canvas.bind(context, [](uint32_t) {
        });
        for (int cell : {80, 96, 112, 128, 160}) {
            for (double dpi : {1., 1.5, 2.}) {
                const int w = cell * spec.columns - 6, h = cell * spec.rows * 5 / 4 - 6;
                const auto layout = arrangePlayer(spec, float(w), float(h));
                const auto arm = playerTonearmStage(layout);
                const double vinylRadius = layout.cover.width * .5;
                for (int step = 0; step <= 20; ++step) {
                    const auto pose = tonearmPose(step / 20.);
                    const auto point = arm.map({pose.contact.x, pose.contact.y, 0, 0});
                    const double radius =
                        std::hypot(point.x - layout.cover.x - vinylRadius, point.y - layout.cover.y - vinylRadius);
                    check(radius > vinylRadius * .5 + 2 && radius < vinylRadius - 2,
                          "Each authored casing keeps the stylus within the black vinyl band");
                }
                if (spec.columns == 2) {
                    check(layout.title.y - (layout.cover.y + layout.cover.height) >= 10 &&
                              layout.buttons[1].y - (layout.title.y + layout.title.height) >= 7,
                          "Compact title retains its approved upper and lower breathing room");
                }
                const auto width = uint32_t(w * dpi), height = uint32_t(h * dpi);
                std::vector<uint8_t> pixels(size_t(width) * height * 4);
                const CdFrame frame{sizeof(frame), width, height, width * 4, pixels.size(), pixels.data(), dpi};
                const auto deadline = GetTickCount64() + 5000;
                for (;;) {
                    check(canvas.render(spec, settings, {}, {}, {}, 1, {}, 0, frame, {}, media), "PNG cover render");
                    host.tick();
                    if (host.timers.empty()) {
                        check(canvas.render(spec, settings, {}, {}, {}, 1, {}, 0, frame, {}, media),
                              "PNG materials settled");
                        break;
                    }
                    check(GetTickCount64() < deadline, "PNG materials must finish loading");
                    Sleep(1);
                }
                const auto& r = layout.cover;
                const double radius = r.width * .5;
                const auto sample = [&](double fraction) {
                    const auto x = size_t((r.x + r.width * .5 + radius * fraction) * dpi);
                    const auto y = size_t((r.y + r.height * .5) * dpi);
                    return (y * width + x) * 4;
                };
                const auto inner = sample(.45), outer = sample(.65);
                check(pixels[inner + 1] > 150 && pixels[inner] < 50 && pixels[inner + 2] < 50,
                      "Cover must extend to nearly half the record radius");
                check(pixels[outer + 3] > 200 && std::abs(int(pixels[outer]) - pixels[outer + 1]) < 35 &&
                          std::abs(int(pixels[outer + 1]) - pixels[outer + 2]) < 35,
                      "Outer half of radius must contain the loaded PNG vinyl, not green cover or transparency");
                for (const auto direction : {cybercanvas::Point{1, 0}, cybercanvas::Point{0, 1},
                                             cybercanvas::Point{-1, 0}, cybercanvas::Point{0, -1}}) {
                    const auto x = size_t((r.x + radius + radius * .98 * direction.x) * dpi);
                    const auto y = size_t((r.y + radius + radius * .98 * direction.y) * dpi);
                    check(pixels[(y * width + x) * 4 + 3] > 200,
                          "PNG vinyl fills the platter opening without a transparent edge gap");
                }
                const auto before = pixels;
                media.tonearmProgress = .9;
                check(canvas.render(spec, settings, {}, {}, {}, 1, {}, 0, frame, {}, media) && pixels != before,
                      "PNG tonearm movement must change the rendered frame");
                media.tonearmProgress = .5;
            }
        }
    }
    std::cout << "PNG turntables: 3 casings, half-radius covers, original arms, pause and 45 size/DPI renders PASS\n";
}

void mechanicalPlayerFeedback() {
    using namespace widgetplugin;
    for (const auto spec :
         {Spec{"mechanical-feedback", Kind::Player, 1, 4, true}, Spec{"mechanical-feedback", Kind::Player, 2, 2, true},
          Spec{"mechanical-feedback", Kind::Player, 2, 4, true}}) {
        auto settings = defaults(Kind::Player, true);
        const CdSettingValue transparent[]{{sizeof(CdSettingValue), CD_SETTING_TOGGLE, "show_background", 0},
                                           {sizeof(CdSettingValue), CD_SETTING_TOGGLE, "show_progress", 0}};
        check(settings.apply(transparent, 2) == CD_OK, "Mechanical feedback settings");
        Media media;
        media.source = L"test-player";
        media.status = CD_MEDIA_PAUSED;
        media.controls = CD_MEDIA_CAN_PREVIOUS | CD_MEDIA_CAN_TOGGLE | CD_MEDIA_CAN_NEXT;
        Canvas canvas;
        for (int cell : {80, 96, 112, 128, 160}) {
            for (double dpi : {1., 1.5, 2.}) {
                const int w = cell * spec.columns - 6, h = cell * spec.rows * 5 / 4 - 6;
                const auto layout = arrangePlayer(spec, float(w), float(h));
                const auto width = uint32_t(w * dpi), height = uint32_t(h * dpi);
                std::vector<uint8_t> pixels(size_t(width) * height * 4);
                const CdFrame frame{sizeof(frame), width, height, width * 4, pixels.size(), pixels.data(), dpi};
                media.commandResult = CD_OK;
                check(canvas.render(spec, settings, {}, {}, {}, 1, {}, 0, frame, {}, media),
                      "Mechanical feedback baseline");
                const auto baseline = pixels;
                media.commandResult = CD_ERROR_BUSY;
                check(canvas.render(spec, settings, {}, {}, {}, 1, {}, 0, frame, {}, media),
                      "Mechanical pending command");
                check(pixels == baseline, "Pending media command must not flash all three buttons");
                for (int pressed = 1; pressed <= 3; ++pressed) {
                    check(canvas.render(spec, settings, {}, {}, {}, 1, {}, 0, frame, {}, media, pressed),
                          "Mechanical pressed feedback render");
                    for (int i = 0; i < 3; ++i) {
                        const auto r = layout.buttons[i];
                        size_t changed = 0;
                        for (uint32_t y = uint32_t(std::ceil(r.y * dpi)); y < uint32_t((r.y + r.height) * dpi); ++y) {
                            for (uint32_t x = uint32_t(std::ceil(r.x * dpi)); x < uint32_t((r.x + r.width) * dpi);
                                 ++x) {
                                const size_t offset = (size_t(y) * width + x) * 4;
                                changed += !std::equal(pixels.begin() + offset, pixels.begin() + offset + 4,
                                                       baseline.begin() + offset);
                            }
                        }
                        check(i + 1 == pressed ? changed > 0 : changed == 0,
                              "Press feedback must change only the selected mechanical button");
                    }
                }
            }
        }
    }
}

void compactPlayerTypography() {
    using namespace widgetplugin;
    cybercanvas::Canvas typography;
    for (const auto spec :
         {Spec{"compact-title", Kind::Player, 1, 4, true}, Spec{"compact-title", Kind::Player, 2, 2, true}}) {
        for (int cell : {80, 96, 112, 128, 160}) {
            const int w = cell * spec.columns - 6, h = cell * spec.rows * 5 / 4 - 6;
            const auto layout = arrangePlayer(spec, float(w), float(h));
            for (const auto* title : {L"45.Alan Walker - Faded", L"夜曲", L"gyp 夜曲",
                                      L"A very long song title with descenders gyp and extra text for ellipsis"}) {
                const auto ink = playerTitleInk(typography, title, layout).bounds;
                check(std::abs(ink.y + ink.height / 2 - layout.title.y - layout.title.height / 2) < .05 &&
                          ink.height <= layout.title.height,
                      "Compact title centers visible Latin and Chinese glyphs inside the nameplate");
            }

            auto settings = defaults(Kind::Player, true);
            Media media;
            media.title = L"45.Alan Walker - Faded";
            media.status = CD_MEDIA_PAUSED;
            media.position = 50;
            media.duration = 100;
            const auto width = uint32_t(w), height = uint32_t(h);
            std::vector<uint8_t> pixels(size_t(width) * height * 4);
            const CdFrame frame{sizeof(frame), width, height, width * 4, pixels.size(), pixels.data(), 1};
            Canvas canvas;
            check(canvas.render(spec, settings, {}, {}, {}, 1, {}, 0, frame, {}, media), "Compact progress baseline");
            const auto before = pixels;
            media.position = 100;
            media.duration = 200;
            check(canvas.render(spec, settings, {}, {}, {}, 1, {}, 0, frame, {}, media) && pixels == before,
                  "Compact progress retains the seek position without rendering elapsed or duration labels");
        }
    }
    std::cout << "Compact players: visible glyph centering and progress without time labels PASS\n";
}

}  // namespace

int main() {
    try {
        using namespace widgetplugin;
        vinylRendering();
        turntableMotion();
        mechanicalPlayerFeedback();
        compactPlayerTypography();
        albumMaskRendering();
        matrixSurfaceRendering();
        check(monthDays({2024, 2, 1}) == 29 && monthDays({2025, 2, 1}) == 28, "leap year");
        check(shiftMonth({2026, 1, 1}, -1) == Date{2025, 12, 1}, "month rollover");
        check(monthStart({2026, 10, 1}, true) == 3 && monthStart({2026, 10, 1}, false) == 4, "week start");
        Spectrum spectrum;
        std::array<float, 63> data{};
        data[12] = .7f;
        data[30] = .2f;
        spectrum.feed(data.data(), 63);
        spectrum.advance(.033f, false, 2, 160);
        check(spectrum.level[12] > .8f && spectrum.level[30] > .35f && spectrum.level[0] == 0,
              "attack and frequency contrast");
        const auto peak = spectrum.peak[12];
        for (int i = 0; i < 4; ++i)
            spectrum.advance(.033f, true, 2, 160);
        check(spectrum.peak[12] == peak && spectrum.level[12] < .5f, "peak hold and release");
        for (int i = 0; i < 100; ++i)
            spectrum.advance(.033f, true, 2, 160);
        check(spectrum.level[12] == 0 && spectrum.peak[12] == 0, "silence settles");
        data[0] = std::numeric_limits<float>::quiet_NaN();
        data[1] = std::numeric_limits<float>::infinity();
        spectrum.feed(data.data(), 63);
        check(spectrum.input[0] == 0 && spectrum.input[1] == 0, "nonfinite input");

        Spectrum narrow;
        narrow.level[17] = .9f;
        check(narrow.range(16.6, 18.4) == .9f, "matrix downsampling preserves narrow FFT peaks");

        for (const int height : {72, 108, 150}) {
            int previousColumns = 0;
            for (int rows = 6; rows <= 24; ++rows) {
                const auto grid = arrangeMatrix(576, float(height), rows);
                const auto narrowGrid = arrangeMatrix(288, float(height), rows);
                check(grid.pitchX == grid.pitchY && grid.diameter == narrowGrid.diameter,
                      "matrix rows determine cell size and equal horizontal/vertical spacing");
                check(grid.columns >= previousColumns && grid.columns > narrowGrid.columns,
                      "matrix columns adapt to density and width");
                const float right = 573 - (grid.left + (grid.columns - 1) * grid.pitchX + grid.diameter);
                check(std::abs(right - (grid.left - 1)) < .001f && right >= 3.999f &&
                          grid.left - 5 < grid.pitchX * .5f + .001f,
                      "matrix column rounding leaves symmetric margins under half a cell");
                previousColumns = grid.columns;
            }
            check(previousColumns > 96, "wide dense matrices exceed the old 96-column limit");
        }

        Canvas canvas;
        for (const auto kind : {Kind::RoundMatrix, Kind::SquareMatrix})
            for (const int columns : {4, 6, 8}) {
                const Spec spec{"matrix", kind, 1, columns};
                auto settings = defaults(kind);
                const uint32_t width = uint32_t(columns * 72), height = 108;
                std::vector<uint8_t> pixels(size_t(width) * height * 4);
                CdFrame frame{sizeof(frame), width, height, width * 4, pixels.size(), pixels.data(), 1};
                const auto render = [&](const Spectrum& data) {
                    check(canvas.render(spec, settings, data, {}, {}, 1, {2026, 10, 14}, 0, frame, {}),
                          "matrix render");
                };

                for (const int rows : {6, 9, 24}) {
                    const auto grid = arrangeMatrix(float(width), float(height), rows);
                    check(grid.rows == rows && grid.columns > 1, "matrix density bounds");
                    const float right = width - 3 - (grid.left + (grid.columns - 1) * grid.pitchX + grid.diameter),
                                bottom = height - 4 - (grid.top + (grid.rows - 1) * grid.pitchY + grid.diameter);
                    check(std::abs(right - (grid.left - 1)) < .001f && std::abs(bottom - 4) < .001f && grid.top == 5,
                          "matrix has symmetric horizontal margins and four-pixel vertical margins");
                    check(grid.diameter < grid.pitchX && grid.diameter < grid.pitchY, "matrix dots do not overlap");
                    CdSettingValue density{sizeof(density), CD_SETTING_INTEGER, "matrix_rows", rows, 0, nullptr};
                    check(settings.apply(&density, 1) == CD_OK, "matrix density setting");
                    render({});
                }

                render({});
                const auto silent = pixels;
                render({});
                check(pixels == silent, "cached static matrix is deterministic");
                Spectrum signal;
                for (size_t band = 0; band < Spectrum::count; ++band) {
                    signal.level[band] = band % 2 ? .85f : .2f;
                    signal.peak[band] = std::min(1.f, signal.level[band] + .13f);
                }
                render(signal);
                const auto glowing = pixels;
                check(glowing != silent, "FFT content changes matrix lights");
                render({});
                check(pixels == silent, "silence clears old lights on reused surface");

                CdSettingValue glow{sizeof(glow), CD_SETTING_NUMBER, "glow_strength", 0, 0, nullptr};
                check(settings.apply(&glow, 1) == CD_OK, "matrix glow setting");
                render(signal);
                check(pixels != glowing, "matrix glow changes rendered output");
                glow.number = 101;
                check(settings.apply(&glow, 1) == CD_ERROR_ARGUMENT, "invalid glow rejected");

                CdSettingValue plate{sizeof(plate), CD_SETTING_TOGGLE, "show_background", 0, 0, nullptr};
                check(settings.apply(&plate, 1) == CD_OK, "matrix background disabled");
                render({});
                const size_t gap = (size_t(height / 2) * width + 2) * 4;
                check(pixels[gap + 3] == 0, "matrix plate removal restores transparency");
                plate.integer = 1;
                check(settings.apply(&plate, 1) == CD_OK, "matrix background enabled");
                render({});
                check(pixels == silent, "matrix background cache updates after settings changes");
            }

        // Empty spectrum space must remain transparent, including after clearing a
        // formerly visible plate on the same reused render target.
        for (const auto kind : {Kind::Radial, Kind::Wide, Kind::Bars}) {
            auto settings = defaults(kind);
            Spectrum silent;
            const Spec spec{"alpha", kind, 2, 2};
            std::vector<uint8_t> pixels(200 * 200 * 4);
            CdFrame frame{sizeof(CdFrame), 200, 200, 800, pixels.size(), pixels.data(), 1};
            const auto render = [&] {
                check(canvas.render(spec, settings, silent, {}, {}, 1, {2026, 10, 13}, 0, frame, {}),
                      "transparent spectrum render");
            };
            render();
            const auto transparent = pixels;
            const size_t empty = (100 * 200 + 192) * 4;
            check(pixels[empty + 3] == 0, "no default rectangular music plate");
            CdSettingValue plate{sizeof(plate), CD_SETTING_TOGGLE, "show_background", 1, 0, nullptr};
            check(settings.apply(&plate, 1) == CD_OK, "enable optional music plate");
            render();
            check(pixels[empty + 3] > 0, "optional plate visible");
            plate.integer = 0;
            check(settings.apply(&plate, 1) == CD_OK, "disable optional music plate");
            render();
            check(pixels == transparent, "plate removal clears old pixels");
        }
        for (const auto& item : catalog) {
            auto spec = item.spec;
            spec.mechanical = spec.kind == Kind::Player;
            const auto type = spec.kind;
            Host host;
            const auto pluginId = "com.cyberdesktop.music.player.mechanical." + std::to_string(spec.rows) + "x" +
                                  std::to_string(spec.columns);
            const auto materialDirectory = playerMaterialDirectory(pluginId);
            CdCreateContext context{
                sizeof(context), CD_PLUGIN_ABI_VERSION, "test", "instance", "", &host.api.base, &host};
            const bool pngPlayer = spec.kind == Kind::Player && spec.mechanical;
            if (pngPlayer) {
                context.plugin_id = pluginId.c_str();
                context.plugin_directory = materialDirectory.c_str();
            }
            CdInstanceApi instance{};
            check(create(spec, &context, &instance, sizeof(instance)) == CD_OK, "create instance");

            struct Guard {
                CdInstanceApi& instance;

                ~Guard() {
                    instance.destroy(instance.instance);
                }
            } guard{instance};

            CdSettingValue setting{};
            check(instance.get_setting(instance.instance, "font_family", &setting, sizeof(setting)) == CD_OK &&
                      std::string(setting.text) == (spec.mechanical ? "Microsoft YaHei UI" : "@siyuan-heavy"),
                  "default font");
            CdSettingValue invalid{sizeof(invalid), CD_SETTING_CHOICE, "language", 0, 0, "fr"};
            check(instance.apply_settings(instance.instance, &invalid, 1) == CD_ERROR_ARGUMENT,
                  "atomic invalid setting");
            if (type == Kind::InputStats) {
                check(host.inputStats && host.timers.empty() && !host.audio,
                      "input stats subscribe without timers or FFT");
                CdInputStats stats{sizeof(stats),   CD_INPUT_STATS_ACTIVE, 20261014, 0, 123, 45, 1000, 2000, 0, 0,
                                   GetTickCount64()};
                host.inputStats(host.inputStatsUser, &stats);
                const int redraws = host.redraws;
                ++stats.sampled_at_ms;
                host.inputStats(host.inputStatsUser, &stats);
                check(host.redraws == redraws, "stable input counts do not redraw");
                ++stats.keyboard_today;
                ++stats.keyboard_total;
                host.inputStats(host.inputStatsUser, &stats);
                check(host.redraws == redraws + 1, "input counter changes refresh only this instance");
            }
            if (type == Kind::Player) {
                const CdSettingValue appearance{sizeof(appearance), CD_SETTING_CHOICE, "appearance", 0, 0,
                                                "mechanical"};
                check(instance.apply_settings(instance.instance, &appearance, 1) == CD_ERROR_ARGUMENT,
                      "mechanical media plugin rejects skin switching");
                check(host.media && host.player == "auto" && !host.audio,
                      "player uses shared media subscription without FFT");
                const CdMediaInfo media{sizeof(media),
                                        CD_MEDIA_PLAYING,
                                        CD_MEDIA_CAN_TOGGLE | CD_MEDIA_CAN_PREVIOUS | CD_MEDIA_CAN_NEXT,
                                        0,
                                        0,
                                        0,
                                        "QQMusic.exe",
                                        "Song",
                                        "Artist",
                                        "Album",
                                        nullptr,
                                        1,
                                        GetTickCount64(),
                                        10,
                                        180,
                                        1,
                                        CD_OK,
                                        0};
                host.media(host.mediaUser, &media);
                const int redraws = host.redraws;
                host.media(host.mediaUser, &media);
                check(host.redraws == redraws, "unchanged media does not repaint");
                CdLayout layout{};
                layout.struct_size = sizeof(layout);
                layout.logical_width = spec.columns * 77;
                layout.logical_height = spec.rows * 100;
                layout.device_pixel_ratio = 1;
                layout.safe_content = {4, 4, layout.logical_width - 8, layout.logical_height - 8};
                instance.layout_changed(instance.instance, &layout);
                check(host.timers.size() == 1 && host.timers.front().interval == 33,
                      "Playing records share one 30 fps timer with progress updates");
                std::vector<uint8_t> recordPixels(size_t(layout.logical_width * layout.logical_height) * 4);
                CdFrame recordFrame{sizeof(recordFrame),
                                    uint32_t(layout.logical_width),
                                    uint32_t(layout.logical_height),
                                    uint32_t(layout.logical_width) * 4,
                                    recordPixels.size(),
                                    recordPixels.data(),
                                    1};
                const auto captureRecord = [&] {
                    check(instance.render(instance.instance, &recordFrame) == CD_OK, "Render live record");
                    return recordPixels;
                };
                if (pngPlayer) {
                    captureRecord();
                    const auto deadline = GetTickCount64() + 5000;
                    while (host.timers.size() > 1) {
                        check(GetTickCount64() < deadline, "Live PNG player materials must settle");
                        Sleep(1);
                        host.tick();
                        captureRecord();
                    }
                }
                auto recordBefore = captureRecord();
                Sleep(40);
                host.tick();
                check(captureRecord() != recordBefore, "Playing record rotates without metadata updates");
                auto pausedMedia = media;
                pausedMedia.status = CD_MEDIA_PAUSED;
                host.media(host.mediaUser, &pausedMedia);
                check(host.timers.size() == 1 && host.timers.front().interval == 1000,
                      "Paused records release the animation cadence");
                recordBefore = captureRecord();
                const int pausedRedraws = host.redraws;
                Sleep(40);
                host.tick();
                check(captureRecord() == recordBefore && host.redraws == pausedRedraws,
                      "Paused record holds its angle without repaints");
                host.media(host.mediaUser, &media);
                CdSettingValue hideProgress{sizeof(hideProgress), CD_SETTING_TOGGLE, "show_progress", 0};
                check(instance.apply_settings(instance.instance, &hideProgress, 1) == CD_OK,
                      "Hide progress independently");
                recordBefore = captureRecord();
                Sleep(40);
                host.tick();
                check(captureRecord() != recordBefore && host.timers.size() == 1 && host.timers.front().interval == 33,
                      "Record resumes and rotates even with progress hidden");
                recordBefore = captureRecord();
                instance.set_paused(instance.instance, 1);
                Sleep(40);
                instance.set_paused(instance.instance, 0);
                check(captureRecord() == recordBefore,
                      "Hidden record preserves its angle without catching up on resume");
                const auto controls =
                    arrangePlayer(spec, float(layout.safe_content.width), float(layout.safe_content.height));
                const auto click = [&](double x, double y) {
                    CdInput event{sizeof(event), CD_INPUT_PRESS, CD_BUTTON_LEFT, 0, x + 4, y + 4, 0};
                    instance.input(instance.instance, &event);
                    event.kind = CD_INPUT_RELEASE;
                    event.buttons = 0;
                    instance.input(instance.instance, &event);
                };
                constexpr uint32_t expected[]{CD_MEDIA_PREVIOUS, CD_MEDIA_TOGGLE, CD_MEDIA_NEXT};
                for (int i = 0; i < 3; ++i) {
                    const auto& r = controls.buttons[i];
                    click(r.x + r.width * .5, r.y + r.height * .5);
                    check(host.commands == i + 1 && host.lastCommand == expected[i], "player button dispatch");
                }
                click(controls.timeline.x + controls.timeline.width * .5, controls.timeline.y);
                click(0, 0);
                check(host.commands == 3, "unsupported seek and background clicks send no commands");
                auto busy = media;
                busy.command_result = CD_ERROR_BUSY;
                host.media(host.mediaUser, &busy);
                for (const auto& r : controls.buttons) {
                    click(r.x + r.width * .5, r.y + r.height * .5);
                }
                check(host.commands == 3, "pending media command still blocks duplicate dispatch");
                host.media(host.mediaUser, &media);
                auto disabled = media;
                disabled.controls = 0;
                host.media(host.mediaUser, &disabled);
                const auto& r = controls.buttons[1];
                click(r.x + r.width * .5, r.y + r.height * .5);
                check(host.commands == 3, "unavailable controls send no commands");
                const CdSettingValue reduced{sizeof(reduced), CD_SETTING_TOGGLE, "reduced_motion", 1};
                check(instance.apply_settings(instance.instance, &reduced, 1) == CD_OK && host.timers.size() == 1 &&
                          host.timers.front().interval == 1000,
                      "reduced motion releases the rotating-record cadence");
                recordBefore = captureRecord();
                const int reducedRedraws = host.redraws;
                Sleep(40);
                host.tick();
                check(captureRecord() == recordBefore && host.redraws == reducedRedraws,
                      "reduced motion leaves the record stationary without decorative redraws");
                CdSettingValue selected{sizeof(selected), CD_SETTING_CHOICE, "player", 0, 0, "qqmusic"};
                check(instance.apply_settings(instance.instance, &selected, 1) == CD_OK && host.player == "qqmusic",
                      "player preference resubscribes atomically");
                selected.text = "cyber_player";
                check(
                    instance.apply_settings(instance.instance, &selected, 1) == CD_OK && host.player == "cyber_player",
                    "CyberPlayer preference selects its Windows media session");
                check(playerName(L"CYBER_PLAYER.EXE", false) == L"CyberPlayer" &&
                          playerName(L"CyberDesktop.CyberPlayer", true) == L"CyberPlayer",
                      "CyberPlayer session names use the product name");
            }
            if (type == Kind::Calendar) {
                CdLayout layout{};
                layout.struct_size = sizeof(layout);
                layout.logical_width = 288;
                layout.logical_height = 200;
                layout.device_pixel_ratio = 1;
                layout.safe_content = {4, 4, 280, 192};
                instance.layout_changed(instance.instance, &layout);
                CdInput event{sizeof(CdInput), CD_INPUT_PRESS, CD_BUTTON_LEFT, 0, 268, 18, 0};
                instance.input(instance.instance, &event);
                event.kind = CD_INPUT_RELEASE;
                event.buttons = 0;
                instance.input(instance.instance, &event);
                std::array<uint8_t, 8> saved{};
                uint64_t count = 0;
                check(instance.save_state(instance.instance, saved.data(), saved.size(), &count) == CD_OK &&
                          saved[4] == 1,
                      "month navigation saves offset");
                event.kind = CD_INPUT_DOUBLE_CLICK;
                instance.input(instance.instance, &event);
                check(instance.save_state(instance.instance, saved.data(), saved.size(), &count) == CD_OK &&
                          saved[4] == 0,
                      "double click returns to current month");
            }
            for (int ch : {72, 100, 150})
                for (double dpr : {1.0, 1.5, 2.0}) {
                    const uint32_t width = uint32_t(spec.columns * 72 * dpr), height = uint32_t(spec.rows * ch * dpr),
                                   stride = width * 4 + 12;
                    std::vector<uint8_t> pixels(size_t(stride) * height + 32, 0xCD);
                    CdFrame frame{sizeof(CdFrame),           width,         height, stride,
                                  uint64_t(stride) * height, pixels.data(), dpr};
                    check(instance.render(instance.instance, &frame) == CD_OK, "render size and DPI");
                    for (uint32_t y = 0; y < height; ++y) {
                        for (uint32_t x = width * 4; x < stride; ++x)
                            check(pixels[size_t(y) * stride + x] == 0xCD, "row guard");
                        for (uint32_t x = 0; x < width; ++x) {
                            const auto* p = pixels.data() + size_t(y) * stride + x * 4;
                            check(p[0] <= p[3] && p[1] <= p[3] && p[2] <= p[3], "premultiplied alpha");
                        }
                    }
                    for (size_t i = size_t(stride) * height; i < pixels.size(); ++i)
                        check(pixels[i] == 0xCD, "capacity guard");
                }
            if (isMusic(type)) {
                check(host.audio, "audio subscription");
                data.fill(0);
                data[10] = .8f;
                host.audio(host.audioUser, data.data(), 63, 0, 0);
                // The real timer is 33 ms. A same-timestamp callback has zero delta
                // and correctly requests no redraw, especially with cached fonts.
                Sleep(35);
                host.tick();
                check(host.redraws > 0, "music redraw");
            }
            instance.set_paused(instance.instance, 1);
            check(host.timers.empty() && !host.audio && !host.media && !host.inputStats,
                  "pause releases subscriptions");
            const int redraws = host.redraws;
            host.tick();
            check(host.redraws == redraws, "paused no redraw");
            instance.set_paused(instance.instance, 0);
            check(type == Kind::InputStats ? bool(host.inputStats)
                  : type == Kind::Album    ? host.timers.empty()
                                           : !host.timers.empty(),
                  "resume subscription");
            uint64_t size = 0;
            check(instance.save_state(instance.instance, nullptr, 0, &size) == CD_OK && size == 8, "save size");
            std::array<uint8_t, 8> state{};
            check(instance.save_state(instance.instance, state.data(), 8, &size) == CD_OK &&
                      instance.restore_state(instance.instance, state.data(), 8) == CD_OK,
                  "state roundtrip");
        }
        std::cout << "PASS: music dynamics, silence, date arithmetic, photo "
                     "rendering, player controls, input stats, 144 size/DPI renders, instance lifecycle\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
