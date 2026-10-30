#include <iostream>

#include "tools/NativeSettings.h"
#include "tools/PreviewImages.h"
#include "tools/PreviewOptions.h"
#include "tools/PreviewOverlay.h"
#include "tools/PreviewReplay.h"
#include "tools/PreviewServices.h"
#include "tools/PreviewState.h"
#include "tools/ResourceWatcher.h"

namespace {
struct Preview {
    std::filesystem::path root;
    cdsdk::PreviewOptions options;
    std::unique_ptr<cdsdk::NativeModule> module;
    std::unique_ptr<cdsdk::NativeInstance> instance;
    cdsdk::pb::PluginDescriptor descriptor;
    std::optional<cdsdk::pb::PreviewState> saved;
    std::unique_ptr<cdsdk::ResourceWatcher> watcher;
    cybercanvas::Canvas overlayCanvas;
    cdsdk::PreviewImages images;
    std::vector<CdCell> cells;
    std::vector<cdsdk::DiagnosticSample> lastDiagnostics;
    std::vector<uint8_t> pixels, shown, bgra;
    uint32_t width = 288, height = 288, imageWidth = 0, imageHeight = 0;
    uint64_t reloads = 0;
    double seconds = 0, dpr = 1;
    bool paused = false, windowFocused = false;
    int layoutIndex = 0;
    HWND window = nullptr;
    HMENU scenarioMenu = nullptr, sizeMenu = nullptr;
    HANDLE stopEvent = nullptr;
    bool stopRequested = false, settingsOpen = false;
    std::string failure, lastState;
    std::vector<std::vector<std::pair<double, double>>> polygons;
    std::chrono::steady_clock::time_point lastSave;
    std::filesystem::path lastExport;

    bool chinese() const {
        return (options.languageOverride ? options.language : cdsdk::Environment::systemSnapshot().locale)
            .starts_with("zh");
    }

    void caption() {
        if (!window) {
            return;
        }
        const auto index = cdsdk::previewScenarioIndex(options.scenario);
        const auto& scene = cdsdk::previewScenarios[index];
        const auto title = std::wstring(chinese() ? L"插件预览 — " : L"SDK preview — ") +
                           std::to_wstring(uint32_t(std::lround(width / dpr))) + L" × " +
                           std::to_wstring(uint32_t(std::lround(height / dpr))) + L" — " +
                           (chinese() ? scene.chinese : scene.english) +
                           (paused ? (chinese() ? L" — 已暂停" : L" — paused") : L"");
        SetWindowTextW(window, title.c_str());
        if (scenarioMenu) {
            CheckMenuRadioItem(scenarioMenu, 100, 103, UINT(100 + index), MF_BYCOMMAND);
            for (UINT i = 0; i < std::size(cdsdk::previewScenarios); ++i) {
                EnableMenuItem(scenarioMenu, 100 + i,
                               MF_BYCOMMAND | (options.services == "mock" ? MF_ENABLED : MF_GRAYED));
            }
        }
        if (sizeMenu) {
            for (UINT i = 0; i < std::size(cdsdk::previewSizes); ++i) {
                const auto& size = cdsdk::previewSizes[i];
                const bool selected = width == uint32_t(std::lround(size.width * dpr)) &&
                                      height == uint32_t(std::lround(size.height * dpr));
                CheckMenuItem(sizeMenu, 200 + i, MF_BYCOMMAND | (selected ? MF_CHECKED : MF_UNCHECKED));
            }
        }
    }

