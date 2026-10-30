#include <iostream>

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
        if (!instance.create(module, root, "physics-regression", &error)) {
            throw std::runtime_error(error.message);
        }
        CdLayout layout{sizeof(layout), 0, 0, 0, nullptr, 400, 400, 1, {17, 29, 360, 350}};
        instance.api.layout_changed(instance.api.instance, &layout);
        std::vector<uint8_t> pixels;
        const auto render = [&](double dpi = 1) {
            if (!instance.render(uint32_t(400 * dpi), uint32_t(400 * dpi), dpi, &pixels, &error)) {
                throw std::runtime_error(error.message);
            }
        };
        const auto ball = [&] {
            std::vector<cdsdk::DiagnosticSample> samples;
            if (!instance.host.diagnostics.read(&samples)) {
                throw std::runtime_error("Physics diagnostic read failed");
            }
            for (const auto& sample : samples) {
                for (const auto& node : sample.nodes) {
                    if (std::string_view(node.name) == "Ball 1") {
                        return node.bounds;
                    }
                }
            }
            throw std::runtime_error("Missing ball visual");
        };
        render();
        instance.input({sizeof(CdInput), CD_INPUT_MOVE, 0, 0, 17, 29});
        const auto initial = ball();
        instance.input({sizeof(CdInput), CD_INPUT_PRESS, CD_BUTTON_LEFT, 0, initial.x + initial.width / 2,
                        initial.y + initial.height / 2});
        check("safe-content offset preserves host drag capture", instance.host.captured);
        instance.input({sizeof(CdInput), CD_INPUT_MOVE, CD_BUTTON_LEFT, 0, 270, 180});
        for (int i = 0; i <= 120; ++i) {
            instance.host.tick(i / 60.);
        }
        render();
        const auto dragged = ball();
        check("dragged visual reaches cursor",
              std::abs(dragged.x + dragged.width / 2 - 270) < 5 && std::abs(dragged.y + dragged.height / 2 - 180) < 12);
        instance.input({sizeof(CdInput), CD_INPUT_CANCEL});
        check("cancel releases host capture", !instance.host.captured);
        for (int i = 121; i <= 3600; ++i) {
            instance.host.tick(i / 60.);
        }
        render();
        check("settled world removes animation timer", instance.host.subscriptions.empty());
        const auto settled = ball();
        render(2);
        const auto highDpi = ball();
        check("DPI change preserves logical body pose",
              std::abs(settled.x - highDpi.x) < .6 && std::abs(settled.y - highDpi.y) < .6);
        instance.pause(true);
        check("hidden instance has no timer or capture",
              instance.host.subscriptions.empty() && !instance.host.captured);
        instance.host.tick(36000);
        instance.pause(false);
        render();
        check("hidden duration does not advance physics", std::abs(settled.y - ball().y) < .6);
        const CdSettingValue reduced{sizeof(reduced), CD_SETTING_TOGGLE, "reduced_motion", 1};
        check("reduced motion applies", instance.api.apply_settings(instance.api.instance, &reduced, 1) == CD_OK);
        instance.host.tick(36001);
        check("reduced motion stops decorative timer", instance.host.subscriptions.empty());
        const auto saved = instance.state(&error);
        check("versioned state restores",
              instance.api.restore_state(instance.api.instance, reinterpret_cast<const uint8_t*>(saved.data()),
                                         saved.size()) == CD_OK);
        render();
        cdsdk::PreviewImages images;
        images.save(output / "settled.png", 400, 400, pixels);
        const CdSettingValue springSettings[]{{sizeof(CdSettingValue), CD_SETTING_INTEGER, "count", 1},
                                              {sizeof(CdSettingValue), CD_SETTING_TOGGLE, "spring", 1},
                                              {sizeof(CdSettingValue), CD_SETTING_TOGGLE, "reduced_motion", 0}};
        check("spring mode applies", instance.api.apply_settings(instance.api.instance, springSettings,
                                                                 uint32_t(std::size(springSettings))) == CD_OK);
        render();
        const auto suspendedStart = ball();
        bool springMoved = false;
        for (int i = 0; i <= 1800; ++i) {
            instance.host.tick(36002 + i / 60.);
            if (i == 60) {
                render();
                springMoved = std::hypot(ball().x - suspendedStart.x, ball().y - suspendedStart.y) > 2;
            }
        }
        render();
        const auto suspended = ball();
        check("spring oscillates then holds the ball above the floor",
              springMoved && suspended.y + suspended.height < layout.safe_content.y + layout.safe_content.height * .65);
        check("settled spring stops decorative updates", instance.host.subscriptions.empty());
        images.save(output / "spring-settled.png", 400, 400, pixels);
        instance.input({sizeof(CdInput), CD_INPUT_PRESS, CD_BUTTON_LEFT, 0, suspended.x + suspended.width / 2,
                        suspended.y + suspended.height / 2});
        check("suspended ball can acquire drag capture", instance.host.captured);
        const CdSettingValue noSpring{sizeof(noSpring), CD_SETTING_TOGGLE, "spring", 0};
        check("spring mode can be disabled", instance.api.apply_settings(instance.api.instance, &noSpring, 1) == CD_OK);
        render();
        check("mode replacement releases the old mouse joint and capture", !instance.host.captured);
        for (int i = 0; i <= 600; ++i) {
            instance.host.tick(36033 + i / 60.);
        }
        render();
        check("removing suspension restores free fall", ball().y > suspended.y + 40);
        const CdSettingValue matrixSettings[]{{sizeof(CdSettingValue), CD_SETTING_INTEGER, "count", 12},
                                              {sizeof(CdSettingValue), CD_SETTING_TOGGLE, "reduced_motion", 1}};
        check("matrix settings restored", instance.api.apply_settings(instance.api.instance, matrixSettings,
                                                                      uint32_t(std::size(matrixSettings))) == CD_OK);
        for (const bool springMode : {false, true}) {
            const CdSettingValue mode{sizeof(mode), CD_SETTING_TOGGLE, "spring", int64_t(springMode)};
            if (instance.api.apply_settings(instance.api.instance, &mode, 1) != CD_OK) {
                throw std::runtime_error("Matrix spring mode failed");
            }
            for (const auto theme : {CD_ENV_THEME_DARK, CD_ENV_THEME_LIGHT}) {
                instance.host.theme = theme;
                for (const auto size : std::array<std::pair<uint32_t, uint32_t>, 5>{
                         {{256, 256}, {384, 384}, {576, 576}, {192, 384}, {384, 192}}}) {
                    for (double dpi : {1., 1.25, 1.5, 1.75, 2., 3.}) {
                        const CdLayout resized{sizeof(resized),
                                               0,
                                               0,
                                               0,
                                               nullptr,
                                               double(size.first),
                                               double(size.second),
                                               dpi,
                                               {0, 0, double(size.first), double(size.second)}};
                        instance.api.layout_changed(instance.api.instance, &resized);
                        const auto width = uint32_t(size.first * dpi), height = uint32_t(size.second * dpi);
                        if (!instance.render(width, height, dpi, &pixels, &error)) {
                            throw std::runtime_error(error.message);
                        }
                        if (dpi == 1.5) {
                            const auto name = std::string(springMode ? "spring-" : "") +
                                              (theme == CD_ENV_THEME_DARK ? "dark-" : "light-") +
                                              std::to_string(size.first) + "x" + std::to_string(size.second) + ".png";
                            images.save(output / name, width, height, pixels);
                        }
                    }
                }
            }
        }
        check("120 physical scene spring theme DPI and size renders", true);
        instance.api.destroy(instance.api.instance);
        instance.api.instance = nullptr;
        check("unload releases timers keyboard diagnostics", instance.host.subscriptions.empty() &&
                                                                 instance.host.diagnostics.empty() &&
                                                                 !instance.host.uiCallback && !instance.host.captured);
        for (int i = 0; i < 100; ++i) {
            cdsdk::NativeInstance repeated;
            if (!repeated.create(module, root, "physics-cycle", &error)) {
                throw std::runtime_error(error.message);
            }
            const CdSettingValue spring{sizeof(spring), CD_SETTING_TOGGLE, "spring", i % 2};
            if (repeated.api.apply_settings(repeated.api.instance, &spring, 1) != CD_OK) {
                throw std::runtime_error("Repeated spring settings failed");
            }
            repeated.api.layout_changed(repeated.api.instance, &layout);
            if (!repeated.render(400, 400, 1, &pixels, &error)) {
                throw std::runtime_error(error.message);
            }
            repeated.pause(true);
            repeated.api.destroy(repeated.api.instance);
            repeated.api.instance = nullptr;
            if (!repeated.host.subscriptions.empty() || !repeated.host.diagnostics.empty() ||
                repeated.host.uiCallback) {
                throw std::runtime_error("Repeated teardown leaked a host registration");
            }
        }
        check("100 create render pause destroy cycles", true);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return failures ? 1 : 0;
}
