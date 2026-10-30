#include <Windows.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>

#include "plugins/widget-suite/AlbumArtwork.h"
#include "plugins/widget-suite/Widget.h"
#include "sdk/tools/Runtime.h"

#ifdef CD_PLAYER_DESIGN_GLASS_ICONS
#include <CyberGlass/Icons.h>

#include <QApplication>
#include <QImage>
#include <QPainter>

#include "sdk/services/Icons.h"

namespace {

CdResult CD_CALL rasterIcon(void*, const char* name, uint32_t rgba, const CdFrame* frame) {
    QImage image(frame->rgba, int(frame->width), int(frame->height), int(frame->stride),
                 QImage::Format_RGBA8888_Premultiplied);
    image.setDevicePixelRatio(frame->device_pixel_ratio);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    cyberglass::icons::paint(
        painter, {0, 0, frame->width / frame->device_pixel_ratio, frame->height / frame->device_pixel_ratio},
        QString::fromLatin1(name), QColor((rgba >> 24) & 255, (rgba >> 16) & 255, (rgba >> 8) & 255, rgba & 255));
    return CD_OK;
}

CdResult CD_CALL queryService(void* context, const char* name, uint32_t version, void* output, uint32_t capacity) {
    if (std::strcmp(name, CD_SERVICE_ICONS) == 0 && version == 1 && capacity >= sizeof(CdIconsApi)) {
        const CdIconsApi icons{sizeof(icons), 1, rasterIcon};
        std::memcpy(output, &icons, sizeof(icons));
        return CD_OK;
    }
    return cdsdk::MockHost::queryService(context, name, version, output, capacity);
}

}  // namespace
#endif

