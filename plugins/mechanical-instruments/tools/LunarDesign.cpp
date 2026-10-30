#include <CyberGlass/Icons.h>
#include <Windows.h>

#include <QApplication>
#include <QImage>
#include <QPainter>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <thread>

#include "plugins/lunar-suite/MechanicalLunar.h"

namespace {
unsigned iconCalls = 0;

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
    ++iconCalls;
    return CD_OK;
}

CdResult CD_CALL queryService(void*, const char* name, uint32_t version, void* output, uint32_t capacity) {
    if (std::strcmp(name, CD_SERVICE_ICONS) || version != 1 || capacity < sizeof(CdIconsApi)) {
        return CD_ERROR_UNSUPPORTED;
    }
    *static_cast<CdIconsApi*>(output) = {sizeof(CdIconsApi), 1, rasterIcon};
    return CD_OK;
}

void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

struct Panel {
    const char* id;
    const char* folder;
    int rows, columns;
};

constexpr Panel kPanels[]{{"com.cyberdesktop.calendar.lunar.mechanical.1x2", "lunar-1x2", 1, 2},
                          {"com.cyberdesktop.calendar.lunar.mechanical.2x2", "lunar-2x2", 2, 2},
                          {"com.cyberdesktop.calendar.lunar.mechanical.4x4", "lunar-4x4", 4, 4}};

void click(lunarplugin::Renderer& renderer, cybercanvas::Rect bounds, uint32_t expected, uint32_t& activated) {
    activated = 0;
    CdInput event{};
    event.struct_size = sizeof(event);
    event.x = bounds.x + bounds.width / 2;
    event.y = bounds.y + bounds.height / 2;
    event.buttons = CD_BUTTON_LEFT;
    event.kind = CD_INPUT_PRESS;
    renderer.input(event);
    event.kind = CD_INPUT_RELEASE;
    event.buttons = 0;
    renderer.input(event);
    require(activated == expected, "Actual controls must match visible navigation and date cells");
}
}  // namespace

