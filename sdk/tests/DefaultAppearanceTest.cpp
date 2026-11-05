#include <Windows.h>
#include <sdk/Appearance.h>
#include <sdk/Canvas.h>
#include <sdk/Settings.h>
#include <sdk/cpp/DiagnosticHost.h>
#include <sdk/services/CharacterChat.h>
#include <sdk/services/CharacterImages.h>
#include <sdk/services/Composition.h>
#include <sdk/services/Live2D.h>
#include <sdk/services/TextInput.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "support/Package.h"
#include "tools/Runtime.h"

namespace {
std::map<void*, int> modelConfigurations;
std::map<void*, CdRect> compositionBounds;
std::map<void*, uint32_t> compositionPass;

// This fixture validates plugin appearance only. It starts no model, image worker,
// network connection or conversation; native model rendering has separate checks.
CdResult CD_CALL appearanceServices(void* context, const char* id, uint32_t version, void* output, uint32_t capacity) {
    if (!id || !output || version != 1) {
        return CD_ERROR_ARGUMENT;
    }
    const auto table = [&]<typename T>(T api) {
        if (capacity < sizeof(T)) {
            return CD_ERROR_BUFFER;
        }
        std::memcpy(output, &api, sizeof(api));
        return CD_OK;
    };
    if (std::strcmp(id, CD_SERVICE_COMPOSITION) == 0) {
        return table(CdCompositionApi{sizeof(CdCompositionApi), 1,
                                      [](void* context, const CdRect* bounds, uint32_t enabled) -> CdResult {
                                          if (enabled && bounds) {
                                              compositionBounds[context] = *bounds;
                                          }
                                          return CD_OK;
                                      },
                                      [](void* context) -> uint32_t {
                                          return compositionPass[context];
                                      },
                                      [](void*, uint32_t) {
                                      }});
    }
    if (std::strcmp(id, CD_SERVICE_LIVE2D) == 0) {
        return table(CdLive2DApi{sizeof(CdLive2DApi), 1,
                                 [](void* context, const CdLive2DOptions*) -> CdResult {
                                     ++modelConfigurations[context];
                                     return CD_OK;
                                 },
                                 [](void*, CdLive2DStatus* status) -> CdResult {
                                     *status = {};
                                     status->struct_size = sizeof(*status);
                                     status->state = 2;
                                     return CD_OK;
                                 },
                                 [](void*, double, double, uint32_t) {
                                 }});
    }
    if (std::strcmp(id, CD_SERVICE_CHARACTER_CHAT) == 0) {
        return table(CdCharacterChatApi{sizeof(CdCharacterChatApi), 1,
                                        [](void*, CdCharacterChatCallback, void*) -> CdResult {
                                            return CD_OK;
                                        },
                                        [](void*, const char*, uint32_t) -> CdResult {
                                            return CD_OK;
                                        }});
    }
    if (std::strcmp(id, CD_SERVICE_CHARACTER_IMAGES) == 0) {
        return table(CdCharacterImagesApi{
            sizeof(CdCharacterImagesApi), 1, +[](void*, const char*, uint32_t, CdCharacterImage* output) -> CdResult {
                static constexpr std::array<uint8_t, 4> transparent{};
                *output = {sizeof(*output), CD_CHARACTER_IMAGE_READY, 1, 1, 4, 0, 1, transparent.data(), nullptr};
                return CD_OK;
            }});
    }
    if (std::strcmp(id, CD_SERVICE_CHARACTER_VERSIONED_IMAGES) == 0) {
        return table(CdCharacterVersionedImagesApi{
            sizeof(CdCharacterVersionedImagesApi), 1,
            +[](void*, const char*, const char*, uint32_t, CdCharacterImage* output) -> CdResult {
                static constexpr std::array<uint8_t, 4> transparent{};
                *output = {sizeof(*output), CD_CHARACTER_IMAGE_READY, 1, 1, 4, 0, 1, transparent.data(), nullptr};
                return CD_OK;
            }});
    }
    if (std::strcmp(id, CD_SERVICE_TEXT_INPUT) == 0) {
        return table(CdTextInputApi{sizeof(CdTextInputApi), 1,
                                    [](void*, CdTextInputCallback, void*) -> CdResult {
                                        return CD_OK;
                                    },
                                    [](void*, const CdRect*, const char*, uint32_t) -> CdResult {
                                        return CD_OK;
                                    },
                                    [](void*, const char*, uint32_t) -> CdResult {
                                        return CD_OK;
                                    },
                                    [](void*, uint32_t) -> CdResult {
                                        return CD_OK;
                                    }});
    }
    return cdsdk::MockHost::queryService(context, id, version, output, capacity);
}

void require(bool value, const std::string& message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

uint64_t alpha(const std::vector<uint8_t>& pixels) {
    uint64_t total = 0;
    for (size_t i = 3; i < pixels.size(); i += 4) {
        total += pixels[i];
    }
    return total;
}

size_t colored(const std::vector<uint8_t>& pixels, uint32_t rgba) {
    size_t count = 0;
    for (size_t i = 0; i < pixels.size(); i += 4) {
        if (pixels[i] == uint8_t(rgba >> 24) && pixels[i + 1] == uint8_t(rgba >> 16) &&
            pixels[i + 2] == uint8_t(rgba >> 8) && pixels[i + 3] == uint8_t(rgba)) {
            ++count;
        }
    }
    return count;
}

void apply(cdsdk::NativeInstance& instance, const char* id, uint32_t type, int64_t value, const char* text = "") {
    const CdSettingValue change{sizeof(change), type, id, value, 0, text};
    require(instance.api.apply_settings(instance.api.instance, &change, 1) == CD_OK, instance.id + ": rejected " + id);
}

bool gradientEnds(const std::vector<uint8_t>& pixels, uint32_t width, bool reverse) {
    size_t start = 0, end = 0;
    for (size_t i = 0; i < pixels.size(); i += 4) {
        const auto x = (i / 4) % width;
        const int red = pixels[i], green = pixels[i + 1], blue = pixels[i + 2];
        if (pixels[i + 3] < 32 || green > std::max(red, blue)) {
            continue;
        }
        const int difference = reverse ? blue - red : red - blue;
        start += x < width / 3 && difference > 40;
        end += x > width * 2 / 3 && difference < -40;
    }
    return start > 12 && end > 12;
}

void gradientCache() {
    cybercanvas::Canvas canvas;
    std::vector<uint8_t> pixels(256 * 128 * 4);
    cybercanvas::FrameBuffer frame{256, 128, 256 * 4, pixels.size(), pixels.data(), 1};
    auto settings = cdsdk::modernSettings({});
    require(!cdsdk::widgetAppearance(settings).gradient, "Default fill changed from solid");
    cdsdk::WidgetAppearance appearance;
    appearance.surface = 0xF02030FF;
    appearance.gradientEnd = 0x2040F080;
    appearance.radius = 12;
    appearance.gradient = true;
    appearance.gradientAngle = 0;
    const auto render = [&] {
        canvas.render(frame, {}, [&](auto& draw, auto) {
            cdsdk::widgetBackground(draw, appearance, {0, 0, 256, 128});
        });
    };
    render();
    require(gradientEnds(pixels, 256, false), "Horizontal gradient endpoints did not reach pixels");
    require(pixels[(64 * 256 + 240) * 4 + 3] < pixels[(64 * 256 + 16) * 4 + 3],
            "Gradient endpoint transparency was lost");
    require(pixels[3] == 0, "Gradient ignored rounded corners");
    const auto stable = canvas.gradientCacheStats();
    const auto begin = std::chrono::steady_clock::now();
    for (int i = 0; i < 48; ++i) {
        render();
    }
    const auto elapsed = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - begin).count();
    const auto cached = canvas.gradientCacheStats();
    require(cached.brushCreates == stable.brushCreates && cached.stopCreates == stable.stopCreates,
            "Unchanged panel allocated new gradient resources");
    appearance.gradient = false;
    const auto solidBegin = std::chrono::steady_clock::now();
    for (int i = 0; i < 48; ++i) {
        render();
    }
    const auto solidElapsed =
        std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - solidBegin).count();
    std::cout << "CPU software render/export us per 256x128 panel: solid=" << solidElapsed / 48
              << ", gradient=" << elapsed / 48 << "; repeated gradient brush/stop allocations=0\n";
    appearance.gradient = true;
    appearance.gradientAngle = 90;
    render();
    require(pixels[(8 * 256 + 128) * 4] > pixels[(120 * 256 + 128) * 4], "Vertical gradient direction was ignored");
    appearance.background = false;
    render();
    require(alpha(pixels) == 0, "Hidden gradient retained background pixels");
    std::cout << "PASS shared gradient transparency, clipping, direction, cache and background toggle\n";
}

