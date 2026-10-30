#include <array>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <utility>

#include "plugins/system-info-suite/Monitor.h"
#include "plugins/system-info-suite/ThemedController.h"

namespace {
using namespace monitorplugin;
int views = 0, paints = 0;
bool imageReady = false, bindFails = false;

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class View final : public themed::View {
public:
    View();
    ~View() override;
    void bind(const CdCreateContext&, std::function<void()>) override;
    void pause(bool) override;
    bool poll() override;
    bool render(Spec, const Options&, Values, const CdFrame&, CdRect) override;
};

View::View() {
    ++views;
}

View::~View() {
    --views;
}

void View::bind(const CdCreateContext&, std::function<void()>) {
    if (bindFails) {
        throw std::runtime_error("injected binding failure");
    }
}

void View::pause(bool) {}

bool View::poll() {
    return std::exchange(imageReady, false);
}

bool View::render(Spec, const Options&, Values, const CdFrame&, CdRect) {
    ++paints;
    return true;
}

std::unique_ptr<themed::View> makeView() {
    return std::make_unique<View>();
}

struct Host {
    uint64_t next = 0, system = 0;
    CdSystemCallback callback = nullptr;
    void* user = nullptr;
    std::map<uint64_t, std::pair<CdTimerCallback, void*>> timers;
    bool timerFails = false;
    static void CD_CALL redraw(void*);
    static void CD_CALL stop(void*, uint64_t);
    static CdResult CD_CALL timer(void*, uint32_t, CdTimerCallback, void*, uint64_t*);
    static CdResult CD_CALL subscribe(void*, uint32_t, CdSystemCallback, void*, uint64_t*);
    bool empty() const;
};

void CD_CALL Host::redraw(void*) {}

void CD_CALL Host::stop(void* context, uint64_t token) {
    auto& host = *static_cast<Host*>(context);
    host.timers.erase(token);
    if (host.system == token) {
        host.system = 0;
        host.callback = nullptr;
        host.user = nullptr;
    }
}

CdResult CD_CALL Host::timer(void* context, uint32_t, CdTimerCallback callback, void* user, uint64_t* token) {
    auto& host = *static_cast<Host*>(context);
    if (host.timerFails) {
        return CD_ERROR_INTERNAL;
    }
    *token = ++host.next;
    host.timers.emplace(*token, std::pair{callback, user});
    return CD_OK;
}

CdResult CD_CALL Host::subscribe(void* context, uint32_t, CdSystemCallback callback, void* user, uint64_t* token) {
    auto& host = *static_cast<Host*>(context);
    *token = host.system = ++host.next;
    host.callback = callback;
    host.user = user;
    return CD_OK;
}

bool Host::empty() const {
    return timers.empty() && !system && !callback && !user;
}

void scenario(int rows, int columns, bool mechanical, bool singleMemory) {
    Host host;
    const CdHostApi api{
        sizeof(CdHostApi), CD_PLUGIN_ABI_VERSION, Host::redraw, nullptr, Host::subscribe, nullptr, Host::stop,
        Host::timer};
    const CdCreateContext context{sizeof(CdCreateContext), CD_PLUGIN_ABI_VERSION, "test", "test", "", &api, &host};
    const Spec spec{"test", rows, columns, mechanical};
    CdInstanceApi instance{};
    check(themed::create(spec, nullptr, &instance, sizeof(instance), makeView) == CD_ERROR_ARGUMENT,
          "invalid context accepted");
    bindFails = true;
    check(themed::create(spec, &context, &instance, sizeof(instance), makeView) == CD_ERROR_INTERNAL,
          "binding failure accepted");
    check(host.empty() && views == 0, "binding failure leaked resources");
    bindFails = false;
    host.timerFails = true;
    check(themed::create(spec, &context, &instance, sizeof(instance), makeView) != CD_OK,
          "partial startup failure accepted");
    check(host.empty() && views == 0, "partial startup retained system subscription");
    host.timerFails = false;
    check(themed::create(spec, &context, &instance, sizeof(instance), makeView, singleMemory) == CD_OK,
          "monitor creation failed");
    CdSettingValue value{};
    check(instance.get_setting(instance.instance, "primary", &value, sizeof(value)) == CD_OK && value.text &&
              std::strcmp(value.text, singleMemory && columns == 1 ? "ram" : "cpu") == 0,
          "primary metric default changed");
    instance.set_paused(instance.instance, 1);
    check(host.empty(), "pause retained callbacks");
    instance.set_paused(instance.instance, 0);
    const auto active = host.timers.size();
    instance.set_paused(instance.instance, 0);
    check(host.timers.size() == active && host.system, "resume duplicated subscriptions");
    const CdSettingValue animation{sizeof(CdSettingValue), CD_SETTING_TOGGLE, "animate", 0};
    check(instance.apply_settings(instance.instance, &animation, 1) == CD_OK, "disable interpolation");
    const CdSystemInfo values{sizeof(CdSystemInfo), 50, 25, 1, 2, 0, 0};
    host.callback(host.user, &values);
    std::array<uint8_t, 16 * 16 * 4> rgba{};
    const CdFrame frame{sizeof(CdFrame), 16, 16, 64, rgba.size(), rgba.data(), 1};
    const auto before = paints;
    check(instance.render(instance.instance, &frame) == CD_OK, "monitor frame failed");
    check(instance.render(instance.instance, &frame) == CD_OK && paints == before + 1,
          "unchanged frame bypassed content cache");
    imageReady = true;
    check(instance.render(instance.instance, &frame) == CD_OK && paints == before + 2,
          "async image readiness did not invalidate content cache");
    instance.destroy(instance.instance);
    check(host.empty() && views == 0, "monitor destruction retained resources");
}
}  // namespace

int main() try {
    for (bool singleMemory : {false, true}) {
        for (bool mechanical : {false, true}) {
            for (auto dimensions : {std::pair{1, 1}, std::pair{1, 2}, std::pair{1, 4}, std::pair{2, 2}}) {
                scenario(dimensions.first, dimensions.second, mechanical, singleMemory);
            }
        }
    }
    std::cout << "PASS: 16 monitor lifecycle/default/cache scenarios\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