    HMENU menu() {
        const auto rootMenu = CreateMenu(), actions = CreatePopupMenu();
        scenarioMenu = CreatePopupMenu();
        sizeMenu = CreatePopupMenu();
        if (!rootMenu || !actions || !scenarioMenu || !sizeMenu) {
            for (const auto item : {rootMenu, actions, scenarioMenu, sizeMenu}) {
                if (item) {
                    DestroyMenu(item);
                }
            }
            scenarioMenu = sizeMenu = nullptr;
            throw std::runtime_error("Cannot create preview menu");
        }
        for (UINT i = 0; i < std::size(cdsdk::previewScenarios); ++i) {
            const auto& scene = cdsdk::previewScenarios[i];
            AppendMenuW(scenarioMenu, MF_STRING, 100 + i, chinese() ? scene.chinese : scene.english);
        }
        for (UINT i = 0; i < std::size(cdsdk::previewSizes); ++i) {
            AppendMenuW(sizeMenu, MF_STRING, 200 + i, cdsdk::previewSizes[i].label);
        }
        AppendMenuW(actions, MF_POPUP, UINT_PTR(scenarioMenu),
                    chinese() ? L"数据场景（F2 切换）" : L"Service scenario (F2)");
        AppendMenuW(actions, MF_POPUP, UINT_PTR(sizeMenu), chinese() ? L"逻辑尺寸（F3 切换）" : L"Logical size (F3)");
        AppendMenuW(actions, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(actions, MF_STRING, 300, chinese() ? L"导出透明 PNG\tF9" : L"Export transparent PNG\tF9");
        AppendMenuW(actions, MF_STRING, 301, chinese() ? L"插件设置\tF6" : L"Plugin settings\tF6");
        AppendMenuW(actions, MF_STRING, 302, chinese() ? L"暂停 / 继续\tF8" : L"Pause / resume\tF8");
        AppendMenuW(actions, MF_STRING, 303, chinese() ? L"调试边界\tF1" : L"Debug bounds\tF1");
        AppendMenuW(rootMenu, MF_POPUP, UINT_PTR(actions), chinese() ? L"预览" : L"Preview");
        return rootMenu;
    }

    void selectSize(size_t index) {
        const auto size = cdsdk::previewSizes[index % std::size(cdsdk::previewSizes)];
        const auto nextWidth = LONG(std::lround(size.width * dpr)), nextHeight = LONG(std::lround(size.height * dpr));
        if (window) {
            RECT rect{0, 0, nextWidth, nextHeight};
            AdjustWindowRectExForDpi(&rect, DWORD(GetWindowLongPtrW(window, GWL_STYLE)), GetMenu(window) != nullptr,
                                     DWORD(GetWindowLongPtrW(window, GWL_EXSTYLE)), GetDpiForWindow(window));
            SetWindowPos(window, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        } else {
            width = nextWidth;
            height = nextHeight;
            layout();
        }
        options.logicalWidth = size.width;
        options.logicalHeight = size.height;
        caption();
    }

    void nextSize() {
        for (size_t i = 0; i < std::size(cdsdk::previewSizes); ++i) {
            const auto& size = cdsdk::previewSizes[i];
            if (width == uint32_t(std::lround(size.width * dpr)) &&
                height == uint32_t(std::lround(size.height * dpr))) {
                selectSize(i + 1);
                return;
            }
        }
        selectSize(0);
    }

    void selectScenario(size_t index) {
        if (options.services != "mock") {
            return;
        }
        options.scenario = cdsdk::previewScenarios[index % std::size(cdsdk::previewScenarios)].id;
        // Recreate subscriptions so error/loading states are reproducible, preserving
        // plugin settings, business state, layout, dimensions and pause status.
        open(true);
    }

    std::filesystem::path exportPng() {
        if (!instance) {
            throw std::runtime_error("No loaded plugin to export");
        }
        auto directory = options.exportDirectory.empty() ? root.parent_path() / "preview-exports" / descriptor.id()
                                                         : options.exportDirectory;
        directory = std::filesystem::weakly_canonical(std::filesystem::absolute(directory));
        const auto package = std::filesystem::weakly_canonical(root);
        for (auto ancestor = directory; !ancestor.empty();) {
            if (CompareStringOrdinal(ancestor.c_str(), -1, package.c_str(), -1, TRUE) == CSTR_EQUAL) {
                throw std::invalid_argument("Export outside the watched plugin directory; use --export-dir");
            }
            const auto parent = ancestor.parent_path();
            if (parent == ancestor) {
                break;
            }
            ancestor = parent;
        }
        if (!render(true)) {
            throw std::runtime_error("Preview is not ready to export");
        }
        auto content = pixels;
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                if (!inside(x + .5, y + .5)) {
                    std::fill_n(content.begin() + (size_t(y) * width + x) * 4, 4, uint8_t(0));
                }
            }
        }
        std::filesystem::create_directories(directory);
        const auto stamp =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
                .count();
        const auto stem = descriptor.id() + "-" + std::to_string(width) + "x" + std::to_string(height) + "-" +
                          (options.theme == CD_ENV_THEME_LIGHT ? "light" : "dark") + "-" + options.scenario + "-" +
                          std::to_string(stamp);
        auto destination = directory / (stem + ".png");
        for (uint32_t suffix = 1; std::filesystem::exists(destination); ++suffix) {
            destination = directory / (stem + "-" + std::to_string(suffix) + ".png");
        }
        images.save(destination, width, height, content);
        lastExport = destination;
        std::cout << "EXPORTED " << cdsdk::utf8Argument(destination.c_str()) << '\n' << std::flush;
        if (window) {
            SetWindowTextW(window, ((chinese() ? L"已导出 — " : L"Exported — ") + destination.wstring()).c_str());
        }
        return destination;
    }