namespace {

void bindMaterials(widgetplugin::Canvas& canvas, cdsdk::MockHost& host, widgetplugin::Spec spec) {
#ifdef CD_PLAYER_DESIGN_GLASS_ICONS
    host.extended.query_service = queryService;
#endif
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, DWORD(std::size(executable)));
    const auto id = spec.kind == widgetplugin::Kind::Album
                        ? std::string(spec.id)
                        : "com.cyberdesktop.music.player.mechanical." + std::to_string(spec.rows) + "x" +
                              std::to_string(spec.columns);
    const auto path = std::filesystem::path(executable).parent_path() / "plugins" / id;
    const auto bytes = path.u8string();
    const std::string directory(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    const CdCreateContext context{
        sizeof(context), CD_PLUGIN_ABI_VERSION, id.c_str(), "design", directory.c_str(), &host.api, &host};
    canvas.bind(context, [](uint32_t) {
    });
}

bool renderSettled(widgetplugin::Canvas& canvas, cdsdk::MockHost& host, widgetplugin::Spec spec,
                   const cdsdk::Settings& settings, const CdFrame& frame, const widgetplugin::Media& media) {
    const auto deadline = GetTickCount64() + 5000;
    do {
        if (!canvas.render(spec, settings, {}, {}, {}, 1, {}, 0, frame, {}, media)) {
            return false;
        }
        if (!canvas.pendingImages()) {
            return true;
        }
        host.tick(double(GetTickCount64()) / 1000);
        Sleep(1);
    } while (GetTickCount64() < deadline);
    return false;
}

int albumDesign(const std::filesystem::path& output, const std::filesystem::path& sample) {
    using namespace widgetplugin;
    const Spec spec{"com.cyberdesktop.album.mechanical.2x4", Kind::Album, 2, 4, true};
    cdsdk::Images images;
    images.set("sample", sample, 0, 2048);
    const auto deadline = GetTickCount64() + 5000;
    while (images.pending() && GetTickCount64() < deadline) {
        images.poll();
        Sleep(1);
    }
    Photo photo{images.get("sample").image, 2, 24};
    if (!photo.bitmap) {
        return 5;
    }
    auto settings = defaults(Kind::Album, true);
    for (int cell : {80, 96, 112, 128, 160}) {
        for (double dpi : {1., 1.5, 2.}) {
            cdsdk::MockHost host;
            Canvas canvas;
            bindMaterials(canvas, host, spec);
            const int logicalWidth = cell * 4 - 6, logicalHeight = cell * 5 / 2 - 6;
            const auto width = uint32_t(logicalWidth * dpi), height = uint32_t(logicalHeight * dpi);
            std::vector<uint8_t> pixels(size_t(width) * height * 4);
            const CdFrame frame{sizeof(frame), width, height, width * 4, pixels.size(), pixels.data(), dpi};
            const auto render = [&](const Photo& value, AlbumPlayback playback = {}) {
                const auto until = GetTickCount64() + 5000;
                do {
                    if (!canvas.render(spec, settings, {}, value, {}, 1, {}, 0, frame, {}, {}, 0, {}, nullptr,
                                       playback)) {
                        throw std::runtime_error("Album native render failed");
                    }
                    host.tick(double(GetTickCount64()) / 1000);
                    Sleep(1);
                } while (canvas.pendingImages() && GetTickCount64() < until);
                if (canvas.pendingImages()) {
                    throw std::runtime_error("Album PNG load timed out");
                }
                if (!canvas.render(spec, settings, {}, value, {}, 1, {}, 0, frame, {}, {}, 0, {}, nullptr, playback)) {
                    throw std::runtime_error("Settled album native render failed");
                }
            };
            const auto save = [&](const wchar_t* name) {
                if (cell == 112 && dpi == 2 && !canvas.savePng((output / name).wstring(), frame)) {
                    throw std::runtime_error("Album preview write failed");
                }
            };
            render(photo);
            const auto normal = pixels;
            save(L"album-normal.png");
            const auto layout = arrangeAlbum(logicalWidth, logicalHeight);
            render(photo, {false, false, 8});
            if (pixels == normal) {
                throw std::runtime_error("Gentle photo motion must change the photo pixels");
            }
            save(L"album-motion.png");
            for (uint32_t y = 0; y < height; ++y) {
                for (uint32_t x = 0; x < width; ++x) {
                    if (x < (layout.photo.x - 1) * dpi || x > (layout.photo.x + layout.photo.width + 1) * dpi ||
                        y < (layout.photo.y - 1) * dpi || y > (layout.photo.y + layout.photo.height + 1) * dpi) {
                        const size_t offset = (size_t(y) * width + x) * 4;
                        if (!std::equal(pixels.begin() + offset, pixels.begin() + offset + 4,
                                        normal.begin() + offset)) {
                            throw std::runtime_error("Photo motion must never move the metal frame");
                        }
                    }
                }
            }
            render(photo, {false, false, 24});
            if (pixels != normal) {
                throw std::runtime_error("Photo motion must return continuously to its starting pose");
            }
            const CdSettingValue stopMotion{sizeof(stopMotion), CD_SETTING_TOGGLE, "photo_motion", 0};
            settings.apply(&stopMotion, 1);
            render(photo, {false, false, 8});
            if (pixels != normal) {
                throw std::runtime_error("Disabled photo motion must remain static");
            }
            const CdSettingValue startMotion{sizeof(startMotion), CD_SETTING_TOGGLE, "photo_motion", 1};
            settings.apply(&startMotion, 1);
            const CdSettingValue reduced{sizeof(reduced), CD_SETTING_TOGGLE, "reduced_motion", 1};
            settings.apply(&reduced, 1);
            render(photo, {false, false, 8});
            if (pixels != normal) {
                throw std::runtime_error("Reduced motion setting must keep photos still");
            }
            const CdSettingValue fullMotion{sizeof(fullMotion), CD_SETTING_TOGGLE, "reduced_motion", 0};
            settings.apply(&fullMotion, 1);
            CdInput pointer{sizeof(pointer),
                            CD_INPUT_MOVE,
                            0,
                            0,
                            layout.photo.x + layout.photo.width / 2,
                            layout.photo.y + layout.photo.height / 2};
            canvas.input(pointer);
            render(photo);
            if (pixels == normal) {
                throw std::runtime_error("Hover must reveal mechanical materials and counter");
            }
            const auto hover = pixels;
            save(L"album-hover.png");
            render(photo, {true, false});
#ifdef CD_PLAYER_DESIGN_GLASS_ICONS
            if (pixels == hover) {
                throw std::runtime_error("Pause must switch the Lucide icon to play");
            }
#endif
            save(L"album-paused.png");
            pointer.kind = CD_INPUT_LEAVE;
            canvas.input(pointer);
            render(photo);
            if (pixels != normal || pixels[3] != 0) {
                throw std::runtime_error("Leave restores clean photo; exterior must remain transparent");
            }
            render({});
            save(L"album-empty.png");
            const CdSettingValue fit{sizeof(fit), CD_SETTING_CHOICE, "fit", 0, 0, "contain"};
            if (settings.apply(&fit, 1) != CD_OK) {
                throw std::runtime_error("Album contain setting failed");
            }
            render(photo);
            const CdSettingValue cover{sizeof(cover), CD_SETTING_CHOICE, "fit", 0, 0, "cover"};
            settings.apply(&cover, 1);
        }
    }
    std::cout << "Mechanical album: photo easing, fixed casing, motion toggles, hover, pause, empty and fit at 15 "
                 "sizes/DPI PASS\n";
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
#ifdef CD_PLAYER_DESIGN_GLASS_ICONS
    int qtArgc = 1;
    char applicationName[] = "PlayerDesign";
    char* qtArgv[]{applicationName, nullptr};
    QApplication application(qtArgc, qtArgv);
#endif
    if (argc < 2 || argc > 4)
        return 1;
    using namespace widgetplugin;
    std::filesystem::create_directories(argv[1]);
    if (argc == 4 && std::wstring(argv[2]) == L"album") {
        return albumDesign(argv[1], argv[3]);
    }
    Media media;
    media.source = L"QQMusic.exe";
    media.title = L"45.Alan Walker - Faded";
    media.artist = L"骆集益";
    media.album = L"仙籁情深";
    media.status = CD_MEDIA_PLAYING;
    media.controls = CD_MEDIA_CAN_TOGGLE | CD_MEDIA_CAN_PREVIOUS | CD_MEDIA_CAN_NEXT;
    media.duration = 123.96;
    media.position = 46;
    std::vector<uint8_t> cover(192 * 192 * 4);
    // Synthetic artwork for native renderer previews; live artwork comes from the player.
    for (int y = 0; y < 192; ++y) {
        for (int x = 0; x < 192; ++x) {
            const double t = y / 191.0;
            uint8_t r = uint8_t(223 - 110 * t), g = uint8_t(144 - 66 * t), b = uint8_t(102 + 18 * t);
            if ((x - 125) * (x - 125) + (y - 57) * (y - 57) < 26 * 26) {
                r = 251;
                g = 218;
                b = 147;
            }
            if (y > 120 + 17 * std::sin(x * .025)) {
                r = 39;
                g = 80;
                b = 87;
            }
            if (y > 155 + 12 * std::sin(x * .035 + 1)) {
                r = 25;
                g = 52;
                b = 65;
            }
            const size_t offset = size_t(y * 192 + x) * 4;
            cover[offset] = b;
            cover[offset + 1] = g;
            cover[offset + 2] = r;
            cover[offset + 3] = 255;
        }
    }
    media.cover.bitmap = cybercanvas::ImageData::fromPremultipliedBgra(192, 192, std::move(cover));
    auto settings = defaults(Kind::Player, argc >= 3 && std::wstring(argv[2]) == L"mechanical");
    const bool mechanical = argc >= 3 && std::wstring(argv[2]) == L"mechanical";
    for (auto spec : {Spec{"player-1x4", Kind::Player, 1, 4}, Spec{"player-2x2", Kind::Player, 2, 2},
                      Spec{"player-2x4", Kind::Player, 2, 4}}) {
        spec.mechanical = mechanical;
        cdsdk::MockHost host;
        Canvas canvas;
        if (mechanical) {
            bindMaterials(canvas, host, spec);
        }
        for (const int cellHeight : {72, 100, 150}) {
            const uint32_t w = uint32_t(spec.columns * 77 * 2), h = uint32_t(spec.rows * cellHeight * 2);
            std::vector<uint8_t> pixels(size_t(w) * h * 4);
            CdFrame frame{sizeof(frame), w, h, w * 4, pixels.size(), pixels.data(), 2};
            if (!renderSettled(canvas, host, spec, settings, frame, media)) {
                return 2;
            }
            const auto name =
                std::wstring(spec.id, spec.id + std::strlen(spec.id)) + L"-" + std::to_wstring(cellHeight) + L".png";
            if (!canvas.savePng((std::filesystem::path(argv[1]) / name).wstring(), frame))
                return 3;
        }
    }
    std::cout << "9 native-render player designs exported\n";
    if ((!mechanical && argc == 3) || (mechanical && argc == 4)) {
        cdsdk::MockHost host;
        Canvas canvas;
        const Spec spec{"vinyl-preview", Kind::Player, 2, 4, mechanical};
        if (mechanical) {
            bindMaterials(canvas, host, spec);
        }
        const auto frames = argv[mechanical ? 3 : 2];
        std::filesystem::create_directories(frames);
        constexpr uint32_t width = 600, height = 384;
        std::vector<uint8_t> pixels(size_t(width) * height * 4);
        CdFrame frame{sizeof(frame), width, height, width * 4, pixels.size(), pixels.data(), 2};
        for (int index = 0; index < 240; ++index) {
            // Six seconds playing, then two paused; progress stays fixed in this artwork demo.
            media.recordDegrees = std::min(index, 180) * .6;
            media.status = index < 180 ? CD_MEDIA_PLAYING : CD_MEDIA_PAUSED;
            const auto name = L"record-" + std::to_wstring(1000 + index).substr(1) + L".png";
            if (!renderSettled(canvas, host, spec, settings, frame, media) ||
                !canvas.savePng((std::filesystem::path(frames) / name).wstring(), frame)) {
                return 4;
            }
        }
        std::cout << "240 rotating and paused record frames exported\n";
    }
}
