#include <iostream>
#include <thread>

#include "tools/PreviewImages.h"
#include "tools/Runtime.h"

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) {
        return 2;
    }
    int failures = 0;
    const auto check = [&](const char* name, bool passed) {
        std::cout << (passed ? "PASS " : "FAIL ") << name << '\n';
        failures += !passed;
    };
    try {
        const std::filesystem::path root = argv[1], output = argv[2];
        std::filesystem::create_directories(output);
        cdsdk::Error error;
        cdsdk::pb::PluginDescriptor descriptor;
        if (!cdsdk::loadDirectoryDescriptor(root, &descriptor, &error)) {
            throw std::runtime_error(error.message);
        }
        cdsdk::NativeModule module;
        if (!module.open(root, descriptor, &error)) {
            throw std::runtime_error(error.message);
        }
        cdsdk::NativeInstance instance;
        if (!instance.create(module, root, "gallery-regression", &error)) {
            throw std::runtime_error(error.message);
        }
        std::vector<uint8_t> pixels;
        cdsdk::PreviewImages images;
        double seconds = 0;
        const auto render = [&](double dpi = 1) {
            if (!instance.render(uint32_t(384 * dpi), uint32_t(384 * dpi), dpi, &pixels, &error)) {
                throw std::runtime_error(error.message);
            }
        };
        render();
        instance.host.cpuPercent = 37;
        for (int i = 0; i < 150; ++i) {
            seconds += 1. / 60;
            instance.host.tick(seconds);
            render();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const auto animated = pixels;
        const CdSettingValue stop{sizeof(stop), CD_SETTING_TOGGLE, "animate", 0};
        check("animation setting applies", instance.api.apply_settings(instance.api.instance, &stop, 1) == CD_OK);
        for (int i = 0; i < 100; ++i) {
            seconds += 1. / 60;
            instance.host.tick(seconds);
            render();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check("static gallery retains data polling but no decorative timer",
              instance.host.subscriptions.size() == 1 && !instance.host.subscriptions.begin()->second.timer);
        const auto paused = pixels;
        render();
        check("unchanged frame is pixel stable", pixels == paused);
        check("timeline and decoded art changed visible output", animated != paused);
        instance.input({sizeof(CdInput), CD_INPUT_PRESS, CD_BUTTON_LEFT, 0, 160, 285});
        instance.input({sizeof(CdInput), CD_INPUT_RELEASE, CD_BUTTON_LEFT, 0, 160, 285});
        instance.host.uiFocus(true);
        check("chart accepts keyboard navigation",
              instance.host.uiKey(CD_KEY_HOME) && instance.host.uiKey(CD_KEY_RIGHT));
        for (const char* style : {"bars", "donut", "heatmap"}) {
            const CdSettingValue setting{sizeof(setting), CD_SETTING_CHOICE, "chart", 0, 0, style};
            check("chart setting applies", instance.api.apply_settings(instance.api.instance, &setting, 1) == CD_OK);
            for (const auto theme : {CD_ENV_THEME_DARK, CD_ENV_THEME_LIGHT}) {
                instance.host.theme = theme;
                for (const auto size : std::array<std::pair<uint32_t, uint32_t>, 5>{
                         {{256, 256}, {384, 384}, {576, 576}, {192, 384}, {384, 192}}}) {
                    for (double dpi : {1., 1.25, 1.5, 1.75, 2., 3.}) {
                        const CdLayout layout{sizeof(layout),
                                              0,
                                              0,
                                              0,
                                              nullptr,
                                              double(size.first),
                                              double(size.second),
                                              dpi,
                                              {0, 0, double(size.first), double(size.second)}};
                        instance.api.layout_changed(instance.api.instance, &layout);
                        const auto width = uint32_t(size.first * dpi), height = uint32_t(size.second * dpi);
                        if (!instance.render(width, height, dpi, &pixels, &error)) {
                            throw std::runtime_error(error.message);
                        }
                        if (dpi == 1.5) {
                            for (int frame = 0; frame < 30; ++frame) {
                                seconds += 1. / 60;
                                instance.host.tick(seconds);
                                if (!instance.render(width, height, dpi, &pixels, &error)) {
                                    throw std::runtime_error(error.message);
                                }
                                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                            }
                            const auto name = std::string(style) + (theme == CD_ENV_THEME_DARK ? "-dark-" : "-light-") +
                                              std::to_string(size.first) + "x" + std::to_string(size.second) + ".png";
                            images.save(output / name, width, height, pixels);
                        }
                    }
                }
            }
        }
        check("180 chart/theme/DPI/size renders", true);
        instance.pause(true);
        check("hidden gallery removes all subscriptions", instance.host.subscriptions.empty());
        instance.host.tick(seconds + 10000);
        instance.pause(false);
        render();
        check("resume restores data subscription", !instance.host.subscriptions.empty());
        const auto saved = instance.state(&error);
        check("gallery state round trip",
              instance.api.restore_state(instance.api.instance, reinterpret_cast<const uint8_t*>(saved.data()),
                                         saved.size()) == CD_OK);
        instance.api.destroy(instance.api.instance);
        instance.api.instance = nullptr;
        check("gallery unload releases registrations",
              instance.host.subscriptions.empty() && instance.host.diagnostics.empty() && !instance.host.uiCallback);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return failures ? 1 : 0;
}