    ~Preview() {
        instance.reset();
        module.reset();
        if (stopEvent) {
            CloseHandle(stopEvent);
        }
    }

    void report(std::string message) {
        failure = std::move(message);
        std::cerr << failure << '\n';
        if (window) {
            const auto title = cdsdk::settingsWide(failure);
            SetWindowTextW(window, (L"SDK preview — " + title).c_str());
        }
    }

    bool open(bool reload = false) {
        if (settingsOpen) {
            return false;
        }
        try {
            if (reload && instance) {
                saved = cdsdk::capturePreviewState(*instance, descriptor);
                save();
            }
            instance.reset();
            module.reset();
            cdsdk::Error error;
            cdsdk::pb::PluginDescriptor next;
            if (!cdsdk::loadDirectoryDescriptor(root, &next, &error)) {
                throw std::runtime_error(cdsdk::errorText(error));
            }
            if (reload && next.id() != descriptor.id()) {
                throw std::runtime_error("A running preview cannot change plugin identity");
            }
            descriptor = std::move(next);
            layoutIndex = 0;
            if (!options.layout.empty()) {
                bool found = false;
                for (int i = 0; i < descriptor.layouts_size(); ++i) {
                    if (descriptor.layouts(i).id() == options.layout) {
                        layoutIndex = i;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    throw std::invalid_argument("Unknown preview layout");
                }
            }
            module = std::make_unique<cdsdk::NativeModule>();
            if (!module->open(root, descriptor, &error)) {
                throw std::runtime_error(cdsdk::errorText(error));
            }
            instance = std::make_unique<cdsdk::NativeInstance>();
            options.configure(instance->host);
            instance->host.virtualTime = seconds;
            if (!instance->create(*module, root, "preview-1", &error)) {
                throw std::runtime_error(cdsdk::errorText(error));
            }
            instance->host.requestKeyboardFocus = [this](bool focused) {
                if (window && focused) {
                    SetFocus(window);
                }
                instance->host.uiFocus(focused && (window ? GetFocus() == window : windowFocused));
            };
            if (saved) {
                cdsdk::restorePreviewState(*instance, descriptor, *saved);
            }
            for (const auto& field : descriptor.settings_page().fields()) {
                if (field.id() != "language" || !options.languageOverride) {
                    continue;
                }
                auto language = options.language;
                if (field.type() == cdsdk::pb::CHOICE) {
                    const auto supported = [&](const std::string& tag) {
                        return std::any_of(field.choices().begin(), field.choices().end(), [&](const auto& choice) {
                            return choice.value() == tag;
                        });
                    };
                    if (!supported(language)) {
                        language = language.starts_with("zh") ? "zh-CN" : "en";
                    }
                    if (!supported(language)) {
                        continue;
                    }
                }
                const CdSettingValue value{sizeof(value), uint32_t(field.type()), "language", 0, 0, language.c_str()};
                if (instance->api.apply_settings(instance->api.instance, &value, 1) != CD_OK) {
                    throw std::runtime_error("Plugin rejected preview language");
                }
            }
            layout();
            instance->host.uiFocus(windowFocused);
            if (paused) {
                instance->pause(true);
            }
            failure.clear();
            if (reload) {
                ++reloads;
                std::cout << "RELOADED " << reloads << " state restored\n" << std::flush;
            }
            caption();
            return true;
        } catch (const std::exception& error) {
            instance.reset();
            module.reset();
            report(error.what());
            return false;
        }
    }

    void save() {
        if (instance) {
            saved = cdsdk::capturePreviewState(*instance, descriptor);
        }
        if (saved && !options.state.empty()) {
            const auto bytes = saved->SerializeAsString();
            if (bytes != lastState) {
                cdsdk::writePreviewState(options.state, *saved);
                lastState = bytes;
            }
        }
    }

    bool inside(double x, double y) const {
        for (const auto& polygon : polygons) {
            bool hit = false;
            for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
                const auto [xi, yi] = polygon[i];
                const auto [xj, yj] = polygon[j];
                if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) {
                    hit = !hit;
                }
            }
            if (hit) {
                return true;
            }
        }
        return false;
    }

