#include <CyberGlass/Icons.h>

#include <QApplication>
#include <QImage>
#include <QPainter>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>

#include "plugins/mechanical-calendar/tools/TestHost.h"

namespace {
using namespace mechanicalcalendar;

void check(bool passed, const char* message) {
    if (!passed) {
        throw std::runtime_error(message);
    }
}

CdResult CD_CALL raster(void* pointer, const char* name, uint32_t color, const CdFrame* output) {
    ++static_cast<testing::Host*>(pointer)->iconCalls;
    QImage image(output->rgba, int(output->width), int(output->height), int(output->stride),
                 QImage::Format_RGBA8888_Premultiplied);
    image.setDevicePixelRatio(output->device_pixel_ratio);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    cyberglass::icons::paint(
        painter, QRectF(0, 0, output->width / output->device_pixel_ratio, output->height / output->device_pixel_ratio),
        QString::fromLatin1(name),
        QColor(int(color >> 24), int((color >> 16) & 255), int((color >> 8) & 255), int(color & 255)));
    return CD_OK;
}

void render(Renderer& renderer, const Month& month, const cdsdk::Settings& settings, const Visual& visual, double width,
            double height, double dpi, const std::filesystem::path& file) {
    const auto w = uint32_t(std::ceil(width * dpi)), h = uint32_t(std::ceil(height * dpi));
    std::vector<uint8_t> pixels(size_t(w) * h * 4 + 32);
    std::fill(pixels.end() - 32, pixels.end(), uint8_t{0xa5});
    CdFrame frame{sizeof(frame), w, h, w * 4, size_t(w) * h * 4, pixels.data(), dpi};
    for (unsigned pass = 0; pass < 200; ++pass) {
        check(renderer.render(month, settings, visual, frame), "Native month render failed");
        if (renderer.ready()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(renderer.ready(), "Mechanical housing failed to decode");
    check(std::all_of(pixels.end() - 32, pixels.end(),
                      [](auto p) {
                          return p == 0xa5;
                      }),
          "Buffer guard changed");
    check(renderer.save(file, frame), "Actual native PNG export failed");
    const auto cached = renderer.cacheBuilds();
    check(renderer.render(month, settings, visual, frame) && renderer.cacheBuilds() == cached,
          "Unchanged calendar frame rebuilt its static material cache");
}
}  // namespace

int wmain(int argc, wchar_t** argv) try {
    check(argc == 3, "Expected repository and fixed output directory");
    int guiArgc = 1;
    char applicationName[] = "mechanical-calendar-design";
    char* guiArgv[]{applicationName, nullptr};
    QApplication application(guiArgc, guiArgv);
    const std::filesystem::path root(argv[1]), output(argv[2]);
    testing::Host host;
    host.iconApi = {sizeof(CdIconsApi), 1, raster};
    const auto path = (root / "build/relwithdebinfo/plugins" / kPluginId).u8string();
    const std::string directory(reinterpret_cast<const char*>(path.data()), path.size());
    Renderer renderer(host.context(directory.c_str()));
    auto settings = defaults();
    unsigned count = 0;
    for (Date date : {Date{2026, 10, 13}, Date{2026, 8, 13}}) {
        const auto calendar = month(date, 0, true);
        for (unsigned cell : {80u, 96u, 112u, 128u, 160u}) {
            for (double dpi : {1., 1.5, 2.}) {
                const auto name = std::to_string(calendar.weeks) + "weeks-w" + std::to_string(cell) + "-dpi" +
                                  std::to_string(unsigned(dpi * 100)) + ".png";
                render(renderer, calendar, settings, {}, cell * 4 - 6., cell * 2.5 - 6., dpi, output / name);
                if (date.month == 10 && cell == 96 && dpi == 2) {
                    std::filesystem::copy_file(output / name,
                                               root / "plugins/mechanical-calendar/packages/4x2/preview.png",
                                               std::filesystem::copy_options::overwrite_existing);
                }
                ++count;
            }
        }
    }
    render(renderer, month({2026, 10, 13}, 0, true), settings, {}, 960, 600, 2, output / "detail.png");
    render(renderer, month({2026, 10, 13}, 0, true), settings, {{}, 1, 1, 1}, 378, 234, 2,
           output / "navigation-hover.png");
    render(renderer, month({2026, 10, 13}, 0, true), settings, {{}, 1, 1, 1, 1}, 378, 234, 2,
           output / "navigation-pressed.png");
    render(renderer, month({2021, 2, 13}, 0, true), settings, {}, 960, 600, 2, output / "four-weeks.png");
    render(renderer, month({2026, 10, 13}, 0, false), settings, {}, 960, 600, 2, output / "sunday-first.png");
    const CdSettingValue english{sizeof(english), CD_SETTING_CHOICE, "language", 0, 0, "en"};
    check(settings.apply(&english, 1) == CD_OK, "English settings failed");
    render(renderer, month({2026, 10, 13}, 0, false), settings, {}, 960, 600, 2, output / "english.png");
    const CdSettingValue accent{sizeof(accent), CD_SETTING_COLOR, "accent", 0x2681e6ff};
    check(settings.apply(&accent, 1) == CD_OK, "Color settings failed");
    render(renderer, month({2026, 10, 13}, 0, false), settings, {}, 960, 600, 2, output / "custom-color.png");
    check(host.iconCalls > 0, "Actual CyberGlass Lucide host entry was not exercised");
    std::cout << "PASS " << count << " native layout/DPI renders, real Lucide host icons and static cache reuse\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
}
