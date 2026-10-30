#include <filesystem>
#include <iostream>
#include <map>

#include "plugins/sign-suite/Sign.h"

namespace {
void check(bool ok, const char* message) {
    if (!ok) {
        throw std::runtime_error(message);
    }
}

struct Host {
    struct Timer {
        CdTimerCallback callback;
        void* user;
    };

    std::map<uint64_t, Timer> timers;
    uint64_t next = 1, now = 1000;
    unsigned redraws = 0;
    bool failTimer = false;
    CdHostApi api{};

    Host() {
        api = {sizeof(api), CD_PLUGIN_ABI_VERSION};
        api.request_redraw = [](void* p) {
            ++static_cast<Host*>(p)->redraws;
        };
        api.unsubscribe = [](void* p, uint64_t token) {
            static_cast<Host*>(p)->timers.erase(token);
        };
        api.subscribe_timer = [](void* p, uint32_t, CdTimerCallback callback, void* user, uint64_t* token) -> CdResult {
            auto& host = *static_cast<Host*>(p);
            if (host.failTimer) {
                return CD_ERROR_INTERNAL;
            }
            *token = host.next++;
            host.timers.emplace(*token, Timer{callback, user});
            return CD_OK;
        };
    }

    void tick() {
        now += 50;
        for (auto [id, timer] : timers) {
            timer.callback(timer.user, now);
        }
    }
};

CdResult setting(neon::Sign& sign, const char* key, int64_t value, uint32_t type = CD_SETTING_INTEGER) {
    const CdSettingValue change{sizeof(change), type, key, value};
    return sign.applySettings(&change, 1);
}

CdResult effectSetting(neon::Sign& sign, const char* name) {
    const CdSettingValue change{sizeof(change), CD_SETTING_CHOICE, "effect", 0, 0, name};
    return sign.applySettings(&change, 1);
}

}  // namespace