void gradientInstance(cdsdk::NativeInstance& first, cdsdk::NativeInstance& second, int columns, int rows) {
    cdsdk::Error error;
    apply(first, "background", CD_SETTING_COLOR, 0xF02030FF);
    apply(first, "background_gradient_end", CD_SETTING_COLOR, 0x2040F0FF);
    apply(first, "background_fill", CD_SETTING_CHOICE, 0, "linear");
    apply(first, "show_background", CD_SETTING_TOGGLE, 1);
    apply(first, "foreground", CD_SETTING_COLOR, 0x00FF00FF);
    apply(first, "secondary", CD_SETTING_COLOR, 0x00FF00FF);
    apply(first, "accent", CD_SETTING_COLOR, 0x00FF00FF);
    CdSettingValue untouched{};
    require(second.api.get_setting(second.api.instance, "background_fill", &untouched, sizeof(untouched)) == CD_OK &&
                std::strcmp(untouched.text, "solid") == 0,
            first.id + ": gradient selection leaked into another instance");
    for (const double dpi : {1., 1.5}) {
        CdLayout layout{};
        layout.struct_size = sizeof(layout);
        layout.logical_width = columns * 88;
        layout.logical_height = rows * 110;
        layout.device_pixel_ratio = dpi;
        layout.safe_content = {6, 6, layout.logical_width - 12, layout.logical_height - 12};
        first.api.layout_changed(first.api.instance, &layout);
        const auto width = uint32_t(layout.logical_width * dpi), height = uint32_t(layout.logical_height * dpi);
        std::vector<uint8_t> pixels;
        for (const int angle : {0, 180}) {
            apply(first, "background_gradient_angle", CD_SETTING_INTEGER, angle);
            require(first.render(width, height, dpi, &pixels, &error), first.id + ": gradient render");
            require(gradientEnds(pixels, width, angle == 180), first.id + ": gradient endpoint colors/direction");
        }
        apply(first, "background_fill", CD_SETTING_CHOICE, 0, "solid");
        require(first.render(width, height, dpi, &pixels, &error) && colored(pixels, 0xF02030FF) > 12,
                first.id + ": switching back to solid retained the gradient cache");
        apply(first, "background_fill", CD_SETTING_CHOICE, 0, "linear");
    }
}
}  // namespace

