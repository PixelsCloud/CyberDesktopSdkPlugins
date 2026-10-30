#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <thread>

#include "plugins/productivity-suite/Productivity.h"
using namespace productivity;

namespace {
int checks = 0;

void check(bool value, const char* message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}

struct Host {
    int redraws = 0;
    uint64_t next = 0;
    std::map<uint64_t, std::pair<CdTimerCallback, void*>> timers;
    CdSystemCallback system = nullptr;
    uint64_t systemToken = 0;
    void* systemUser = nullptr;
};

void CD_CALL redraw(void* p) {
    ++static_cast<Host*>(p)->redraws;
}

CdResult CD_CALL timer(void* p, uint32_t, CdTimerCallback cb, void* user, uint64_t* out) {
    auto& h = *static_cast<Host*>(p);
    *out = ++h.next;
    h.timers[*out] = {cb, user};
    return CD_OK;
}

CdResult CD_CALL system(void* p, uint32_t, CdSystemCallback cb, void* user, uint64_t* out) {
    auto& h = *static_cast<Host*>(p);
    h.system = cb;
    h.systemUser = user;
    *out = ++h.next;
    h.systemToken = *out;
    return CD_OK;
}

void CD_CALL unsubscribe(void* p, uint64_t id) {
    auto& h = *static_cast<Host*>(p);
    h.timers.erase(id);
    if (id == h.systemToken) {
        h.system = nullptr;
        h.systemUser = nullptr;
        h.systemToken = 0;
    }
}

CdHostApi services{sizeof(CdHostApi), CD_PLUGIN_ABI_VERSION, redraw, nullptr, system, nullptr, unsubscribe, timer};

struct Native {
    Host host;
    CdInstanceApi api{};

    Native(Spec spec) {
        CdCreateContext c{sizeof(c), CD_PLUGIN_ABI_VERSION, spec.id, "test-instance", "", &services, &host};
        check(create(spec, &c, &api, sizeof(api)) == CD_OK, "create");
    }

    ~Native() {
        if (api.instance)
            api.destroy(api.instance);
    }

    std::string save() {
        uint64_t size = 0;
        check(api.save_state(api.instance, nullptr, 0, &size) == CD_OK, "state size");
        std::string bytes(size, ' ');
        check(api.save_state(api.instance, reinterpret_cast<uint8_t*>(bytes.data()), size, &size) == CD_OK,
              "state copy");
        return bytes;
    }

    void click(double x, double y) {
        CdInput e{sizeof(e), CD_INPUT_PRESS, CD_BUTTON_LEFT, 0, x, y, 0};
        api.input(api.instance, &e);
        e.kind = CD_INPUT_RELEASE;
        e.buttons = 0;
        api.input(api.instance, &e);
    }

