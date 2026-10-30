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

#include "AirQuality.h"

using namespace airplugin;
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
        sample.aqi = 350;
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

        Snapshot parsed;
        parsed.valid = true;
        parsed.city = "济南";
        parsed.station = "示例监测站";
        parsed.time = "2026-10-13 11:00:00";
        parsed.aqi = 42;
        parsed.dominant = "pm25";
        parsed.pollutants = {37, 23, 16, 9, 4, 3};
        parsed.sources = {{"Environmental Protection Department (山东省环境保护厅)", "https://example.org/epa"},
                          {"World Air Quality Index Project", "https://waqi.info/"}};
        parsed.forecast = {{"2026-10-13", 37, 30, 44}, {"2026-10-14", 52, 44, 60}, {"2026-10-15", 61, 52, 75}};

        require(queryFrom(defaults()).mode == "auto", "default location is automatic");
        require(validStation("123") && validStation("1234567890123456"), "station identifiers accepted");
        for (const std::string invalid : {"", "@123", "123/", "1 2", "12345678901234567"}) {
            require(!validStation(invalid), "malformed station identifiers rejected before subscription");
        }

        for (int i = 0; i < 6; ++i) {
            const double value = std::array<double, 6>{50, 100, 150, 200, 300, 500}[size_t(i)];
            require(
                level(value) == i && std::filesystem::exists(root / L"assets/icons" / (wide(levelIcon(i)) + L".png")),
                "US AQI boundary and icon");
        }

        Json report;
        Json renders = Json::array();
        checkImageScheduling(root / L"assets", parsed);
        Renderer renderer(root / L"assets");
        for (const Spec spec : std::array<Spec, 3>{{{"1x2", 1, 2}, {"1x4", 1, 4}, {"2x4", 2, 4}}})
            for (int cellHeight : {72, 99, 144}) {
                const uint32_t w = uint32_t(spec.columns * 77), h = uint32_t(spec.rows * cellHeight);
                std::vector<uint8_t> pixels(size_t(w) * h * 4);
                CdFrame frame{sizeof(CdFrame), w, h, w * 4, pixels.size(), pixels.data(), 1};
                auto settings = defaults();
                require(renderer.renderReference(spec, settings, parsed, false, frame, {}), "frame rendered");
                bool visible = false;
                for (size_t i = 3; i < pixels.size(); i += 4) {
                    visible |= pixels[i] > 0;
                    require(pixels[i - 3] <= pixels[i] && pixels[i - 2] <= pixels[i] && pixels[i - 1] <= pixels[i],
                            "premultiplied RGBA");
                }
                require(visible && renderer.savePng(
                                       output / (wide(spec.id) + L"-" + std::to_wstring(cellHeight) + L".png"), frame),
                        "nonempty frame exported");
                renders.push_back({{"size", spec.id}, {"width", w}, {"height", h}});
            }
        report["renders"] = renders;
        report["passed"] = true;
        std::ofstream(output / "air-tests.json") << report.dump(2);
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
