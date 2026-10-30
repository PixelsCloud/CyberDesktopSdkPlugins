#include <Windows.h>

#include <iostream>
#include <stdexcept>
#include <vector>

#include "Lunar.h"

namespace {
void check(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

struct Host {
    CdTimerCallback callback = nullptr;
    void* user = nullptr;
    uint64_t token = 0, next = 1;
    int redraws = 0;

    static void CD_CALL redraw(void* p) {
        ++static_cast<Host*>(p)->redraws;
    }

    static CdResult CD_CALL timer(void* p, uint32_t interval, CdTimerCallback cb, void* u, uint64_t* token) {
        auto& h = *static_cast<Host*>(p);
        check(interval == 1000 && !h.token, "Unexpected calendar timer or duplicate subscription");
        h.callback = cb;
        h.user = u;
        *token = h.token = h.next++;
        return CD_OK;
    }

    static void CD_CALL stop(void* p, uint64_t token) {
        auto& h = *static_cast<Host*>(p);
        check(token == h.token, "Unknown timer");
        h.token = 0;
        h.callback = nullptr;
    }

    CdHostApi api{sizeof(api), CD_PLUGIN_ABI_VERSION, redraw, nullptr, nullptr, nullptr, stop, timer};
};
}  // namespace

int main() {
    try {
        using namespace lunarplugin;
        check(!valid({1900, 2, 29}) && valid({2000, 2, 29}) && !valid({2101, 1, 1}), "Date validation");
        check(shiftMonth({2024, 1, 31}, 1) == Date{2024, 2, 29}, "Month end navigation");
        check(shiftMonth({1900, 1, 1}, -1) == Date{1900, 1, 1}, "Lower range navigation");
        check(weekday({2026, 2, 17}) == 2, "Weekday fixture");
        for (int day = 1; day <= 28; ++day) {
            const auto l = convert({2026, 2, day});
            check(l.month == (day < 17 ? 12 : 1) && l.day == (day < 17 ? day + 13 : day - 16),
                  "HKO February 2026 lunar fixture mismatch");
        }
        check(convert({2026, 2, 16}).festival == L"除夕", "Lunar New Year's Eve");
        check(convert({2026, 2, 17}).festival == L"春节", "Lunar New Year");
        check(convert({2026, 2, 4}).term == L"立春" && convert({2026, 2, 18}).term == L"雨水", "Solar term fixtures");
        check(convert({2026, 3, 3}).festival == L"元宵", "Lantern festival fixture");
        check(convert({2023, 3, 22}).leap && convert({2023, 3, 22}).month == 2, "Leap lunar month");
        const auto grid = monthGrid({2026, 2, 17}, true);
        check(grid.rows == 5 && grid.dates[0] == Date{2026, 1, 26} && grid.dates[22] == Date{2026, 2, 17},
              "Month layout fixture");
        check(monthGrid({2026, 2, 1}, false).dates[0] == Date{2026, 2, 1}, "Sunday start");
        auto settings = defaults();
        Renderer renderer;
        int count = 0;
        for (const auto spec : {Spec{"test", 1, 2}, Spec{"test", 2, 2}, Spec{"test", 4, 4}}) {
            for (double dpi : {1., 1.5, 2.}) {
                for (int cellH : {72, 100, 150}) {
                    const int w = spec.columns * 72, h = spec.rows * cellH;
                    std::vector<uint8_t> pixels(size_t(w * dpi) * size_t(h * dpi) * 4);
                    CdFrame frame{sizeof(frame),
                                  uint32_t(w * dpi),
                                  uint32_t(h * dpi),
                                  uint32_t(w * dpi) * 4,
                                  pixels.size(),
                                  pixels.data(),
                                  dpi};
                    for (bool dark : {false, true}) {
                        for (int background : {0, 1}) {
                            const CdSettingValue option{sizeof(option), CD_SETTING_TOGGLE, "show_background",
                                                        background};
                            check(settings.apply(&option, 1) == CD_OK, "Background setting");
                            check(renderer.render(spec, settings, {{2026, 2, 17}, {2026, 2, 17}, dark}, frame, {}),
                                  "DPI render");
                            size_t alpha = 0;
                            for (size_t i = 3; i < pixels.size(); i += 4) {
                                alpha += pixels[i] != 0;
                                check(pixels[i - 1] <= pixels[i] && pixels[i - 2] <= pixels[i] &&
                                          pixels[i - 3] <= pixels[i],
                                      "Nonpremultiplied output");
                            }
                            check(alpha > 20, "Empty calendar");
                            const auto before = pixels;
                            check(renderer.render(spec, settings, {{2026, 2, 17}, {2026, 2, 17}, dark}, frame, {}) &&
                                      pixels == before,
                                  "Identical calendar frame changed");
                            ++count;
                        }
                    }
                }
            }
            Host host;
            CdCreateContext c{sizeof(c), CD_PLUGIN_ABI_VERSION, spec.id, "test", "", &host.api, &host};
            CdInstanceApi instance{};
            check(create(spec, &c, &instance, sizeof(instance)) == CD_OK && host.token, "Create and subscribe");
            const int redraws = host.redraws;
            host.callback(host.user, 1000);
            check(host.redraws == redraws, "Unchanged day repainted");
            const CdSettingValue invalid{sizeof(invalid), CD_SETTING_CHOICE, "lighting", 0, 0, "bad"};
            check(instance.apply_settings(instance.instance, &invalid, 1) == CD_ERROR_ARGUMENT,
                  "Invalid setting accepted");
            uint8_t state[8]{};
            uint64_t size = 0;
            check(instance.save_state(instance.instance, state, 8, &size) == CD_OK && size == 8, "Save selected date");
            state[5] = 13;
            check(instance.restore_state(instance.instance, state, 8) == CD_ERROR_ARGUMENT, "Invalid state accepted");
            instance.set_paused(instance.instance, 1);
            check(!host.token, "Hidden timer retained");
            instance.set_paused(instance.instance, 0);
            check(host.token, "Resume timer missing");
            instance.destroy(instance.instance);
            check(!host.token, "Timer leaked after destroy");
        }
        std::cout << "PASS: lunar fixtures, leap month, festivals, terms, month navigation, " << count
                  << " DPI/theme/background renders, state validation and lifecycle\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