int main(int argc, char** argv) try {
    require(argc >= 4 && argc <= 6,
            "Usage: package-directory selection.tsv output-directory [expected-layouts] [--gradients-only]");
    const int expectedLayouts = argc >= 5 ? std::stoi(argv[4]) : 86;
    const bool gradientsOnly = argc == 6 && std::strcmp(argv[5], "--gradients-only") == 0;
    require(argc != 6 || gradientsOnly, "Unknown appearance test mode");
    if (gradientsOnly) {
        gradientCache();
    }
    require(expectedLayouts > 0, "Expected a positive number of layouts");
    const auto packages = cdsdk::utf8Path(argv[1]);
    const auto output = cdsdk::utf8Path(argv[3]);
    std::filesystem::create_directories(output);
    std::ifstream selection(cdsdk::utf8Path(argv[2]));
    require(bool(selection), "Open footprint selection");
    std::ofstream report(output / (gradientsOnly ? "gradient-results.tsv" : "appearance-results.tsv"));
    report << (gradientsOnly ? "id\tdpi\tgradient\n" : "id\tcell\tdpi\tbackgroundAlpha\ttransparentAlpha\n");
    std::set<std::string> exported;
    std::string id;
    int columns = 0, rows = 0, count = 0;
    while (selection >> id >> columns >> rows) {
        const auto directory = packages / id;
        cdsdk::Error error;
        cdsdk::pb::PluginDescriptor descriptor;
        if (!cdsdk::loadDirectoryDescriptor(directory, &descriptor, &error)) {
            throw std::runtime_error(id + ": " + error.message);
        }
        for (const auto* key : {"background_fill", "background_gradient_end", "background_gradient_angle"}) {
            const auto& fields = descriptor.settings_page().fields();
            require(std::any_of(fields.begin(), fields.end(),
                                [&](const auto& field) {
                                    return field.id() == key;
                                }),
                    id + ": missing gradient editor " + key);
        }
        cdsdk::NativeModule module;
        if (!module.open(directory, descriptor, &error)) {
            throw std::runtime_error(id + ": " + error.message);
        }
        cdsdk::NativeInstance first, second;
        first.host.fixedUnixMs = 1793759316000ull;
        first.host.extended.query_service = appearanceServices;
        second.host.extended.query_service = appearanceServices;
        modelConfigurations[&first.host] = modelConfigurations[&second.host] = 0;
        compositionPass[&first.host] = compositionPass[&second.host] = CD_COMPOSITION_ALL;
        if (!first.create(module, directory, "appearance-first", &error)) {
            throw std::runtime_error(id + ": " + error.message);
        }
        if (!second.create(module, directory, "appearance-second", &error)) {
            throw std::runtime_error(id + ": " + error.message);
        }
        first.host.tick(.2);
        first.pause(true);
        second.pause(true);
        for (const auto key :
             {"show_background", "background", "foreground", "secondary", "accent", "font_family",
              "background_corner_radius", "background_fill", "background_gradient_end", "background_gradient_angle"}) {
            CdSettingValue value{};
            require(first.api.get_setting(first.api.instance, key, &value, sizeof(value)) == CD_OK,
                    id + ": missing appearance setting " + key);
        }
        if (gradientsOnly) {
            gradientInstance(first, second, columns, rows);
            report << id << "\t1,1.5\tPASS\n";
            modelConfigurations.erase(&first.host);
            modelConfigurations.erase(&second.host);
            compositionBounds.erase(&first.host);
            compositionBounds.erase(&second.host);
            compositionPass.erase(&first.host);
            compositionPass.erase(&second.host);
            ++count;
            std::cout << "PASS gradient " << id << '\n';
            continue;
        }
        CdSettingValue initialBackground{}, initialForeground{};
        first.api.get_setting(first.api.instance, "background", &initialBackground, sizeof(initialBackground));
        first.api.get_setting(first.api.instance, "foreground", &initialForeground, sizeof(initialForeground));
        const CdSettingValue retiredColors[]{{sizeof(CdSettingValue), CD_SETTING_COLOR, "background", 0xF8F9F3FF},
                                             {sizeof(CdSettingValue), CD_SETTING_COLOR, "foreground", 0x252B24FF},
                                             {sizeof(CdSettingValue), CD_SETTING_COLOR, "secondary", 0x747B6CFF},
                                             {sizeof(CdSettingValue), CD_SETTING_COLOR, "accent", 0x8C9E76FF}};
        require(first.api.apply_settings(first.api.instance, retiredColors, 4) == CD_OK,
                id + ": rejected retired stock palette");
        CdSettingValue refreshed{};
        first.api.get_setting(first.api.instance, "background", &refreshed, sizeof(refreshed));
        require(refreshed.integer == initialBackground.integer, id + ": retained retired monochrome stock palette");
        if (id != "com.cyberdesktop.live2d" && id != "com.cyberdesktop.character-chat" && exported.insert(id).second) {
            CdLayout sampleLayout{};
            sampleLayout.struct_size = sizeof(sampleLayout);
            sampleLayout.logical_width = columns * 88;
            sampleLayout.logical_height = rows * 110;
            sampleLayout.device_pixel_ratio = 2;
            sampleLayout.safe_content = {6, 6, sampleLayout.logical_width - 12, sampleLayout.logical_height - 12};
            first.api.layout_changed(first.api.instance, &sampleLayout);
            std::vector<uint8_t> pixels;
            const auto width = uint32_t(sampleLayout.logical_width * 2),
                       height = uint32_t(sampleLayout.logical_height * 2);
            first.pause(false);
            bool imagesSettled = false;
            std::vector<cdsdk::DiagnosticSample> diagnostics;
            for (int warm = 0; warm < 180; ++warm) {
                first.host.tick(.25 + warm * .016);
                require(first.render(width, height, 2, &pixels, &error), id + ": native default sample");
                Sleep(8);
                if (warm >= 11) {
                    diagnostics.clear();
                    require(first.host.diagnostics.read(&diagnostics), id + ": read image diagnostics");
                    uint64_t pending = 0, failed = 0;
                    for (const auto& sample : diagnostics) {
                        pending += sample.stats.values[CD_DIAG_IMAGE_PENDING];
                        failed += sample.stats.values[CD_DIAG_IMAGE_FAILED];
                    }
                    require(failed == 0, id + ": default preview has failed image layers");
                    if (pending == 0) {
                        imagesSettled = true;
                        break;
                    }
                }
            }
            require(imagesSettled, id + ": default preview image layers did not settle");
            first.pause(true);

            if (id.find("clock.pixel") != std::string::npos || id.find("hourglass.pixel") != std::string::npos) {
                require(colored(pixels, uint32_t(initialForeground.integer)) > 80, id + ": missing lit digit pixels");
                const auto bg = uint32_t(initialBackground.integer), fg = uint32_t(initialForeground.integer);
                require(colored(pixels, cdsdk::mixColor(bg, fg, .08)) > 80,
                        id + ": inactive digit dots did not use the faint track color");
                int separation = 0;
                for (int shift : {24, 16, 8}) {
                    separation += std::abs(int((bg >> shift) & 255) - int((fg >> shift) & 255));
                }
                require(separation > 260, id + ": insufficient active/inactive digit contrast");
            }

            CdFrame frame{sizeof(frame), width, height, width * 4, pixels.size(), pixels.data(), 2};
            cdsdk::Canvas writer;
            require(writer.save((output / (id + ".png")).wstring(), frame), id + ": save native sample");
        }
        if (id.find("notes.") != std::string::npos) {
            apply(first, "text", CD_SETTING_TEXT, 0, "Plan 2026\nDesign 123");
        }
        apply(first, "background", CD_SETTING_COLOR, 0x204060FF);
        apply(first, "foreground", CD_SETTING_COLOR, 0xD64028FF);
        CdSettingValue untouched{};
        require(second.api.get_setting(second.api.instance, "background", &untouched, sizeof(untouched)) == CD_OK &&
                    untouched.integer == initialBackground.integer,
                id + ": appearance leaked into another instance");
        first.api.get_setting(first.api.instance, "foreground", &refreshed, sizeof(refreshed));
        require(refreshed.integer == 0xD64028FF, id + ": custom foreground was overwritten");
        for (const auto cell : {88, 80, 72}) {
            for (const auto dpi : {1., 1.5}) {
                const double width = columns * cell, height = rows * cell * 1.25;
                CdLayout layout{};
                layout.struct_size = sizeof(layout);
                std::vector<CdCell> cells;
                for (int row = 0; row < rows; ++row) {
                    for (int column = 0; column < columns; ++column) {
                        cells.push_back({column, row});
                    }
                }
                layout.cell_count = uint32_t(cells.size());
                layout.cells = cells.data();
                layout.logical_width = width;
                layout.logical_height = height;
                layout.device_pixel_ratio = dpi;
                layout.safe_content = {6, 6, width - 12, height - 12};
                layout.text_color = {37, 43, 36, 255};
                layout.accent_color = {140, 158, 118, 255};
                first.api.layout_changed(first.api.instance, &layout);
                apply(first, "show_background", CD_SETTING_TOGGLE, 1);
                std::vector<uint8_t> surface, transparent;
                require(first.render(uint32_t(width * dpi), uint32_t(height * dpi), dpi, &surface, &error),
                        id + ": surface render " + error.message);
                require(colored(surface, 0x204060FF) > 12, id + ": background color did not reach pixels");
                if (id == "com.cyberdesktop.live2d") {
                    const auto bounds = compositionBounds.at(&first.host);
                    require(
                        bounds.x == 6 && bounds.y == 6 && bounds.width == width - 12 && bounds.height == height - 12,
                        "Live2D did not configure the model inside the six-pixel safe bounds");
                    compositionPass[&first.host] = CD_COMPOSITION_BACKGROUND;
                    std::vector<uint8_t> backgroundLayer, foregroundLayer;
                    require(first.render(uint32_t(width * dpi), uint32_t(height * dpi), dpi, &backgroundLayer, &error),
                            "Live2D background layer failed");
                    compositionPass[&first.host] = CD_COMPOSITION_FOREGROUND;
                    require(first.render(uint32_t(width * dpi), uint32_t(height * dpi), dpi, &foregroundLayer, &error),
                            "Live2D foreground layer failed");
                    require(colored(backgroundLayer, 0x204060FF) > 12 && alpha(foregroundLayer) == 0,
                            "Live2D background would cover the GPU model in layered composition");
                    compositionPass[&first.host] = CD_COMPOSITION_ALL;
                }
                apply(first, "show_background", CD_SETTING_TOGGLE, 0);
                require(first.render(uint32_t(width * dpi), uint32_t(height * dpi), dpi, &transparent, &error),
                        id + ": transparent render " + error.message);
                require(alpha(surface) > alpha(transparent) + 255, id + ": background switch did not clear pixels");
                if (id.find("notes.") != std::string::npos || id.find("clock.digital.") != std::string::npos) {
                    require(colored(transparent, 0xD64028FF) > 3, id + ": foreground color did not reach text");
                    apply(first, "font_family", CD_SETTING_FONT, 0, "Consolas");
                    std::vector<uint8_t> changedFont;
                    require(first.render(uint32_t(width * dpi), uint32_t(height * dpi), dpi, &changedFont, &error),
                            id + ": configured font render");
                    require(changedFont != transparent, id + ": font change did not invalidate typography");
                    apply(first, "font_family", CD_SETTING_FONT, 0, "Segoe UI");
                }
                report << id << '\t' << cell << '\t' << dpi << '\t' << alpha(surface) << '\t' << alpha(transparent)
                       << '\n';
            }
        }
        ++count;
        if (id == "com.cyberdesktop.live2d") {
            require(modelConfigurations[&first.host] == 1, "Appearance changes restarted the model service");
        }
        modelConfigurations.erase(&first.host);
        modelConfigurations.erase(&second.host);
        compositionBounds.erase(&first.host);
        compositionBounds.erase(&second.host);
        compositionPass.erase(&first.host);
        compositionPass.erase(&second.host);
        std::cout << "PASS " << id << '\n';
    }
    require(count == expectedLayouts, "Selected footprint count differs from expected layouts");
    std::cout << "PASS " << count << " layouts, " << count * (gradientsOnly ? 2 : 6)
              << (gradientsOnly ? " DPI pairs with gradient direction, solid restoration and instance isolation\n"
                                : " size/DPI/background pairs and per-instance settings\n")
              << "Model, character image and chat services are offline appearance fixtures.\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
