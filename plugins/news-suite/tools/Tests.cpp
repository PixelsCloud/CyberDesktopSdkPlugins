#include <Windows.h>

#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <thread>

#include "NewsCatalog.h"
#include "plugins/news-suite/tools/Samples.h"
#ifdef CD_NEWS_TEST_SERVICES
#include "tools/Runtime.h"
#endif

namespace {
using namespace news;
using Json = nlohmann::json;
int checks = 0, renders = 0;

void check(bool value, const char* message) {
    ++checks;
    if (!value) {
        throw std::runtime_error(message);
    }
}

template <class F>
void rejects(F&& action, const char* message) {
    bool rejected = false;
    try {
        action();
    } catch (...) {
        rejected = true;
    }
    check(rejected, message);
}

void models() {
    auto m = sample(Kind::Reader, true);
    m.key = query(Kind::Reader, defaults(Kind::Reader)).key();
    markRead(m, m.items[0].url);
    auto restored = Model{};
    check(restoreBytes(restored, stateBytes(m)) && restored.items == m.items && restored.saved == m.saved &&
              restored.read == m.read,
          "state roundtrip");
    const auto before = stateBytes(restored);
    check(!restoreBytes(restored, "{invalid") && before == stateBytes(restored), "atomic invalid state restore");
    auto bad = Json::parse(before);
    bad["items"][0][1] = "file:///C:/secret";
    check(!restoreBytes(restored, bad.dump()) && before == stateBytes(restored), "state validates URL");
    const auto original = m.items;
    accept(m, {m.key, "network", {}, 0, false});
    check(m.items == original && !m.loading && m.error == "network", "network failure retains snapshot");
    toggleSaved(m, m.items[0]);
    check(m.saved.size() == 2, "save item");
    toggleSaved(m, m.items[0]);
    check(m.saved.size() == 1, "remove saved item");
    auto s = defaults(Kind::Reader);
    const CdSettingValue settings[]{{sizeof(CdSettingValue), CD_SETTING_CHOICE, "source", 0, 0, "custom"},
                                    {sizeof(CdSettingValue), CD_SETTING_TEXT, "feeds", 0, 0,
                                     "https://example.org/a\nhttps://example.org/b\nhttps://example.org/a"}};
    check(s.apply(settings, 2) == CD_OK && query(Kind::Reader, s).urls.size() == 2, "custom feeds deduplicated");
    Query q;
    q.kind = Kind::Github;
    q.language = "c++";
    q.period = "weekly";
    check(q.address() == "https://github.com/trending/c%2B%2B?since=weekly", "encoded programming language");
}

#ifdef CD_NEWS_TEST_SERVICES
void lifecycle() {
    for (const auto& c : catalog) {
        cdsdk::MockHost host;
        host.capabilities.insert("CONTENT");
        CdCreateContext context{sizeof(context), CD_PLUGIN_ABI_VERSION, c.spec.id, "test", "", &host.api, &host};
        CdInstanceApi instance{};
        check(create(c.spec, &context, &instance, sizeof(instance)) == CD_OK, "create native plugin");
        auto cache = sample(c.spec.kind, true);
        cache.key = query(c.spec.kind, defaults(c.spec.kind)).key();
        const auto bytes = stateBytes(cache);
        check(instance.restore_state(instance.instance, reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()) ==
                  CD_OK,
              "restore cached plugin data");
        check(std::none_of(host.subscriptions.begin(), host.subscriptions.end(),
                           [](const auto& entry) {
                               return entry.second.timer != nullptr;
                           }),
              "news has no polling timer");
        CdLayout layout{
            sizeof(layout),   0, 0, 0, nullptr, 288, c.spec.rows * 100., 1, {4, 4, 280, c.spec.rows * 100. - 8},
            {30, 30, 30, 255}};
        instance.layout_changed(instance.instance, &layout);
        const auto snapshot = [&] {
            uint64_t size = 0;
            instance.save_state(instance.instance, nullptr, 0, &size);
            std::string bytes(size_t(size), ' ');
            instance.save_state(instance.instance, reinterpret_cast<uint8_t*>(bytes.data()), size, &size);
            return Json::parse(bytes);
        };
        const auto hits = hitRegions(c.spec, cache, layout.safe_content);
        const auto star = std::find_if(hits.begin(), hits.end(), [](const auto& h) {
            return h.action == 200;
        });
        check(star != hits.end(), "first item bookmark hit region");
        CdInput event{sizeof(event),
                      CD_INPUT_PRESS,
                      CD_BUTTON_LEFT,
                      0,
                      star->area.x + star->area.width / 2,
                      star->area.y + star->area.height / 2};
        instance.input(instance.instance, &event);
        event.kind = CD_INPUT_MOVE;
        event.x += 12;
        instance.input(instance.instance, &event);
        event.x -= 12;
        event.kind = CD_INPUT_RELEASE;
        event.buttons = 0;
        instance.input(instance.instance, &event);
        check(snapshot().at("saved").size() == 1, "drag cancels bookmark click");
        event.kind = CD_INPUT_PRESS;
        event.buttons = CD_BUTTON_LEFT;
        instance.input(instance.instance, &event);
        event.kind = CD_INPUT_RELEASE;
        event.buttons = 0;
        instance.input(instance.instance, &event);
        check(snapshot().at("saved").size() == 2, "native input saves clicked item");
        const auto bad = CdSettingValue{sizeof(CdSettingValue), CD_SETTING_INTEGER, "refresh_minutes", 0};
        check(instance.apply_settings(instance.instance, &bad, 1) == CD_ERROR_ARGUMENT, "invalid interval rejected");
        const CdSettingValue invalid[]{{sizeof(CdSettingValue), CD_SETTING_CHOICE, "language", 0, 0, "en"},
                                       {sizeof(CdSettingValue), CD_SETTING_TEXT, "feeds", 0, 0, "file:///C:/private"}};
        check(instance.apply_settings(instance.instance, invalid, 2) == CD_ERROR_ARGUMENT,
              "invalid custom feed is atomic");
        CdSettingValue output{};
        instance.get_setting(instance.instance, "language", &output, sizeof(output));
        check(std::string(output.text) == "auto", "failed batch did not alter other settings");
        uint64_t stateSize = 0;
        check(instance.save_state(instance.instance, nullptr, 0, &stateSize) == CD_OK && stateSize > 100,
              "ABI snapshot size");
        check(instance.save_state(instance.instance, reinterpret_cast<uint8_t*>(&output), 1, &stateSize) ==
                  CD_ERROR_BUFFER,
              "ABI short output rejected");
        instance.set_paused(instance.instance, 1);
        check(host.subscriptions.empty(), "hidden plugin unsubscribes content");
        instance.set_paused(instance.instance, 0);
        check(std::count_if(host.subscriptions.begin(), host.subscriptions.end(),
                            [](const auto& item) {
                                return item.second.content != nullptr;
                            }) == 1,
              "visible plugin resumes content");
        instance.destroy(instance.instance);
        check(host.subscriptions.empty(), "all host subscriptions removed");
    }
}

#endif

void rendering() {
    Renderer renderer;
    for (const char* mode : {"scroll", "pages"}) {
        for (const auto& c : catalog) {
            auto s = defaults(c.spec.kind);
            const CdSettingValue browse{sizeof(browse), CD_SETTING_CHOICE, "list_mode", 0, 0, mode};
            s.apply(&browse, 1);
            for (const double dpi : {1., 1.5, 2.}) {
                for (const int cellHeight : {72, 100, 150}) {
                    for (const bool light : {false, true}) {
                        for (const bool clear : {false, true}) {
                            const CdSettingValue settings[]{
                                {sizeof(CdSettingValue), CD_SETTING_CHOICE, "lighting", 0, 0, light ? "day" : "night"},
                                {sizeof(CdSettingValue), CD_SETTING_TOGGLE, "show_background", !clear}};
                            s.apply(settings, 2);
                            auto m = sample(c.spec.kind, true);
                            const uint32_t width = uint32_t(288 * dpi),
                                           height = uint32_t(c.spec.rows * cellHeight * dpi);
                            const uint32_t stride = width * 4 + 16;
                            std::vector<uint8_t> pixels(size_t(stride) * height + 64, 0xcd);
                            CdFrame f{sizeof(f), width, height, stride, uint64_t(stride) * height, pixels.data(), dpi};
                            const CdRect safe{3, 3, 282, c.spec.rows * double(cellHeight) - 6};
                            if (cellHeight == 150) {
                                m.items[0].title = "مرحبا بالعالم — أخبار التقنية المفتوحة";
                            }
                            check(renderer.render(c.spec, s, m, f, safe), "native rendering");
                            ++renders;
                            for (uint32_t y = 0; y < height; ++y) {
                                for (uint32_t x = 0; x < width; ++x) {
                                    const auto* v = pixels.data() + size_t(y) * stride + x * 4;
                                    check(v[0] <= v[3] && v[1] <= v[3] && v[2] <= v[3], "premultiplied alpha");
                                    if (!x || !y) {
                                        check(v[3] == 0, "safe content clipping");
                                    }
                                }
                                check(pixels[size_t(y) * stride + width * 4] == 0xcd, "row padding untouched");
                            }
                            check(pixels.back() == 0xcd, "buffer guard untouched");
                            const auto hits = hitRegions(c.spec, m, safe);
                            for (const auto& hit : hits) {
                                check(hit.area.x >= safe.x && hit.area.y >= safe.y &&
                                          hit.area.x + hit.area.width <= safe.x + safe.width + .01 &&
                                          hit.area.y + hit.area.height <= safe.y + safe.height + .01,
                                      "hit regions inside safe content");
                            }
                        }
                    }
                }
            }
        }
    }
}

void responsiveLayout() {
    for (const auto& entry : catalog) {
        const auto m = sample(entry.spec.kind, true);
        for (double width : {144., 200., 288., 480.}) {
            for (double height : {144., 200., 400.}) {
                const CdRect safe{3, 5, width, height};
                const auto g = geometry(entry.spec, safe);
                check(g.top <= g.bottom && g.bottom <= height && g.rowHeight >= 0, "responsive article geometry");
                const auto hits = hitRegions(entry.spec, m, safe);
                double previousTabY = 0;
                for (const auto& hit : hits) {
                    const auto r = hit.area;
                    check(r.width > 0 && r.height > 0 && r.x >= safe.x && r.y >= safe.y &&
                              r.x + r.width <= safe.x + width + .01 && r.y + r.height <= safe.y + height + .01,
                          "responsive hit regions clipped to safe content");
                    if (hit.action >= 10 && hit.action < 20) {
                        check(r.y >= previousTabY && r.y + r.height <= safe.y + g.top, "wrapped tabs precede articles");
                        previousTabY = r.y;
                    }
                }
            }
        }
    }
    const Spec spec{"test", Kind::Reader, 2, 4};
    check(geometry(spec, {0, 0, 144, 400}).top > geometry(spec, {0, 0, 288, 400}).top,
          "source tabs reflow and move article content");
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc > 1 && std::wstring(argv[1]) == L"--live") {
            throw std::runtime_error("Live checks now belong to the private host content service");
        }
        models();
#ifdef CD_NEWS_TEST_SERVICES
        lifecycle();
#endif
        rendering();
        responsiveLayout();
        const Json report{
            {"passed", true}, {"checks", checks}, {"nativeRenders", renders}, {"networkInDefaultTests", false}};
        if (argc > 1) {
            std::ofstream(std::filesystem::path(argv[1])) << report.dump(2);
        }
        std::cout << report.dump() << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << '\n';
        return 1;
    }
}