    void geometry() {
        cells.clear();
        polygons.clear();
        double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
        const auto& selected = descriptor.layouts(layoutIndex);
        for (const auto& cell : selected.cells()) {
            cells.push_back({cell.a(), cell.b()});
            std::vector<std::pair<double, double>> polygon;
            if (selected.topology() == cdsdk::pb::HEX) {
                const double cx = std::sqrt(3.) * (cell.a() + cell.b() * .5), cy = 1.5 * cell.b();
                for (int i = 0; i < 6; ++i) {
                    const double angle = (i * 60 - 30) * 3.141592653589793 / 180;
                    polygon.emplace_back(cx + std::cos(angle), cy + std::sin(angle));
                }
            } else {
                const double a = cell.a(), b = cell.b();
                polygon = {{a, b}, {a + 1, b}, {a + 1, b + 1}, {a, b + 1}};
            }
            for (auto [x, y] : polygon) {
                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
                minY = std::min(minY, y);
                maxY = std::max(maxY, y);
            }
            polygons.push_back(std::move(polygon));
        }
        const bool rectangle = selected.topology() == cdsdk::pb::RECT;
        const double scale = std::max(1., std::min((width - 24.) / (maxX - minX), (height - 24.) / (maxY - minY)));
        const double sx = rectangle ? width / (maxX - minX) : scale, sy = rectangle ? height / (maxY - minY) : scale;
        const double ox = rectangle ? 0 : (width - (maxX - minX) * scale) * .5,
                     oy = rectangle ? 0 : (height - (maxY - minY) * scale) * .5;
        for (auto& polygon : polygons) {
            for (auto& point : polygon) {
                point.first = ox + (point.first - minX) * sx;
                point.second = oy + (point.second - minY) * sy;
            }
        }
    }

    void layout() {
        if (!instance) {
            return;
        }
        geometry();
        CdLayout layout{sizeof(layout),
                        uint32_t(descriptor.layouts(layoutIndex).topology()),
                        0,
                        uint32_t(cells.size()),
                        cells.data(),
                        width / dpr,
                        height / dpr,
                        dpr,
                        {0, 0, width / dpr, height / dpr},
                        options.theme == CD_ENV_THEME_LIGHT ? CdColor{30, 40, 50, 255} : CdColor{235, 240, 245, 255},
                        {70, 200, 170, 255}};
        instance->host.preview->placement(window, dpr);
        instance->api.layout_changed(instance->api.instance, &layout);
        instance->host.dirty = true;
    }