    void layout(int w, int h) {
        CdLayout l{};
        l.struct_size = sizeof(l);
        l.logical_width = w;
        l.logical_height = h;
        l.safe_content = {0, 0, double(w), double(h)};
        l.device_pixel_ratio = 1;
        api.layout_changed(api.instance, &l);
    }
};

struct ClipboardTest {
    HDESK desktop;
    bool passed = false;
};

DWORD WINAPI isolatedClipboard(void* pointer) {
    auto& test = *static_cast<ClipboardTest*>(pointer);
    try {
        check(SetThreadDesktop(test.desktop) != FALSE, "private test desktop");
        Native notes({"notes", Kind::Notes, 1, 4});
        notes.layout(308, 99);
        notes.click(45, 42);
        const auto read = []() {
            std::wstring text;
            check(OpenClipboard(nullptr) != FALSE, "open private clipboard");
            auto memory = GetClipboardData(CF_UNICODETEXT);
            if (memory) {
                const auto* bytes = static_cast<const wchar_t*>(GlobalLock(memory));
                if (bytes) {
                    text = bytes;
                    GlobalUnlock(memory);
                }
            }
            CloseClipboard();
            return text;
        };
        check(read() == wide(defaults(Kind::Notes).at("text").text), "copy entire note to private clipboard");
        CdSettingValue mode{sizeof(CdSettingValue), CD_SETTING_TOGGLE, "copy_all", 0};
        check(notes.api.apply_settings(notes.api.instance, &mode, 1) == CD_OK, "copy single-line mode");
        notes.click(45, 63);
        check(read() == L"今天也要把事情做好", "copy selected line to private clipboard");
        test.passed = true;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
    }
    return 0;
}

bool clipboardTest() {
    // A separate window station owns a separate clipboard. Never overwrite the
    // user's clipboard just to verify the note widget's copy action.
    const auto original = GetProcessWindowStation();
    auto station = CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);
    if (!station)
        return false;
    if (!SetProcessWindowStation(station)) {
        CloseWindowStation(station);
        return false;
    }
    auto desktop = CreateDesktopW(L"Default", nullptr, nullptr, 0,
                                  DESKTOP_CREATEWINDOW | DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS, nullptr);
    ClipboardTest state{desktop};
    HANDLE thread = desktop ? CreateThread(nullptr, 0, isolatedClipboard, &state, 0, nullptr) : nullptr;
    if (thread) {
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
    }
    SetProcessWindowStation(original);
    if (desktop)
        CloseDesktop(desktop);
    CloseWindowStation(station);
    return state.passed;
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc > 1 && std::wstring(argv[1]) == L"--live-rss") {
            throw std::runtime_error("Live RSS checks now belong to the private host content service");
        }
        if (argc > 1 && std::wstring(argv[1]) == L"--clipboard-isolated") {
            check(clipboardTest(), "isolated clipboard integration");
            std::cout << "Isolated clipboard integration passed; user clipboard "
                         "unchanged\n";
            return 0;
        }
        check(driveRoot("C:\\") && driveRoot("d:/") && !driveRoot("\\\\server\\share") && !driveRoot("C:folder"),
              "drive root validation");
        check(webUrl("https://example.org/feed.xml") && !webUrl("file:///x") &&
                  !webUrl("https://user:pass@example.org/") && !webUrl("javascript:evil") &&
                  !webUrl("https://example.org/\r\nevil"),
              "URL validation");
        check(daysUntil("2027-01-01", false, 2026, 10, 13) == 80, "date difference");
        check(daysUntil("2024-02-29", true, 2026, 10, 13) == 504, "annual leap date");
        check(daysUntil("2026-10-12", false, 2026, 10, 13) == -1, "elapsed day");
        check(daysUntil("2026-10-13", true, 2026, 10, 13) == 0, "anniversary today");
        bool invalid = false;
        try {
            daysUntil("2026-02-30", false, 2026, 1, 1);
        } catch (...) {
            invalid = true;
        }
        check(invalid, "invalid calendar date");
        Data tasks;
        reconcileTasks(tasks, "A\nA\nB");
        tasks.tasks[1].done = true;
        reconcileTasks(tasks, "B\nA\nA\nC");
        check(tasks.tasks.size() == 4 && !tasks.tasks[1].done && tasks.tasks[2].done, "duplicate task reconciliation");
        const auto encoded = stateBytes(tasks, Kind::Todo);
        Data restored;
        check(restoreBytes(restored, Kind::Todo, encoded) && restored.tasks == tasks.tasks, "task persistence");
        const auto prior = restored.tasks;
        check(!restoreBytes(restored, Kind::Todo, "{bad") && prior == restored.tasks, "invalid state atomic");
        Data clock;
        clock.running = true;
        clock.deadline = 1000;
        auto settings = defaults(Kind::Timer);
        advanceTimer(clock, settings, 900);
        check(clock.remaining == 100 && clock.running, "deadline advance");
        advanceTimer(clock, settings, 1001);
        check(clock.finished && !clock.running && clock.completed == 1, "timer completion");
        advanceTimer(clock, settings, 2000);
        check(clock.completed == 1, "completion not repeated");
        CdSettingValue automatic{sizeof(CdSettingValue), CD_SETTING_TOGGLE, "auto_break", 1};
        settings.apply(&automatic, 1);
        clock = {};
        clock.running = true;
        clock.deadline = 1;
        advanceTimer(clock, settings, 2);
        check(clock.resting && clock.running && clock.remaining == 300, "automatic break");
        advanceTimer(clock, settings, 302);
        check(clock.finished && !clock.running, "break completion");
        for (int width : {154, 186, 218, 250, 314}) {
            const Spec spec{"object-timer", Kind::Timer, 2, 2, true};
            Native n(spec);
            const int height = (width + 6) * 5 / 4 - 6;
            n.layout(width, height);
            const auto hits = hitRegions(spec, {}, defaults(Kind::Timer, true), {0, 0, double(width), double(height)});
            check(hits.size() == 2 && hits[0].rect.width >= 16 && hits[1].rect.width >= 16,
                  "freestanding stopwatch keeps two accessible button targets at every fixed preset");
            n.click(hits[0].rect.x + hits[0].rect.width / 2, hits[0].rect.y + hits[0].rect.height / 2);
            check(nlohmann::json::parse(n.save())["running"], "stopwatch physical play button starts the deadline");
            n.click(hits[1].rect.x + hits[1].rect.width / 2, hits[1].rect.y + hits[1].rect.height / 2);
            const auto state = nlohmann::json::parse(n.save());
            check(!state["running"].get<bool>() && state["remaining"] == 1500,
                  "stopwatch physical reset button restores the configured focus duration");
            const auto before = n.save();
            n.click(0, 0);
            check(n.save() == before, "transparent stopwatch margins do not activate controls");
        }
        {
            Native n({"timer", Kind::Timer, 1, 2, true});
            n.layout(154, 99);
            n.click(45, 76);
            auto state = nlohmann::json::parse(n.save());
            check(state["running"] == true, "timer start click");
            n.click(45, 76);
            state = nlohmann::json::parse(n.save());
            check(state["running"] == false, "timer pause click");
            n.click(45, 76);
            const auto saved = n.save();
            n.api.set_paused(n.api.instance, 1);
            n.click(45, 76);
            check(n.save() == saved && n.host.timers.empty(), "paused input and subscriptions");
            Native other({"timer", Kind::Timer, 1, 2});
            check(other.api.restore_state(other.api.instance, reinterpret_cast<const uint8_t*>(saved.data()),
                                          saved.size()) == CD_OK,
                  "timer restore");
            check(nlohmann::json::parse(other.save())["running"] == true, "timer running survives restore");
        }
        {
            Native n({"todo", Kind::Todo, 2, 4});
            n.layout(308, 198);
            n.click(50, 50);
            check(nlohmann::json::parse(n.save())["tasks"][0]["done"] == true, "task check");
            const auto saved = n.save();
            Native other({"todo", Kind::Todo, 2, 4});
            check(other.api.restore_state(other.api.instance, reinterpret_cast<const uint8_t*>(saved.data()),
                                          saved.size()) == CD_OK &&
                      other.save() == saved,
                  "task native roundtrip");
            check(other.api.restore_state(other.api.instance, nullptr, 0) == CD_OK &&
                      !nlohmann::json::parse(other.save())["tasks"][0]["done"].get<bool>(),
                  "empty state resets task completion");
            CdInput event{sizeof(event), CD_INPUT_PRESS, CD_BUTTON_LEFT, 0, 50, 50, 0};
            n.api.input(n.api.instance, &event);
            event.kind = CD_INPUT_MOVE;
            event.x = 70;
            n.api.input(n.api.instance, &event);
            event.kind = CD_INPUT_RELEASE;
            event.x = 50;
            n.api.input(n.api.instance, &event);
            check(n.save() == saved, "drag is not a click");
            CdSettingValue changes[2]{{sizeof(CdSettingValue), CD_SETTING_TEXT, "tasks", 0, 0, "Changed"},
                                      {sizeof(CdSettingValue), CD_SETTING_CHOICE, "language", 0, 0, "de"}};
            check(n.api.apply_settings(n.api.instance, changes, 2) == CD_ERROR_ARGUMENT && n.save() == saved,
                  "atomic settings");
        }
        {
            Native n({"date", Kind::Countdown, 1, 2});
            CdSettingValue date{sizeof(CdSettingValue), CD_SETTING_TEXT, "date", 0, 0, "2026-02-30"};
            check(n.api.apply_settings(n.api.instance, &date, 1) == CD_ERROR_ARGUMENT, "invalid date setting");
        }
        {
            Native n({"network", Kind::Network, 1, 4, true});
            const CdSettingValue appearance{sizeof(appearance), CD_SETTING_CHOICE, "appearance", 0, 0, "mechanical"};
            check(n.api.apply_settings(n.api.instance, &appearance, 1) == CD_ERROR_ARGUMENT,
                  "mechanical plugin has fixed appearance");
            CdSystemInfo metrics{sizeof(metrics), 0, 0, 0, 0, 512, 4096};
            check(n.host.system != nullptr, "network subscribes host sampler");
            n.host.system(n.host.systemUser, &metrics);
            metrics.network_down_bytes_per_second *= 4;
            n.host.system(n.host.systemUser, &metrics);
            BOOL animations = TRUE;
            SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
            check(n.host.timers.size() == (animations ? 1u : 0u), "network schedules only a changed-needle transition");
            if (animations) {
                Sleep(620);
                const auto callbacks = n.host.timers;
                for (const auto& [token, callback] : callbacks) {
                    if (n.host.timers.contains(token)) {
                        callback.first(callback.second, GetTickCount64());
                    }
                }
                check(n.host.timers.empty() && n.host.system,
                      "network settles while preserving its sampler subscription");
            }
            const CdSettingValue reduced{sizeof(reduced), CD_SETTING_TOGGLE, "reduced_motion", 1};
            check(n.api.apply_settings(n.api.instance, &reduced, 1) == CD_OK, "network reduced motion setting");
            metrics.network_down_bytes_per_second *= 2;
            n.host.system(n.host.systemUser, &metrics);
            check(n.host.timers.empty(), "reduced network motion never schedules decorative frames");
            const auto redraws = n.host.redraws;
            n.api.set_paused(n.api.instance, 1);
            check(n.host.system == nullptr, "network pause unsubscribes");
            check(n.host.redraws == redraws, "pause does not redraw");
        }
        Renderer renderer;
        for (int kind = 0; kind < 7; ++kind)
            for (const auto dimensions :
                 std::vector<std::pair<int, int>>{{154, 99}, {308, 99}, {154, 198}, {308, 198}, {130, 258}}) {
                const auto [w, h] = dimensions;
                auto s = defaults(Kind(kind));
                Data d;
                d.valid = true;
                d.total = 100;
                d.free = 40;
                d.available = 40;
                d.articles = {{"First", "https://example.org/first", "Tue, 13 Oct 2026 12:00:00 GMT"},
                              {"Second", "https://example.org/second", ""}};
                reconcileTasks(d, "First\nSecond\nThird");
                std::vector<uint8_t> rgba(size_t(w) * h * 4);
                CdFrame f{sizeof(f), uint32_t(w), uint32_t(h), uint32_t(w * 4), rgba.size(), rgba.data(), 1};
                check(renderer.render({"render", Kind(kind), h > 150 ? 2 : 1, w > 200 ? 4 : 2}, s, d, f, {}),
                      "render different aspect ratios");
                check(std::any_of(rgba.begin(), rgba.end(),
                                  [](uint8_t b) {
                                      return b != 0;
                                  }),
                      "visible pixels");
                bool premultiplied = true;
                for (size_t p = 0; p < rgba.size(); p += 4)
                    premultiplied &= rgba[p] <= rgba[p + 3] && rgba[p + 1] <= rgba[p + 3] && rgba[p + 2] <= rgba[p + 3];
                check(premultiplied, "premultiplied RGBA");
            }
        std::cout << checks << " focused productivity checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << " after " << checks << " checks\n";
        return 1;
    }
}
