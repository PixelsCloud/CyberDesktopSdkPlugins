#include <CyberCanvas/Canvas.h>
#include <sdk/Settings.h>
#include <sdk/cpp/Plugin.h>

#include <iostream>

#include "tests/ComponentServicesTest.h"
#include "tools/PreviewImages.h"
#include "tools/PreviewOptions.h"
#include "tools/PreviewOverlay.h"
#include "tools/PreviewReplay.h"
#include "tools/PreviewState.h"
#include "tools/ResourceWatcher.h"

namespace {
struct ThrowingPlugin {
    cdsdk::Settings settings{};

    explicit ThrowingPlugin(const CdCreateContext&) {}

    CdResult render(const CdFrame&) {
        throw std::runtime_error("test exception");
    }

    static inline int pauses = 0;

    void setPaused(bool) {
        ++pauses;
    }
};
}  // namespace

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
        componentServicesTest(check);
        const std::filesystem::path plugin = argv[1], output = argv[2];
        std::filesystem::create_directories(output);
        using Adapter = cdsdk::PluginAdapter<ThrowingPlugin>;
        CdPluginApi api{};
        check("adapter rejects version before modifying output",
              Adapter::query(99, &api, sizeof(api), "test", "1.0.0") == CD_ERROR_VERSION && api.struct_size == 0);
        check("adapter rejects undersized query output",
              Adapter::query(1, &api, sizeof(api) - 1, "test", "1.0.0") == CD_ERROR_BUFFER && api.struct_size == 0);
        Adapter::query(1, &api, sizeof(api), "test", "1.0.0");
        CdHostApi host{sizeof(host), 1};
        CdCreateContext context{sizeof(context), 1, "test", "instance", ".", &host};
        CdInstanceApi instance{};
        check("typed adapter creates existing C ABI table",
              api.create(&context, &instance, sizeof(instance)) == CD_OK && instance.struct_size == 88);
        CdFrame frame{sizeof(frame)};
        check("C callbacks contain C++ exceptions", instance.render(instance.instance, &frame) == CD_ERROR_INTERNAL);
        instance.set_paused(instance.instance, 1);
        instance.set_paused(instance.instance, 1);
        instance.set_paused(instance.instance, 0);
        instance.set_paused(instance.instance, 0);
        check("adapter pause hooks are idempotent", ThrowingPlugin::pauses == 2);
        uint64_t size = 99;
        check("optional state defaults to empty", instance.save_state(instance.instance, nullptr, 0, &size) == CD_OK &&
                                                      size == 0 &&
                                                      instance.restore_state(instance.instance, nullptr, 0) == CD_OK);
        instance.destroy(instance.instance);

        cybercanvas::Canvas canvas;
        std::vector<uint8_t> pixels(1536 * 12 * 4);
        for (size_t i = 0; i < pixels.size(); i += 4) {
            pixels[i] = uint8_t(i / 4 % 255);
            pixels[i + 1] = 80;
            pixels[i + 2] = 160;
            pixels[i + 3] = 255;
        }
        auto actual = pixels;
        cybercanvas::FrameBuffer source{1536, 12, 1536 * 4, pixels.size(), pixels.data(), 1.5};
        cybercanvas::FrameBuffer destination{1536, 12, 1536 * 4, actual.size(), actual.data(), 1.5};
        {
            auto painting = canvas.frame(destination);
            canvas.blit(source, {0, 0, 1024, 8});
            check("transient upload tracks actual DPI bytes without cache entries",
                  canvas.stats().transientBytes == pixels.size() && canvas.stats().bitmapEntries == 0);
        }
        check("large uncached blit preserves pixels and releases transient accounting",
              actual == pixels && canvas.stats().transientBytes == 0 && canvas.stats().uploads.count == 1);
        cdsdk::PreviewImages images;
        images.save(output / "roundtrip.png", 1536, 12, actual);
        auto loaded = images.load(output / "roundtrip.png");
        check("PNG uses WIC and preserves RGBA pixel payload", loaded.rgba == pixels && loaded.width == 1536);
        images.save(output / "roundtrip.bmp", 1536, 12, actual);
        check("BMP is independently decoded", images.load(output / "roundtrip.bmp").rgba == pixels);
        auto modified = loaded;
        modified.rgba[0] += 3;
        check("visual diff has per-channel tolerance", cdsdk::compareImages(loaded, modified, 3).differing == 0 &&
                                                           cdsdk::compareImages(loaded, modified, 2).differing == 1);
        const auto mockPath = output / "mock.json";
        std::ofstream(mockPath) << R"({"cpuPercent":73,"gpuPercent":11,"hasNetworkData":false})";
        cdsdk::PreviewOptions options;
        options.mock = mockPath;
        options.time = 1709208000000ull;
        options.language = "zh-CN";
        options.languageOverride = true;
        cdsdk::MockHost mock;
        options.configure(mock);
        double cpu = 0;
        uint64_t token = 0;
        cdsdk::MockHost::system(
            &mock, 1000,
            [](void* user, const CdSystemInfo* data) {
                *static_cast<double*>(user) = data->cpu_percent;
            },
            &cpu, &token);
        mock.tick(0);
        check("mock JSON overrides real service callback data",
              cpu == 73 && !mock.hasNetworkData && mock.gpuPercent == 11 && mock.fixedUnixMs == options.time);
        std::ofstream(mockPath) << R"({"hasNetworkData":1})";
        bool invalid = false;
        try {
            options.configure(mock);
        } catch (const std::invalid_argument&) {
            invalid = true;
        }
        check("mock JSON rejects incorrect field types", invalid);
        const auto replayPath = output / "invalid-replay.json";
        std::ofstream(replayPath) << R"({"events":[{"frame":0,"kind":1,"buttons":1e30}]})";
        invalid = false;
        try {
            cdsdk::PreviewReplay replay(replayPath);
        } catch (const std::invalid_argument&) {
            invalid = true;
        }
        check("replay bounds values before integer conversion", invalid);

        cdsdk::pb::PluginDescriptor descriptor;
        cdsdk::Error error;
        cdsdk::NativeModule module;
        cdsdk::NativeInstance native;
        if (!cdsdk::loadDirectoryDescriptor(plugin, &descriptor, &error) || !module.open(plugin, descriptor, &error) ||
            !native.create(module, plugin, "preview-test", &error)) {
            throw std::runtime_error(cdsdk::errorText(error));
        }
        native.host.fixedUnixMs = 1709208000000ull;
        native.host.utcOffsetMinutes = 0;
        const CdCell cell{0, 0};
        const CdLayout layout{sizeof(layout),     0, 0, 1, &cell, 320, 360, 1, {0, 0, 320, 360}, {240, 240, 240, 255},
                              {50, 200, 150, 255}};
        native.api.layout_changed(native.api.instance, &layout);
        if (!native.render(320, 360, 1, &pixels, &error)) {
            throw std::runtime_error(cdsdk::errorText(error));
        }
        check("workbench publishes semantic buttons and optional capture",
              native.host.streaming && native.host.semantics.size() >= 5);
        native.host.uiFocus(true);
        check("keyboard operates native example", native.host.uiKey(CD_KEY_TAB) && native.host.uiKey(CD_KEY_ENTER));
        const auto saved = cdsdk::capturePreviewState(native, descriptor);
        check("business state reflects keyboard action",
              saved.native_state().size() == 60 && uint8_t(saved.native_state()[4]) == 1);
        cdsdk::writePreviewState(output / "state.pb", saved);
        native.api.restore_state(native.api.instance, nullptr, 0);
        cdsdk::restorePreviewState(native, descriptor, cdsdk::readPreviewState(output / "state.pb"));
        check("settings and state persist through protobuf file", native.state(&error) == saved.native_state());
        auto rejected = saved;
        rejected.set_plugin_id("other.plugin");
        bool refused = false;
        try {
            cdsdk::restorePreviewState(native, descriptor, rejected);
        } catch (const std::exception&) {
            refused = true;
        }
        check("state cannot cross plugin identities", refused && native.state(&error) == saved.native_state());
        const auto button =
            std::find_if(native.host.semantics.begin(), native.host.semantics.end(), [](const auto& node) {
                return node.role == CD_ROLE_SLIDER;
            });
        if (button == native.host.semantics.end()) {
            throw std::runtime_error("No slider");
        }
        const double x = button->bounds.x + button->bounds.width / 2, y = button->bounds.y + button->bounds.height / 2;
        native.input({sizeof(CdInput), CD_INPUT_PRESS, CD_BUTTON_LEFT, 0, x, y});
        check("capture is claimed only inside press callback", native.host.captured);
        native.pause(true);
        check("pause revokes focus and captured gesture", !native.host.captured && !native.host.uiGranted);
        native.pause(false);
        // Page switches must transfer the one keyboard registration, preserve each
        // sample's model and stop the inactive page's clocks.
        const auto select = [&](const char* page) {
            const CdSettingValue value{sizeof(value), CD_SETTING_CHOICE, "page", 0, 0, page};
            if (native.api.apply_settings(native.api.instance, &value, 1) != CD_OK ||
                !native.render(320, 360, 1, &pixels, &error)) {
                throw std::runtime_error("Component page selection failed");
            }
        };
        int index = 0;
        const CdSettingValue demo{sizeof(demo), CD_SETTING_CHOICE, "data_source", 0, 0, "demo"};
        native.api.apply_settings(native.api.instance, &demo, 1);
        for (const char* page : {"info", "tasks", "media", "calendar"}) {
            select(page);
            const auto before = native.state(&error);
            native.host.uiFocus(true);
            const CdUiEvent event{sizeof(event), CD_UI_ACTION, CD_UI_PRESS, 0, uint32_t(index >= 2 ? 2 : 1)};
            check("component page exposes working semantic actions",
                  native.host.uiCallback && native.host.uiCallback(native.host.uiUser, &event) == CD_OK);
            native.render(320, 360, 1, &pixels, &error);
            images.save(output / (std::string(page) + ".png"), 320, 360, pixels);
            const auto after = native.state(&error);
            check("component action updates serialized model", before != after);
            select("canvas");
            select(page);
            check("page switches preserve independent component state", native.state(&error) == after);
            native.pause(true);
            const auto hidden = native.state(&error);
            check("hidden component rejects keyboard actions", !native.host.uiKey(CD_KEY_ENTER));
            native.pause(false);
            check("hidden time does not advance component model", native.state(&error) == hidden);
            ++index;
        }
        const CdSettingValue liveSource{sizeof(liveSource), CD_SETTING_CHOICE, "data_source", 0, 0, "host"};
        native.api.apply_settings(native.api.instance, &liveSource, 1);
        const auto hasService = [&](bool media) {
            return std::any_of(native.host.subscriptions.begin(), native.host.subscriptions.end(),
                               [&](const auto& entry) {
                                   return media ? entry.second.media != nullptr : entry.second.system != nullptr;
                               });
        };
        select("info");
        native.host.tick(10);
        native.render(320, 360, 1, &pixels, &error);
        images.save(output / "info-live.png", 320, 360, pixels);
        check("workbench live info subscribes only to telemetry", hasService(false) && !hasService(true));
        native.pause(true);
        check("workbench hidden info removes producer", !hasService(false));
        native.pause(false);
        check("workbench resumed info reconnects", hasService(false));
        select("media");
        native.host.tick(11);
        native.render(320, 360, 1, &pixels, &error);
        images.save(output / "media-live.png", 320, 360, pixels);
        check("switch to media transfers service ownership", !hasService(false) && hasService(true));
        const auto liveState = native.state(&error);
        native.host.uiFocus(true);
        const CdUiEvent play{sizeof(play), CD_UI_ACTION, CD_UI_PRESS, 0, 2};
        const auto beforeRequest = native.host.nextRequest;
        check(
            "workbench button dispatches a real service command",
            native.host.uiCallback(native.host.uiUser, &play) == CD_OK && native.host.nextRequest == beforeRequest + 1);
        native.host.tick(12);
        native.host.tick(13);
        check("host playback is not saved as local demo state", native.state(&error) == liveState);
        native.host.hasMediaSession = false;
        native.host.tick(14);
        native.render(320, 360, 1, &pixels, &error);
        images.save(output / "media-empty.png", 320, 360, pixels);
        const auto emptyRequest = native.host.nextRequest;
        native.host.uiCallback(native.host.uiUser, &play);
        check("empty player button cannot dispatch command", native.host.nextRequest == emptyRequest);
        select("canvas");
        check("return to canvas releases both component services", !hasService(false) && !hasService(true));
        native.host.uiFocus(true);
        check("original workbench regains keyboard after component pages",
              native.host.uiKey(CD_KEY_TAB) && native.host.uiKey(CD_KEY_ENTER));
        const uint8_t legacy[]{'W', 'B', 1, 0, 7, 0, 0, 0};
        check("legacy workbench state still restores",
              native.api.restore_state(native.api.instance, legacy, sizeof(legacy)) == CD_OK &&
                  uint8_t(native.state(&error)[4]) == 7);
        std::vector<cdsdk::DiagnosticSample> retained;
        if (!cdsdk::sampleDiagnostics(native.host, &retained, &error)) {
            throw std::runtime_error(cdsdk::errorText(error));
        }
        native.api.destroy(native.api.instance);
        native.api.instance = nullptr;
        cdsdk::previewOverlay(canvas, retained, 320, 360, 1, pixels);
        check("overlay remains usable after module instance failure or destruction",
              !retained.empty() && native.host.diagnostics.empty() && native.host.subscriptions.empty() &&
                  !pixels.empty());

        const auto resourceRoot = output / "watch";
        std::filesystem::create_directories(resourceRoot);
        cdsdk::ResourceWatcher watcher(resourceRoot);
        // Wait for an actual notification before measuring coalescing. Worker
        // startup may exceed an arbitrary sleep on a busy build machine.
        bool watching = false;
        for (int attempt = 0; attempt < 8 && !watching; ++attempt) {
            std::ofstream(resourceRoot / "asset.json") << "startup";
            for (int i = 0; i < 25 && !watching; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                const auto startup = watcher.drain();
                if (!startup.error.empty()) {
                    throw std::runtime_error(startup.error);
                }
                watching = startup.changed;
            }
        }
        check("background resource watcher is receiving events", watching);
        for (int i = 0; i < 8; ++i) {
            std::ofstream(resourceRoot / "asset.json") << i;
        }
        std::ofstream(resourceRoot / "ignored.dll") << "not a resource";
        cdsdk::ResourceWatcher::Changes changes;
        for (int i = 0; i < 150 && !changes.changed; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            changes = watcher.drain();
        }
        check(
            "background notifications coalesce resources and ignore DLLs",
            changes.changed && changes.paths.size() == 1 && changes.paths[0] == L"asset.json" && changes.error.empty());
        check("draining does not replay old changes", !watcher.drain().changed);
        std::cout << "RESULT " << failures << " failures\n";
        return failures ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