int wmain(int argc, wchar_t** argv) try {
    const std::filesystem::path output = argc > 1 ? argv[1] : L"neon-previews";
    std::filesystem::create_directories(output);
    Host host;
    CdCreateContext context{};
    context.struct_size = sizeof(context);
    context.abi_version = CD_PLUGIN_ABI_VERSION;
    context.host = &host.api;
    context.host_context = &host;
    context.plugin_id = "com.cyberdesktop.neon.sign";
    context.plugin_directory = ".";
    context.instance_id = "test";
    {
        neon::Sign sign(context);
        check(host.timers.size() == 1, "One animation subscription per instance");
        for (double dpi : {1., 1.25, 1.5, 2., 3.}) {
            for (auto size : {cybercanvas::Size{288, 144}, {432, 144}, {432, 200}, {432, 72}}) {
                const uint32_t width = uint32_t(size.width * dpi), height = uint32_t(size.height * dpi);
                std::vector<uint8_t> pixels(size_t(width) * height * 4 + 32, 0xA5);
                CdFrame frame{sizeof(frame), width, height, width * 4, pixels.size() - 32, pixels.data(), dpi};
                check(sign.render(frame) == CD_OK, "Render at actual DPI");
                check(pixels[(size_t(uint32_t(2 * dpi)) * width + width / 2) * 4 + 3] > 200,
                      "Full sign backing reaches the standard top inset without letterboxing");
                check(std::all_of(pixels.end() - 32, pixels.end(),
                                  [](auto c) {
                                      return c == 0xA5;
                                  }),
                      "Frame buffer bounds");
                size_t visible = 0;
                for (size_t i = 0; i < frame.capacity; i += 4) {
                    const auto alpha = pixels[i + 3];
                    visible += alpha > 0;
                    check(pixels[i] <= alpha && pixels[i + 1] <= alpha && pixels[i + 2] <= alpha,
                          "Premultiplied output");
                }
                check(visible > width * height / 10 && visible < width * height,
                      "Visible artwork and transparent surround");
                const auto name = L"sign-" + std::to_wstring(int(size.width)) + L"x" +
                                  std::to_wstring(int(size.height)) + L"-" + std::to_wstring(int(dpi * 100)) + L".png";
                check(sign.savePreview((output / name).wstring(), frame), "PNG preview export");
            }
        }
        std::vector<uint8_t> pixels(720 * 264 * 4);
        CdFrame frame{sizeof(frame), 720, 264, 2880, pixels.size(), pixels.data(), 2};
        check(effectSetting(sign, "flicker") == CD_OK, "Select legacy flicker");
        const auto before = host.redraws;
        for (int i = 0; i < 241; ++i) {
            host.tick();
            if (host.redraws != before) {
                check(sign.render(frame) == CD_OK, "Animation frame");
            }
        }
        check(host.redraws - before >= 12 && host.redraws - before <= 20,
              "Only changing lights invalidate, no idle redraw");
        check(setting(sign, "animation", 0, CD_SETTING_TOGGLE) == CD_OK && host.timers.empty(), "Disable stops timer");
        check(sign.render(frame) == CD_OK, "Still sign render");
        check(sign.savePreview((output / "preview.png").wstring(), frame), "Default preview");
        const auto stable = pixels;
        for (int i = 0; i < 20; ++i) {
            host.tick();
        }
        check(sign.render(frame) == CD_OK && pixels == stable, "Disabled animation remains stable");
        host.failTimer = true;
        check(setting(sign, "animation", 1, CD_SETTING_TOGGLE) != CD_OK && !sign.settings.at("animation").integer,
              "Failed timer does not commit settings");
        host.failTimer = false;
        check(setting(sign, "animation", 1, CD_SETTING_TOGGLE) == CD_OK, "Resume animation");
        sign.setPaused(true);
        check(host.timers.empty(), "Hidden widget unsubscribes");
        check(setting(sign, "damage", 80) == CD_OK && host.timers.empty(),
              "Editing hidden widget does not restart timer");
        sign.setPaused(false);
        check(host.timers.size() == 1, "Visible widget resumes exactly one subscription");
        check(setting(sign, "damage", 0) == CD_OK && host.timers.empty(), "Undamaged sign needs no timer");
        check(setting(sign, "damage", 101) == CD_ERROR_ARGUMENT && sign.settings.at("damage").integer == 0,
              "Invalid setting rejected without mutation");
        check(setting(sign, "plate", 0, CD_SETTING_TOGGLE) == CD_OK && sign.render(frame) == CD_OK, "Transparent mode");
        check(sign.savePreview((output / "transparent.png").wstring(), frame), "Transparent preview");
    }
    check(host.timers.empty(), "Destroy releases timers");
    context.plugin_id = "com.cyberdesktop.neon.sign.1x4";
    {
        neon::Sign sign(context);
        check(sign.settings.at("damage").integer == 90, "Default damage is 90 percent");
        for (double dpi : {1., 1.25, 1.5, 2., 3.}) {
            for (int height : {54, 72, 100}) {
                const uint32_t w = uint32_t(288 * dpi), h = uint32_t(height * dpi);
                std::vector<uint8_t> pixels(size_t(w) * h * 4 + 32, 0xA5);
                CdFrame frame{sizeof(frame), w, h, w * 4, pixels.size() - 32, pixels.data(), dpi};
                check(sign.render(frame) == CD_OK, "Compact render at actual DPI");
                check(pixels[(size_t(uint32_t(2 * dpi)) * w + w / 2) * 4 + 3] > 200,
                      "Minimal backing fills brick height with the standard top inset");
                check(std::all_of(pixels.end() - 32, pixels.end(),
                                  [](auto c) {
                                      return c == 0xA5;
                                  }),
                      "Compact frame bounds");
                check(sign.savePreview((output / (L"minimal-" + std::to_wstring(height) + L"-" +
                                                  std::to_wstring(int(dpi * 100)) + L".png"))
                                           .wstring(),
                                       frame),
                      "Compact preview");
            }
        }
        std::vector<uint8_t> pixels(640 * 144 * 4);
        CdFrame frame{sizeof(frame), 640, 144, 2560, pixels.size(), pixels.data(), 2};
        check(sign.render(frame) == CD_OK && sign.savePreview((output / "minimal-preview.png").wstring(), frame),
              "Minimal package preview");
        for (const auto* effect : {"flicker", "breathe", "scan", "wave", "glitch", "mixed"}) {
            check(effectSetting(sign, effect) == CD_OK && host.timers.size() == 1,
                  "Switch effect without leaking timers");
            sign.render(frame);
            auto previous = pixels;
            unsigned changed = 0;
            const auto start = host.redraws;
            for (int step = 0; step < 240; ++step) {
                host.tick();
                check(sign.render(frame) == CD_OK, "Render effect cycle");
                changed += previous != pixels;
                previous = pixels;
                if (step == 60 || step == 75) {
                    check(sign.savePreview(
                              (output / (std::string(effect) + "-" + std::to_string(step) + ".png")).wstring(), frame),
                          "Effect preview");
                }
            }
            check(changed >= 4, "Every effect produces visible animation");
            if (std::string_view(effect) == "glitch" || std::string_view(effect) == "flicker") {
                check(host.redraws - start >= 12 && host.redraws - start <= 20,
                      "Intermittent effects do not redraw at idle");
            }
            sign.setPaused(true);
            check(host.timers.empty(), "Each effect pauses when hidden");
            const auto frozen = pixels;
            for (int step = 0; step < 10; ++step) {
                host.tick();
            }
            check(sign.render(frame) == CD_OK && pixels == frozen, "Hidden effect phase remains frozen");
            sign.setPaused(false);
        }
        check(effectSetting(sign, "unknown") == CD_ERROR_ARGUMENT, "Reject invalid effect");
        check(setting(sign, "damage", 0) == CD_OK && host.timers.size() == 1, "Mixed animation works without damage");
        check(setting(sign, "intensity", 0) == CD_OK && host.timers.empty(), "Zero intensity stops timer");
        check(setting(sign, "intensity", 100) == CD_OK && host.timers.size() == 1, "Full intensity resumes");
        check(setting(sign, "animation", 0, CD_SETTING_TOGGLE) == CD_OK && host.timers.empty(),
              "Master animation switch");
        sign.render(frame);
        const auto frozen = pixels;
        for (int i = 0; i < 20; ++i) {
            host.tick();
        }
        check(sign.render(frame) == CD_OK && pixels == frozen, "All mixed animation stops");
    }
    check(host.timers.empty(), "All instances release subscriptions");
    std::cout
        << "PASS: 35 DPI/size renders, six animated effects, sparse redraw, compact layout, pause/resume, settings "
           "rollback\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