    bool render(bool force = false) {
        if (!instance || (!force && !instance->host.dirty)) {
            return false;
        }
        cdsdk::Error error;
        std::vector<uint8_t> next;
        instance->host.dirty = false;
        if (!instance->render(width, height, dpr, &next, &error)) {
            report(cdsdk::errorText(error));
            return false;
        }
        std::vector<cdsdk::DiagnosticSample> samples;
        if (!cdsdk::sampleDiagnostics(instance->host, &samples, &error)) {
            report(cdsdk::errorText(error));
            return false;
        }
        for (const auto& sample : samples) {
            for (const auto& node : sample.nodes) {
                if (node.error[0]) {
                    const std::string message = std::string(node.resource) + ": " + node.error;
                    if (failure != message) {
                        report(message);
                    }
                    return false;
                }
            }
            if (sample.stats.values[CD_DIAG_IMAGE_PENDING] && !bgra.empty()) {
                return false;
            }
        }
        pixels = std::move(next);
        lastDiagnostics = std::move(samples);
        compose();
        return true;
    }

    void compose() {
        if (pixels.size() != size_t(width) * height * 4) {
            return;
        }
        shown = pixels;
        if (options.overlay) {
            cdsdk::previewOverlay(overlayCanvas, lastDiagnostics, width, height, dpr, shown);
        }
        imageWidth = width;
        imageHeight = height;
        bgra.resize(shown.size());
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                const size_t i = (size_t(y) * width + x) * 4;
                const bool tile = inside(x + .5, y + .5);
                const uint32_t alpha = tile ? shown[i + 3] : 0;
                const uint32_t background =
                    tile ? (options.theme == CD_ENV_THEME_LIGHT ? 215 : 54) + (((x / 16 + y / 16) % 2) ? 10 : 0) : 28;
                for (int c = 0; c < 3; ++c) {
                    bgra[i + c] = uint8_t(
                        std::min(255u, (tile ? uint32_t(shown[i + 2 - c]) : 0u) + background * (255 - alpha) / 255));
                }
                bgra[i + 3] = 255;
                if (!tile) {
                    for (int c = 0; c < 4; ++c) {
                        shown[i + c] = 0;
                    }
                }
            }
        }
    }

    bool step() {
        if (stopEvent && WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0) {
            stopRequested = true;
            return false;
        }
        if (watcher && !settingsOpen) {
            const auto changes = watcher->drain();
            if (changes.changed) {
                if (!changes.error.empty()) {
                    report(changes.error);
                }
                open(true);
            }
        }
        bool changed = false;
        if (instance) {
            seconds += 1. / 60;
            instance->host.tick(seconds);
            changed = render();
        }
        if (std::chrono::steady_clock::now() - lastSave > std::chrono::milliseconds(500)) {
            save();
            lastSave = std::chrono::steady_clock::now();
        }
        return changed;
    }
};

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto* p = reinterpret_cast<Preview*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        p = static_cast<Preview*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        p->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(p));
    }
    if (!p) {
        return DefWindowProcW(window, message, w, l);
    }
    try {
        if (message == WM_SIZE) {
            p->width = std::clamp<uint32_t>(LOWORD(l), 1, 4096);
            p->height = std::clamp<uint32_t>(HIWORD(l), 1, 4096);
            p->layout();
            p->caption();
            return 0;
        }
        if (message == WM_DPICHANGED) {
            p->dpr = std::clamp(HIWORD(w) / 96., .5, 4.);
            const auto* rect = reinterpret_cast<const RECT*>(l);
            SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            p->layout();
            return 0;
        }
        if (message == WM_SETFOCUS || message == WM_KILLFOCUS) {
            p->windowFocused = message == WM_SETFOCUS;
            if (p->instance) {
                p->instance->host.uiFocus(p->windowFocused);
            }
            return 0;
        }
        if (message == WM_COMMAND) {
            const auto id = LOWORD(w);
            if (id >= 100 && id < 104) {
                p->selectScenario(id - 100);
            } else if (id >= 200 && id < 205) {
                p->selectSize(id - 200);
            } else if (id >= 300 && id <= 303) {
                const WPARAM keys[]{VK_F9, VK_F6, VK_F8, VK_F1};
                return windowProc(window, WM_KEYDOWN, keys[id - 300], 0);
            }
            return 0;
        }
        if (message == WM_KEYDOWN) {
            if (w == VK_F2) {
                p->selectScenario(cdsdk::previewScenarioIndex(p->options.scenario) + 1);
                return 0;
            }
            if (w == VK_F3) {
                p->nextSize();
                return 0;
            }
            if (w == VK_F9) {
                p->exportPng();
                return 0;
            }
            if (w == VK_F1) {
                p->options.overlay = !p->options.overlay;
                p->compose();
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            }
            if (w == VK_F5) {
                p->open(true);
                return 0;
            }
            if (p->instance) {
                if (w == VK_F6) {
                    if (p->settingsOpen)
                        return 0;
                    p->settingsOpen = true;
                    try {
                        cdsdk::showSettings(window, *p->instance, p->descriptor.settings_page());
                    } catch (...) {
                        p->settingsOpen = false;
                        throw;
                    }
                    p->settingsOpen = false;
                    return 0;
                }
                if (w == VK_F7) {
                    p->layoutIndex = (p->layoutIndex + 1) % p->descriptor.layouts_size();
                    p->options.layout = p->descriptor.layouts(p->layoutIndex).id();
                    p->layout();
                    return 0;
                }
                if (w == VK_F8) {
                    p->paused = !p->paused;
                    p->instance->pause(p->paused);
                    p->instance->host.uiFocus(p->windowFocused);
                    p->caption();
                    return 0;
                }
                const uint32_t keys[]{VK_TAB,    VK_LEFT,  VK_RIGHT,  VK_UP,   VK_DOWN,
                                      VK_RETURN, VK_SPACE, VK_ESCAPE, VK_HOME, VK_END};
                if (!(GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000)) {
                    for (uint32_t i = 0; i < std::size(keys); ++i) {
                        if (w == keys[i] && p->instance->host.uiKey(i + 1, (GetKeyState(VK_SHIFT) & 0x8000) ? 1 : 0)) {
                            return 0;
                        }
                    }
                }
            }
        }
        if (message == WM_MOUSEWHEEL && p->instance) {
            POINT point{short(LOWORD(l)), short(HIWORD(l))};
            ScreenToClient(window, &point);
            p->instance->input(
                {sizeof(CdInput), CD_INPUT_WHEEL, 0, 0, point.x / p->dpr, point.y / p->dpr, double(short(HIWORD(w)))});
            return 0;
        }
        if ((message == WM_MOUSELEAVE || message == WM_CAPTURECHANGED || message == WM_CANCELMODE) && p->instance) {
            p->instance->input({sizeof(CdInput), message == WM_MOUSELEAVE ? CD_INPUT_LEAVE : CD_INPUT_CANCEL});
            return 0;
        }
        if (message == WM_TIMER) {
            if (p->step()) {
                InvalidateRect(window, nullptr, FALSE);
            }
            if (p->stopRequested) {
                PostMessageW(window, WM_CLOSE, 0, 0);
            }
            return 0;
        }
        if ((message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_MOUSEMOVE ||
             message == WM_LBUTTONDBLCLK) &&
            p->instance) {
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
            TrackMouseEvent(&tracking);
            if (message != WM_LBUTTONUP && !p->instance->host.captured &&
                !p->inside(short(LOWORD(l)), short(HIWORD(l)))) {
                return 0;
            }
            if (message == WM_LBUTTONDOWN) {
                SetFocus(window);
            }
            p->instance->input({sizeof(CdInput),
                                message == WM_LBUTTONDOWN     ? CD_INPUT_PRESS
                                : message == WM_LBUTTONUP     ? CD_INPUT_RELEASE
                                : message == WM_LBUTTONDBLCLK ? CD_INPUT_DOUBLE_CLICK
                                                              : CD_INPUT_MOVE,
                                uint32_t(w & MK_LBUTTON), 0, short(LOWORD(l)) / p->dpr, short(HIWORD(l)) / p->dpr, 0});
            if (p->instance->host.captured) {
                SetCapture(window);
            } else if (GetCapture() == window) {
                ReleaseCapture();
            }
            return 0;
        }
        if (message == WM_PAINT) {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window, &paint);
            if (!p->bgra.empty()) {
                BITMAPINFO info{};
                info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                info.bmiHeader.biWidth = LONG(p->imageWidth);
                info.bmiHeader.biHeight = -LONG(p->imageHeight);
                info.bmiHeader.biPlanes = 1;
                info.bmiHeader.biBitCount = 32;
                info.bmiHeader.biCompression = BI_RGB;
                StretchDIBits(dc, 0, 0, p->width, p->height, 0, 0, p->imageWidth, p->imageHeight, p->bgra.data(), &info,
                              DIB_RGB_COLORS, SRCCOPY);
            } else {
                FillRect(dc, &paint.rcPaint, GetSysColorBrush(COLOR_WINDOW));
            }
            EndPaint(window, &paint);
            return 0;
        }
        if (message == WM_CLOSE) {
            p->save();
            DestroyWindow(window);
            return 0;
        }
        if (message == WM_DESTROY) {
            KillTimer(window, 1);
            p->window = nullptr;
            PostQuitMessage(0);
            return 0;
        }
    } catch (const std::exception& error) {
        p->report(error.what());
    }
    return DefWindowProcW(window, message, w, l);
}
}  // namespace

