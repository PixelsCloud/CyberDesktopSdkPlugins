#include <array>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "plugins/productivity-suite/Productivity.h"
#include "plugins/productivity-suite/ThemedController.h"

namespace {
using namespace productivity;
int views = 0;
int frames = 0;
bool failBinding = false;

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class View final : public themed::View {
public:
    View();
    ~View() override;
    void bind(const CdCreateContext&, std::function<void(uint32_t)>) override;
    void input(const CdInput&) override;
    void cancelInput() override;
    void pause(bool) override;
    void syncInput(Spec, const cdsdk::Settings&, const Data&, CdRect) override;
    bool render(Spec, const cdsdk::Settings&, const Data&, const CdFrame&, CdRect) override;
};

View::View() {
    ++views;
}

View::~View() {
    --views;
}

void View::bind(const CdCreateContext&, std::function<void(uint32_t)>) {
    if (failBinding) {
        throw std::runtime_error("injected binding failure");
    }
}

void View::input(const CdInput&) {}

void View::cancelInput() {}

void View::pause(bool) {}

void View::syncInput(Spec, const cdsdk::Settings&, const Data&, CdRect) {}

bool View::render(Spec, const cdsdk::Settings&, const Data&, const CdFrame&, CdRect) {
    ++frames;
    return true;
}

std::unique_ptr<themed::View> makeView() {
    return std::make_unique<View>();
}

struct Host {
    std::map<uint64_t, std::pair<CdTimerCallback, void*>> timers;
    uint64_t next = 0;
    uint64_t system = 0;
    bool fail = false;
    static void CD_CALL redraw(void*);
    static void CD_CALL stop(void*, uint64_t);
    static CdResult CD_CALL timer(void*, uint32_t, CdTimerCallback, void*, uint64_t*);
    static CdResult CD_CALL subscribeSystem(void*, uint32_t, CdSystemCallback, void*, uint64_t*);
    bool empty() const;
};

void CD_CALL Host::redraw(void*) {}

void CD_CALL Host::stop(void* context, uint64_t token) {
    auto& host = *static_cast<Host*>(context);
    host.timers.erase(token);
    if (host.system == token) {
        host.system = 0;
    }
}

CdResult CD_CALL Host::timer(void* context, uint32_t, CdTimerCallback callback, void* user, uint64_t* token) {
    auto& host = *static_cast<Host*>(context);
    if (host.fail) {
        return CD_ERROR_INTERNAL;
    }
    *token = ++host.next;
    host.timers.emplace(*token, std::pair{callback, user});
    return CD_OK;
}

CdResult CD_CALL Host::subscribeSystem(void* context, uint32_t, CdSystemCallback, void*, uint64_t* token) {
    auto& host = *static_cast<Host*>(context);
    if (host.fail) {
        return CD_ERROR_INTERNAL;
    }
    *token = host.system = ++host.next;
    return CD_OK;
}

bool Host::empty() const {
    return timers.empty() && !system;
}

void scenario(Kind kind, bool mechanical, const char* font) {
    Host host;
    const CdHostApi api{
        sizeof(CdHostApi), CD_PLUGIN_ABI_VERSION, Host::redraw, nullptr, Host::subscribeSystem, nullptr, Host::stop,
        Host::timer};
    const CdCreateContext context{sizeof(CdCreateContext), CD_PLUGIN_ABI_VERSION, "test", "test", "", &api, &host};
    const Spec spec{"test", kind, 2, 4, mechanical};
    CdInstanceApi instance{};
    check(themed::create(spec, nullptr, &instance, sizeof(instance), makeView) == CD_ERROR_ARGUMENT,
          "invalid context was accepted");
    check(themed::create(spec, &context, &instance, sizeof(instance), nullptr) == CD_ERROR_ARGUMENT,
          "missing factory was accepted");
    failBinding = true;
    check(themed::create(spec, &context, &instance, sizeof(instance), makeView) == CD_ERROR_INTERNAL,
          "binding failure was accepted");
    check(views == 0 && host.empty(), "binding exception leaked resources");
    failBinding = false;
    check(themed::create(spec, &context, &instance, sizeof(instance), makeView, font) == CD_OK,
          "controller creation failed");
    check(views == 1, "view is not exclusively owned");
    if (font) {
        CdSettingValue value{};
        check(instance.get_setting(instance.instance, "font_family", &value, sizeof(value)) == CD_OK && value.text &&
                  std::strcmp(value.text, font) == 0,
              "theme font default was lost");
    }
    instance.set_paused(instance.instance, 1);
    check(host.empty(), "pause retained subscriptions");
    instance.set_paused(instance.instance, 0);
    const auto count = host.timers.size();
    instance.set_paused(instance.instance, 0);
    check(host.timers.size() == count, "repeated resume duplicated timers");
    std::array<uint8_t, 64 * 64 * 4> rgba{};
    const CdFrame frame{sizeof(CdFrame), 64, 64, 256, rgba.size(), rgba.data(), 1};
    for (int i = 0; i < 10; ++i) {
        check(instance.render(instance.instance, &frame) == CD_OK, "view dispatch failed");
    }
    check(views == 1, "frame dispatch allocated a view");
    uint64_t size = 0;
    check(instance.save_state(instance.instance, nullptr, 0, &size) == CD_OK, "state sizing failed");
    std::vector<uint8_t> bytes(size);
    check(instance.save_state(instance.instance, bytes.data(), bytes.size(), &size) == CD_OK &&
              instance.restore_state(instance.instance, bytes.data(), bytes.size()) == CD_OK,
          "state roundtrip failed");
    instance.destroy(instance.instance);
    check(views == 0 && host.empty(), "destroy retained resources");
    if (kind != Kind::Disk) {
        host.fail = true;
        check(themed::create(spec, &context, &instance, sizeof(instance), makeView) != CD_OK,
              "subscription failure was accepted");
        check(views == 0 && host.empty(), "startup failure retained resources");
    }
}
}  // namespace

int main() try {
    for (const char* font : {static_cast<const char*>(nullptr), "Bahnschrift"}) {
        for (bool mechanical : {false, true}) {
            for (auto kind :
                 {Kind::Timer, Kind::Todo, Kind::Countdown, Kind::Network, Kind::Disk, Kind::Notes, Kind::Rss}) {
                scenario(kind, mechanical, font);
            }
        }
    }
    check(frames == 280, "unexpected frame dispatch count");
    std::cout << "PASS: 28 productivity lifecycle scenarios, 280 frame dispatches\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
