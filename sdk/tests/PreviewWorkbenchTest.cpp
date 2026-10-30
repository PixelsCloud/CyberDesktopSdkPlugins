// Exercise the real preview window commands in a hidden window, including WIC
// export and module reload. No separate imitation of the UI action logic.
#define CD_PREVIEW_TEST
#include "sdk/cpp/Services.h"
#include "tools/Preview.cpp"

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
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        const auto output = std::filesystem::absolute(argv[2]);
        std::filesystem::create_directories(output);
        const auto parse = [](std::vector<std::wstring> arguments) {
            std::vector<wchar_t*> pointers;
            for (auto& argument : arguments) {
                pointers.push_back(argument.data());
            }
            return cdsdk::PreviewOptions::parse(int(pointers.size()), pointers.data());
        };
        check("CLI accepts named scenario and export directory",
              parse({L"preview", L"plugin", L"--scenario", L"empty", L"--export-dir", L"exports"}).scenario == "empty");
        bool rejected = false;
        try {
            parse({L"preview", L"plugin", L"--scenario", L"typo"});
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        check("CLI rejects unknown scenario", rejected);
        rejected = false;
        try {
            parse({L"preview", L"plugin", L"--services", L"none", L"--scenario", L"error"});
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        check("scenario cannot imply live or disabled data services", rejected);
        for (const auto& scene : cdsdk::previewScenarios) {
            cdsdk::MockHost host;
            cdsdk::PreviewOptions options;
            options.scenario = scene.id;
            options.configure(host);
            cdsdk::Services services(&host.api, &host);
            const CdLocationQuery query{sizeof(query), CD_LOCATION_DEFAULT};
            std::optional<cdsdk::WeatherSnapshot> value;
            auto weather = services.weather(query, [&](const auto& snapshot) {
                value = snapshot;
            });
            bool timerFired = false;
            uint64_t token = 0;
            cdsdk::MockHost::timer(
                &host, 1000,
                [](void* user, uint64_t) {
                    *static_cast<bool*>(user) = true;
                },
                &timerFired, &token);
            host.tick(0);
            const auto expected = scene.value == cdsdk::MockScenario::Normal  ? CD_DATA_READY
                                  : scene.value == cdsdk::MockScenario::Empty ? CD_DATA_UNAVAILABLE
                                                                              : CD_DATA_ERROR;
            check("scenario reaches weather protocol without stopping timers",
                  timerFired && weather.result() == CD_OK &&
                      (scene.value == cdsdk::MockScenario::Loading ? !value : value && value->status == expected));
            cdsdk::MockHost::unsubscribe(&host, token);
        }

        Preview preview;
        preview.root = std::filesystem::absolute(argv[1]);
        preview.options.exportDirectory = output / L"导出 images";
        preview.options.time = 1709208000000ull;
        preview.options.language = "zh-CN";
        preview.options.languageOverride = true;
        preview.width = 320;
        preview.height = 200;
        if (!preview.open()) {
            throw std::runtime_error(preview.failure);
        }
        const auto page = [&](const char* name) {
            const CdSettingValue setting{sizeof(setting), CD_SETTING_CHOICE, "page", 0, 0, name};
            if (preview.instance->api.apply_settings(preview.instance->api.instance, &setting, 1) != CD_OK) {
                throw std::runtime_error("Page selection failed");
            }
            preview.instance->host.tick(preview.seconds);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (!preview.render(true)) {
                if (!preview.failure.empty()) {
                    throw std::runtime_error("Page render failed: " + preview.failure);
                }
                if (std::chrono::steady_clock::now() >= deadline) {
                    throw std::runtime_error("Page images did not finish decoding");
                }
                // Keep the scenario's virtual clock fixed while the real image worker
                // completes. A pending asynchronous image is not a rendering failure.
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                preview.instance->host.tick(preview.seconds);
            }
        };
        for (const char* name : {"info", "media"}) {
            std::vector<std::vector<uint8_t>> captures;
            for (size_t i = 0; i < std::size(cdsdk::previewScenarios); ++i) {
                preview.selectScenario(i);
                page(name);
                captures.push_back(preview.pixels);
                preview.images.save(output / (std::string(name) + "-" + preview.options.scenario + ".png"),
                                    preview.width, preview.height, preview.pixels);
            }
            bool different = true;
            for (size_t i = 0; i < captures.size(); ++i) {
                for (size_t j = i + 1; j < captures.size(); ++j) {
                    different &= captures[i] != captures[j];
                }
            }
            check("normal loading empty and failure render distinct views", different);
        }
        preview.selectScenario(0);
        page("tasks");
        preview.instance->host.uiFocus(true);
        const CdUiEvent toggle{sizeof(toggle), CD_UI_ACTION, CD_UI_PRESS, 0, 1};
        preview.instance->host.uiCallback(preview.instance->host.uiUser, &toggle);
        cdsdk::Error error;
        const auto saved = preview.instance->state(&error);
        preview.selectScenario(3);
        check("scenario reload preserves task model", preview.instance->state(&error) == saved);
        preview.selectScenario(0);
        page("info");

        WNDCLASSW type{};
        type.lpfnWndProc = windowProc;
        type.hInstance = GetModuleHandleW(nullptr);
        type.lpszClassName = L"CyberDesktopPreviewWorkbenchTest";
        RegisterClassW(&type);
        HWND window = CreateWindowExW(0, type.lpszClassName, L"Hidden preview test", WS_OVERLAPPEDWINDOW, 0, 0, 400,
                                      300, nullptr, preview.menu(), type.hInstance, &preview);
        if (!window) {
            throw std::runtime_error("Hidden test window creation failed");
        }
        preview.dpr = 2;
        SendMessageW(window, WM_COMMAND, 201, 0);
        check("size menu applies logical dimensions at actual target DPR",
              preview.width == 640 && preview.height == 400);
        preview.caption();
        check("size menu marks current preset", (GetMenuState(preview.sizeMenu, 201, MF_BYCOMMAND) & MF_CHECKED) != 0);
        SendMessageW(window, WM_KEYDOWN, VK_F3, 0);
        check("F3 cycles common logical sizes", preview.width == 640 && preview.height == 640);
        SendMessageW(window, WM_COMMAND, 101, 0);
        check("scenario menu recreates loading subscriptions",
              preview.options.scenario == "loading" && preview.instance->host.scenario == cdsdk::MockScenario::Loading);
        SendMessageW(window, WM_KEYDOWN, VK_F2, 0);
        check("F2 cycles to empty data", preview.options.scenario == "empty");
        SendMessageW(window, WM_COMMAND, 100, 0);
        preview.instance->host.tick(preview.seconds);
        SendMessageW(window, WM_KEYDOWN, VK_F8, 0);
        SendMessageW(window, WM_COMMAND, 103, 0);
        check("scenario switching retains pause state", preview.paused && preview.instance->host.paused);
        SendMessageW(window, WM_KEYDOWN, VK_F8, 0);
        SendMessageW(window, WM_COMMAND, 100, 0);
        preview.instance->host.tick(preview.seconds);
        preview.render(true);
        preview.options.overlay = true;
        SendMessageW(window, WM_COMMAND, 300, 0);
        const auto first = preview.lastExport;
        check("export menu writes a PNG to a Unicode directory", !first.empty() && std::filesystem::exists(first));
        if (first.empty()) {
            throw std::runtime_error(preview.failure);
        }
        const auto exported = preview.images.load(first);
        const cdsdk::PreviewImage raw{preview.width, preview.height, preview.pixels};
        check("export excludes debug overlay and matches raw plugin pixels",
              cdsdk::compareImages(exported, raw, 1).differing == 0);
        bool transparent = false;
        for (size_t i = 3; i < exported.rgba.size(); i += 4) {
            transparent |= exported.rgba[i] == 0;
        }
        check("PNG export preserves transparency", transparent && exported.width == 640 && exported.height == 640);
        SendMessageW(window, WM_KEYDOWN, VK_F9, 0);
        check("repeated export does not overwrite earlier image",
              preview.lastExport != first && std::filesystem::exists(first));
        preview.options.exportDirectory = preview.root / "exports";
        rejected = false;
        try {
            preview.exportPng();
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        check("export rejects watched package paths before writing",
              rejected && !std::filesystem::exists(preview.root / "exports"));
        preview.options.services = "none";
        preview.open(true);
        check("scenario menu is disabled without mock services",
              (GetMenuState(preview.scenarioMenu, 101, MF_BYCOMMAND) & MF_GRAYED) != 0);
        DestroyWindow(window);
        std::cout << "RESULT " << failures << " failures\n";
        return failures ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