#ifndef CD_PREVIEW_TEST
int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::cerr
            << "cd-sdk-preview DIRECTORY [--size WIDTHxHEIGHT] [--dpr 0.5..4] [--theme light|dark] [--language BCP47]\n"
               "[--services mock|none] [--mock FILE.json] [--time UNIX_MS] [--utc-offset MINUTES] [--layout ID]\n"
               "[--screenshot OUTPUT.png|bmp] [--frames N] [--state FILE.pb] [--overlay] [--diagnostics OUTPUT.json]\n"
               "[--scenario normal|loading|empty|error] [--export-dir DIRECTORY]\n"
               "[--watch-ms N] [--replay FILE.json]\nLive services: install the package in CyberDesktop or use Plugin "
               "Studio.\n";
        return 2;
    }
    try {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        Preview preview;
        preview.options = cdsdk::PreviewOptions::parse(argc, argv);
        preview.root = std::filesystem::absolute(argv[1]);
        preview.dpr = preview.options.dpr;
        if (!preview.options.stopEvent.empty()) {
            preview.stopEvent = OpenEventW(SYNCHRONIZE, FALSE, preview.options.stopEvent.c_str());
            if (!preview.stopEvent) {
                throw std::runtime_error("Cannot open SDK preview stop event");
            }
        }
        if (!preview.options.state.empty() && std::filesystem::exists(preview.options.state)) {
            preview.saved = cdsdk::readPreviewState(preview.options.state);
        }
        if (!preview.open()) {
            return 1;
        }
        if (preview.options.logicalWidth) {
            preview.width = uint32_t(std::lround(preview.options.logicalWidth * preview.dpr));
            preview.height = uint32_t(std::lround(preview.options.logicalHeight * preview.dpr));
        } else {
            const auto& layout = preview.descriptor.layouts(preview.layoutIndex);
            if (layout.topology() == cdsdk::pb::RECT) {
                int minA = INT_MAX, minB = INT_MAX, maxA = INT_MIN, maxB = INT_MIN;
                for (const auto& cell : layout.cells()) {
                    minA = std::min(minA, cell.a());
                    maxA = std::max(maxA, cell.a());
                    minB = std::min(minB, cell.b());
                    maxB = std::max(maxB, cell.b());
                }
                const double scale = std::min(96. * preview.dpr, 4096. / std::max(maxA - minA + 1, maxB - minB + 1));
                preview.width = uint32_t((maxA - minA + 1) * scale);
                preview.height = uint32_t((maxB - minB + 1) * scale);
            }
        }
        preview.layout();
        preview.watcher = std::make_unique<cdsdk::ResourceWatcher>(preview.root);
        std::cout << "SERVICES " << preview.options.services << "; scenario " << preview.options.scenario << "; time "
                  << (preview.options.time ? "fixed" : "system") << '\n'
                  << std::flush;
        if (!preview.options.screenshot.empty()) {
            preview.windowFocused = true;
            preview.instance->host.uiFocus(true);
            cdsdk::PreviewReplay replay(preview.options.replay);
            preview.render(true);
            for (uint32_t frame = 0; frame < preview.options.frames; ++frame) {
                if (preview.instance) {
                    replay.dispatch(frame, *preview.instance);
                }
                preview.step();
                if (preview.stopRequested) {
                    break;
                }
            }
            const auto start = std::chrono::steady_clock::now();
            while (!preview.stopRequested &&
                   std::chrono::steady_clock::now() - start < std::chrono::milliseconds(preview.options.watchMs)) {
                preview.step();
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
            }
            // Settle background decodes without progressing the animation clock repeatedly.
            for (int i = 0; i < 30 && preview.instance; ++i) {
                preview.instance->host.tick(preview.seconds + .1);
                preview.render();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            if (!preview.failure.empty() || !preview.render(true)) {
                return 1;
            }
            preview.images.save(preview.options.screenshot, preview.width, preview.height, preview.shown);
            preview.save();
            if (!preview.options.diagnostics.empty()) {
                cdsdk::Error error;
                if (!cdsdk::writeDiagnostics(*preview.instance, preview.options.diagnostics, &error)) {
                    throw std::runtime_error(cdsdk::errorText(error));
                }
            }
            return 0;
        }
        WNDCLASSW wc{};
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = windowProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"CyberDesktopSdkPreview";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
        const LONG requestedWidth = LONG(preview.width), requestedHeight = LONG(preview.height);
        RECT client{0, 0, requestedWidth, requestedHeight};
        AdjustWindowRectExForDpi(&client, WS_OVERLAPPEDWINDOW, TRUE, 0, UINT(preview.dpr * 96));
        const auto menu = preview.menu();
        HWND window = CreateWindowExW(0, wc.lpszClassName,
                                      L"SDK preview — F1: bounds; F5: reload; F6: settings; F7: layout; F8: pause",
                                      WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, client.right - client.left,
                                      client.bottom - client.top, nullptr, menu, wc.hInstance, &preview);
        if (!window) {
            DestroyMenu(menu);
            throw std::runtime_error("Cannot create preview window");
        }
        // Window chrome follows monitor DPI, independently of the requested render
        // scale. Correct the client size once its actual monitor is known.
        client = {0, 0, requestedWidth, requestedHeight};
        AdjustWindowRectExForDpi(&client, WS_OVERLAPPEDWINDOW, TRUE, 0, GetDpiForWindow(window));
        SetWindowPos(window, nullptr, 0, 0, client.right - client.left, client.bottom - client.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        preview.caption();
        ShowWindow(window, SW_SHOW);
        SetTimer(window, 1, 16, nullptr);
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "preview: " << error.what() << '\n';
        return 1;
    }
}
#endif
