#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "plugins/widget-suite/ThemedController.h"
#include "plugins/widget-suite/Widget.h"

namespace {
using namespace widgetplugin;
int factories = 0;
int liveViews = 0;
int paints = 0;

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
    void syncInput(Spec, const Media&, CdRect, const Photo&, AlbumPlayback) override;
    bool render(Spec, const cdsdk::Settings&, const Spectrum&, const Photo&, const Photo&, float, Date, int,
                const CdFrame&, CdRect, const Media&, int, const CdInputStats&, const CounterDrums*,
                AlbumPlayback) override;
    cybercanvas::Rect photoRegion(CdRect) const override;
};

View::View() {
    ++liveViews;
}

View::~View() {
    --liveViews;
}

void View::bind(const CdCreateContext&, std::function<void(uint32_t)>) {}

void View::input(const CdInput&) {}

void View::cancelInput() {}

void View::pause(bool) {}

void View::syncInput(Spec, const Media&, CdRect, const Photo&, AlbumPlayback) {}

bool View::render(Spec, const cdsdk::Settings&, const Spectrum&, const Photo&, const Photo&, float, Date, int,
                  const CdFrame&, CdRect, const Media&, int, const CdInputStats&, const CounterDrums*, AlbumPlayback) {
    ++paints;
    return true;
}

cybercanvas::Rect View::photoRegion(CdRect) const {
    return {0, 0, 64, 64};
}

std::unique_ptr<themed::View> makeView() {
    ++factories;
    return std::make_unique<View>();
}

std::unique_ptr<themed::View> emptyView() {
    return {};
}

struct Host {
    std::set<uint64_t> subscriptions;
    uint64_t next = 1;
    int redraws = 0;
    bool fail = false;
    int remaining = -1;
    static void CD_CALL redraw(void*);
    static void CD_CALL stop(void*, uint64_t);
    static CdResult CD_CALL audio(void*, uint32_t, CdAudioCallback, void*, uint64_t*);
    static CdResult CD_CALL timer(void*, uint32_t, CdTimerCallback, void*, uint64_t*);
    static CdResult CD_CALL media(void*, const char*, CdMediaCallback, void*, uint64_t*);
    static CdResult CD_CALL stats(void*, CdInputStatsCallback, void*, uint64_t*);
    static CdResult CD_CALL command(void*, uint64_t, uint32_t, double);
    static CdResult subscribe(void*, uint64_t*);
    CdInputStatsHostApi api{
        {{sizeof(CdInputStatsHostApi), CD_PLUGIN_ABI_VERSION, redraw, nullptr, nullptr, audio, stop, timer},
         media,
         command},
        stats};
};

void CD_CALL Host::redraw(void* context) {
    ++static_cast<Host*>(context)->redraws;
}

void CD_CALL Host::stop(void* context, uint64_t token) {
    static_cast<Host*>(context)->subscriptions.erase(token);
}

CdResult Host::subscribe(void* context, uint64_t* token) {
    auto& host = *static_cast<Host*>(context);
    if (host.fail || host.remaining == 0) {
        return CD_ERROR_INTERNAL;
    }
    if (host.remaining > 0) {
        --host.remaining;
    }
    *token = host.next++;
    host.subscriptions.insert(*token);
    return CD_OK;
}

CdResult CD_CALL Host::audio(void* c, uint32_t, CdAudioCallback, void*, uint64_t* t) {
    return subscribe(c, t);
}

CdResult CD_CALL Host::timer(void* c, uint32_t, CdTimerCallback, void*, uint64_t* t) {
    return subscribe(c, t);
}

CdResult CD_CALL Host::media(void* c, const char*, CdMediaCallback, void*, uint64_t* t) {
    return subscribe(c, t);
}

CdResult CD_CALL Host::stats(void* c, CdInputStatsCallback, void*, uint64_t* t) {
    return subscribe(c, t);
}

CdResult CD_CALL Host::command(void*, uint64_t, uint32_t, double) {
    return CD_OK;
}

void lifecycle(Kind kind, bool mechanical) {
    Host host;
    const Spec spec{"test", kind, 1, 4, mechanical};
    CdCreateContext context{
        sizeof(CdCreateContext), CD_PLUGIN_ABI_VERSION, "test", "instance", "", &host.api.media.base, &host};
    CdInstanceApi instance{};
    check(themed::create(spec, nullptr, &instance, sizeof(instance), makeView) == CD_ERROR_ARGUMENT && factories == 0,
          "invalid context constructed a view");
    check(themed::create(spec, &context, &instance, sizeof(instance), nullptr) == CD_ERROR_ARGUMENT,
          "missing view factory");
    check(themed::create(spec, &context, &instance, sizeof(instance), emptyView) == CD_ERROR_INTERNAL,
          "empty view factory result");
    check(themed::create(spec, &context, &instance, sizeof(instance), makeView) == CD_OK, "create themed controller");
    check(liveViews == 1, "controller must exclusively own one view");
    instance.set_paused(instance.instance, 1);
    check(host.subscriptions.empty(), "pause retained a subscription");
    instance.set_paused(instance.instance, 0);
    const auto active = host.subscriptions.size();
    instance.set_paused(instance.instance, 0);
    check(host.subscriptions.size() == active, "resume duplicated subscriptions");
    std::array<uint8_t, 64 * 64 * 4> pixels{};
    const CdFrame frame{sizeof(CdFrame), 64, 64, 256, pixels.size(), pixels.data(), 1};
    const auto before = factories;
    for (int i = 0; i < 10; ++i) {
        check(instance.render(instance.instance, &frame) == CD_OK, "controller failed to dispatch a frame");
    }
    check(factories == before, "render constructed another view");
    uint64_t size = 0;
    check(instance.save_state(instance.instance, nullptr, 0, &size) == CD_OK, "state size");
    std::vector<uint8_t> state(size);
    check(instance.save_state(instance.instance, state.data(), state.size(), &size) == CD_OK &&
              instance.restore_state(instance.instance, state.data(), state.size()) == CD_OK,
          "state roundtrip");
    instance.destroy(instance.instance);
    check(liveViews == 0 && host.subscriptions.empty(), "destroy retained view or subscriptions");
    factories = 0;
    host.fail = true;
    if (kind != Kind::Album) {
        check(themed::create(spec, &context, &instance, sizeof(instance), makeView) != CD_OK,
              "subscription failure must fail creation");
        check(liveViews == 0 && host.subscriptions.empty(), "failed creation leaked view or subscriptions");
    }
    factories = 0;
    if (isMusic(kind)) {
        host.fail = false;
        host.remaining = 1;
        check(themed::create(spec, &context, &instance, sizeof(instance), makeView) != CD_OK,
              "partial subscription failure must fail creation");
        check(liveViews == 0 && host.subscriptions.empty(), "partial startup did not cancel completed subscriptions");
        factories = 0;
    }
}
}  // namespace

int main() try {
    for (bool mechanical : {false, true}) {
        for (auto kind : {Kind::Bars, Kind::Radial, Kind::Wide, Kind::Album, Kind::Calendar, Kind::RoundMatrix,
                          Kind::SquareMatrix, Kind::Player, Kind::InputStats}) {
            lifecycle(kind, mechanical);
        }
    }
    check(paints == 180, "view frame dispatch count");
    std::cout << "PASS: 18 themed controller lifecycle scenarios, 180 frame dispatches\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