int wmain(int argc, wchar_t** argv) try {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    int qtArgc = 1;
    char name[] = "lunar-design";
    char* qtArgv[]{name, nullptr};
    QApplication application(qtArgc, qtArgv);
    require(argc == 3, "Expected repository and fixed output directory");
    const std::filesystem::path root(argv[1]), output(argv[2]);
    const auto build = std::filesystem::absolute(argv[0]).parent_path();
    std::filesystem::create_directories(output);
    for (const auto& panel : kPanels) {
        const auto module = (build / "plugins" / panel.id).u8string();
        CdServiceHostApi host{};
        auto& base = *reinterpret_cast<CdHostApi*>(&host);
        base.struct_size = sizeof(host);
        base.abi_version = CD_PLUGIN_ABI_VERSION;
        host.query_service = queryService;
        base.request_redraw = [](void*) {
        };
        const CdCreateContext context{sizeof(context),
                                      CD_PLUGIN_ABI_VERSION,
                                      panel.id,
                                      "native-design",
                                      reinterpret_cast<const char*>(module.c_str()),
                                      reinterpret_cast<const CdHostApi*>(&host)};
        uint32_t activated = 0;
        lunarplugin::Renderer renderer;
        renderer.bind(context, [&](uint32_t id) {
            activated = id;
        });
        auto settings = lunarplugin::defaults(true);
        const lunarplugin::Spec spec{panel.id, panel.rows, panel.columns, true};
        lunarplugin::View view{{2026, 2, 17}, {2026, 2, 17}, true};
        for (unsigned cell : {80u, 96u, 112u, 128u, 160u}) {
            for (double dpi : {1., 1.5, 2.}) {
                const double logicalWidth = panel.columns * cell - 6., logicalHeight = panel.rows * cell * 1.25 - 6.;
                const auto width = uint32_t(std::ceil(logicalWidth * dpi)),
                           height = uint32_t(std::ceil(logicalHeight * dpi));
                const uint32_t stride = width * 4 + 16;
                std::vector<uint8_t> pixels(size_t(stride) * height + 32, 0xa5);
                const CdFrame frame{sizeof(frame), width, height, stride, size_t(stride) * height, pixels.data(), dpi};
                const auto paint = [&] {
                    require(renderer.render(spec, settings, view, frame, {}), "Native mechanical lunar render");
                };
                paint();
                for (unsigned attempt = 0; renderer.pendingImages() && attempt < 1000; ++attempt) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    paint();
                }
                require(!renderer.pendingImages(), "Lunar materials must finish decoding");
                paint();
                const auto stable = pixels;
                view.selected = {2026, 2, 18};
                paint();
                require(pixels != stable, "Dates and solar terms must be live, not baked into the material");
                view.selected = {2026, 2, 17};
                paint();
                require(pixels == stable, "Stable lunar frame is deterministic");
                for (uint32_t y = 0; y < height; ++y) {
                    for (uint32_t x = 0; x < width; ++x) {
                        const auto* p = pixels.data() + size_t(y) * stride + x * 4;
                        require(p[0] <= p[3] && p[1] <= p[3] && p[2] <= p[3], "Premultiplied alpha");
                        if (x < 2 || y < 2 || x + 2 >= width || y + 2 >= height) {
                            require(!p[3], "Transparent outer spacing");
                        }
                    }
                    for (uint32_t x = width * 4; x < stride; ++x) {
                        require(pixels[size_t(y) * stride + x] == 0xa5, "Row guard");
                    }
                }
                for (size_t i = size_t(stride) * height; i < pixels.size(); ++i) {
                    require(pixels[i] == 0xa5, "Capacity guard");
                }
                const auto file = output / (std::string(panel.folder) + "-w" + std::to_string(cell) + "-dpi" +
                                            std::to_string(int(dpi * 100)) + ".png");
                require(renderer.save(file.wstring(), frame), "Save native lunar preview");
                if (cell == 96 && dpi == 2) {
                    std::filesystem::copy_file(
                        file, root / "plugins/mechanical-instruments/packages" / panel.folder / "preview.png",
                        std::filesystem::copy_options::overwrite_existing);
                }
            }
        }
        // Keep the destination calendar fixed: intermediate frames may differ only
        // inside the changed month roller, never in the case or other readouts.
        {
            const double w = panel.columns * 96. - 6, h = panel.rows * 120. - 6;
            const uint32_t width = uint32_t(w * 2), height = uint32_t(h * 2);
            std::vector<uint8_t> pixels(size_t(width) * height * 4);
            const CdFrame frame{sizeof(frame), width, height, width * 4, pixels.size(), pixels.data(), 2};
            view.previous = {2026, 1, 17};
            const auto capture = [&](double progress, const char* name) {
                view.progress = progress;
                require(renderer.render(spec, settings, view, frame, {}), "Render native wheel transition");
                require(
                    renderer.save((output / (std::string(panel.folder) + "-roll-" + name + ".png")).wstring(), frame),
                    "Save native wheel transition");
                return pixels;
            };
            const auto start = capture(0, "start"), middle = capture(.5, "middle"), end = capture(1, "end");
            require(start != middle && middle != end && start != end, "Month roller has distinct moving frames");
            const auto layout = lunarplugin::mechanicalLunarLayout(spec, w, h);
            const auto moving = layout.stage.map(panel.rows == 1   ? cybercanvas::Rect{527, 283, 110, 100}
                                                 : panel.rows == 2 ? cybercanvas::Rect{637, 192, 182, 112}
                                                                   : cybercanvas::Rect{807, 140, 52, 95});
            for (uint32_t y = 0; y < height; ++y) {
                for (uint32_t x = 0; x < width; ++x) {
                    const double lx = x / 2., ly = y / 2.;
                    if (lx >= moving.x - 1 && lx <= moving.x + moving.width + 1 && ly >= moving.y - 1 &&
                        ly <= moving.y + moving.height + 1) {
                        continue;
                    }
                    const size_t offset = (size_t(y) * width + x) * 4;
                    require(std::equal(start.begin() + offset, start.begin() + offset + 4, middle.begin() + offset) &&
                                std::equal(start.begin() + offset, start.begin() + offset + 4, end.begin() + offset),
                            "Rolling numbers stay clipped to their own material windows");
                }
            }
            view.direction = -1;
            require(capture(.5, "backward") != middle, "Backward navigation reverses the wheel motion");
            view.progress = 1;
            view.direction = 1;
            std::cout << "PASS clipped native forward/backward roller frames: " << panel.id << '\n';
        }
        constexpr CdRect safe{13, 11, 360, 440};
        renderer.syncInput(spec, settings, view, safe);
        const auto layout = lunarplugin::mechanicalLunarLayout(spec, safe.width, safe.height);
        const auto mapped = [&](cybercanvas::Rect box) {
            auto result = layout.stage.map(box);
            result.x += safe.x;
            result.y += safe.y;
            return result;
        };
        if (panel.rows == 1) {
            click(renderer, mapped(layout.visible), 3, activated);
        } else {
            click(renderer, mapped(layout.previous), 1, activated);
            click(renderer, mapped(layout.next), 2, activated);
            click(renderer, mapped(layout.header), 3, activated);
            const auto previous = layout.stage.map(layout.previous);
            require(lunarplugin::hit(spec, safe.width, safe.height, previous.x + previous.width / 2,
                                     previous.y + previous.height / 2) == -1,
                    "Hit helper uses the artwork transform");
        }
        if (panel.rows == 4) {
            const auto exerciseMonth = [&](lunarplugin::Date selected, bool monday, unsigned expectedRows) {
                const CdSettingValue first{
                    sizeof(first), CD_SETTING_CHOICE, "week_start", 0, 0, monday ? "monday" : "sunday"};
                require(settings.apply(&first, 1) == CD_OK, "Week start setting");
                view.selected = selected;
                const auto month = lunarplugin::monthGrid(selected, monday);
                require(month.rows == int(expectedRows), "Month fixture row count");
                renderer.syncInput(spec, settings, view, safe);
                const cybercanvas::CalendarGrid grid{layout.dates, size_t(month.rows)};
                for (size_t i = 0; i < size_t(month.rows * 7); ++i) {
                    if (month.dates[i].month == selected.month) {
                        click(renderer, mapped(grid.cell(i)), uint32_t(100 + i), activated);
                    }
                }
                std::vector<uint8_t> pixels(756 * 948 * 4);
                const CdFrame frame{sizeof(frame), 756, 948, 756 * 4, pixels.size(), pixels.data(), 2};
                require(renderer.render(spec, settings, view, frame, {}), "Render variable-week calendar");
                require(
                    renderer.save((output / ("month-rows-" + std::to_string(expectedRows) + ".png")).wstring(), frame),
                    "Save variable-week validation");
            };
            exerciseMonth({2026, 2, 17}, false, 4);
            exerciseMonth({2026, 2, 17}, true, 5);
            exerciseMonth({2026, 3, 17}, true, 6);
            view.selected = {1900, 1, 1};
            renderer.syncInput(spec, settings, view, safe);
            activated = 0;
            CdInput event{};
            event.struct_size = sizeof(event);
            const auto previous = mapped(layout.previous);
            event.x = previous.x + previous.width / 2;
            event.y = previous.y + previous.height / 2;
            event.buttons = CD_BUTTON_LEFT;
            event.kind = CD_INPUT_PRESS;
            renderer.input(event);
            event.kind = CD_INPUT_RELEASE;
            event.buttons = 0;
            renderer.input(event);
            require(!activated, "Navigation stops at the supported calendar boundary");
        }
        std::cout << "PASS 15 native grid/DPI captures, live lunar data, input alignment and guards: " << panel.id
                  << '\n';
    }
    std::cout << "PASS monthly 4/5/6 week grids, both week starts, date selection and 1900 boundary\n";
    require(iconCalls > 0, "Native previews must include the Desktop Lucide service");
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
