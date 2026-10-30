#include <Windows.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <utility>
#include <vector>

#include "plugins/weather-suite/Weather.h"

using namespace weatherplugin;
using Json = nlohmann::json;

namespace {
void require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}

struct ImageHost {
    uint64_t next = 1;
    int redraws = 0;
    std::map<uint64_t, std::pair<CdTimerCallback, void*>> timers;

    static void CD_CALL redraw(void* pointer) {
        ++static_cast<ImageHost*>(pointer)->redraws;
    }

    static void CD_CALL unsubscribe(void* pointer, uint64_t token) {
        static_cast<ImageHost*>(pointer)->timers.erase(token);
    }

    static CdResult CD_CALL subscribe(void* pointer, uint32_t, CdTimerCallback callback, void* user, uint64_t* token) {
        auto& host = *static_cast<ImageHost*>(pointer);
        *token = host.next++;
        host.timers.emplace(*token, std::make_pair(callback, user));
        return CD_OK;
    }

    CdHostApi api{sizeof(CdHostApi), CD_PLUGIN_ABI_VERSION, redraw, nullptr, nullptr, nullptr, unsubscribe, subscribe};
};

void checkImageScheduling(const std::filesystem::path& assets, Snapshot sample) {
    ImageHost host;
    {
        Renderer renderer(assets);
        CdCreateContext context{};
        context.struct_size = sizeof(context);
        context.abi_version = CD_PLUGIN_ABI_VERSION;
        context.host = &host.api;
        context.host_context = &host;
        renderer.bind(context);
        std::vector<uint8_t> pixels(154 * 99 * 4);
        CdFrame frame{sizeof(CdFrame), 154, 99, 154 * 4, pixels.size(), pixels.data(), 1};
        const Spec spec{"async-images", 1, 2};
        auto settings = defaults();
        require(renderer.renderReference(spec, settings, sample, false, frame, {}), "shared image resources settle");
        require(host.redraws > 0 && host.timers.empty(), "image completion redraws locally and stops polling");
        const auto retained = host.redraws;
        for (int index = 0; index < 5; ++index) {
            require(renderer.render(spec, settings, sample, false, frame, {}), "retained image frame");
        }
        require(host.redraws == retained && host.timers.empty(), "unchanged images schedule no redraw or timer");
        renderer.pause(true);
        sample.code = 61;
        require(renderer.render(spec, settings, sample, false, frame, {}), "paused image request retains content");
        require(host.redraws == retained && host.timers.empty(), "hidden image updates cannot request a frame");
        renderer.pause(false);
        require(host.redraws > retained, "resume delivers the deferred resource update");
        require(renderer.renderReference(spec, settings, sample, false, frame, {}), "resumed resources settle");
        auto invalid = frame;
        invalid.capacity = 1;
        require(!renderer.render(spec, settings, sample, false, invalid, {}), "shared canvas rejects invalid output");
    }
    require(host.timers.empty(), "renderer destruction releases image timer ownership");
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc != 3) {
            return 2;
        }
        const std::filesystem::path root(argv[1]), output(argv[2]);
        std::filesystem::create_directories(output);

        Snapshot fixture;
        fixture.valid = true;
        fixture.city = "北京";
        fixture.region = "北京市";
        fixture.time = "2026-10-13T10:30";
        fixture.temperature = 18.4;
        fixture.feels = 17.2;
        fixture.humidity = 46;
        fixture.wind = 2.3;
        fixture.code = 2;
        fixture.days = {{"2026-10-13", 2, 22, 14, 10},
                        {"2026-10-14", 0, 24, 15, 0},
                        {"2026-10-15", 3, 21, 13, 20},
                        {"2026-10-16", 61, 19, 12, 75},
                        {"2026-10-17", 2, 22, 14, 15}};

        require(locationFrom(defaults()).automatic && locationFrom(defaults()).city.empty(),
                "default is automatic without a hardcoded city");
        auto oldSettings = defaults();
        CdSettingValue oldCity{sizeof(CdSettingValue), CD_SETTING_TEXT, "city", 0, 0, "北京"};
        require(oldSettings.apply(&oldCity, 1) == CD_OK && locationFrom(oldSettings).city.empty(),
                "old default-city field cannot override automatic location");
        require(weekday("2026-10-13", true) == L"Tue", "forecast weekday");
        require(displayTemperature(0, true) == 32 && displayTemperature(-40, true) == -40,
                "local Fahrenheit conversion");
        for (int code : {0,  1,  2,  3,  45, 48, 51, 53, 55, 56, 57, 61, 63, 65, 66,
                         67, 71, 73, 75, 77, 80, 81, 82, 85, 86, 95, 96, 99, 999})
            for (bool day : {false, true})
                require(std::filesystem::exists(root / L"assets/icons" / (wide(iconName(code, day)) + L".png")),
                        "all WMO codes resolve to bundled icons");

        Json report;
        checkImageScheduling(root / L"assets", fixture);
        Renderer renderer(root / L"assets");
        Json renders = Json::array();
        {
            std::vector<uint8_t> current(154 * 99 * 4), previous(current.size());
            CdFrame a{sizeof(CdFrame), 154, 99, 154 * 4, current.size(), current.data(), 1}, b = a;
            b.rgba = previous.data();
            require(renderer.renderReference({"1x2", 1, 2}, defaults(), {}, true, a, {}) &&
                        renderer.renderReference({"1x2", 1, 2}, oldSettings, {}, true, b, {}) && current == previous,
                    "loading auto UI never displays the previously saved Beijing city");
        }
        for (const Spec spec : std::array<Spec, 3>{{{"1x2", 1, 2}, {"1x4", 1, 4}, {"2x4", 2, 4}}})
            for (int cellHeight : {72, 99, 144}) {
                auto settings = defaults();
                const uint32_t w = uint32_t(spec.columns * 77), h = uint32_t(spec.rows * cellHeight);
                std::vector<uint8_t> pixels(size_t(w) * h * 4);
                CdFrame frame{sizeof(CdFrame), w, h, w * 4, pixels.size(), pixels.data(), 1};
                require(renderer.renderReference(spec, settings, fixture, false, frame, {}), "weather rendered");
                bool visible = false;
                for (size_t i = 3; i < pixels.size(); i += 4) {
                    visible |= pixels[i] > 0;
                    require(pixels[i - 3] <= pixels[i] && pixels[i - 2] <= pixels[i] && pixels[i - 1] <= pixels[i],
                            "RGBA remains premultiplied");
                }
                require(visible, "weather frame contains content");
                renders.push_back({{"size", spec.id}, {"width", w}, {"height", h}});
                require(
                    renderer.savePng(output / (wide(spec.id) + L"-" + std::to_wstring(cellHeight) + L".png"), frame),
                    "preview exported");
            }
        report["renders"] = renders;
        report["passed"] = true;
        std::ofstream(output / "weather-tests.json") << report.dump(2);
        std::cout << "Display model, image lifecycle and layout tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    } catch (Error error) {
        std::cerr << "Display error " << int(error) << '\n';
        return 1;
    }
}
