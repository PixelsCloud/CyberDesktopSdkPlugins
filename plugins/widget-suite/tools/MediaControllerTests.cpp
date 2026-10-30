#include <array>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "plugins/widget-suite/MediaController.h"
#include "plugins/widget-suite/Widget.h"
#include "sdk/services/DataServices.h"

namespace {
using namespace widgetplugin;
int views = 0;
bool bindingFails = false;

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class View final : public media::View {
public:
    View();
    ~View() override;
    void onChange(std::function<void()>) override;
    void onAction(std::function<void(uint32_t)>) override;
    void pause(bool) override;
    void cancelInput() override;
    void setSeeking(bool) override;
    void syncInput(const Media&, CdRect) override;
    void input(const CdInput&) override;
    bool ready() const override;
    bool render(const cdsdk::Settings&, const Media&, double, const CdFrame&, CdRect) override;
    cybercanvas::Rect seekRegion(const media::PlayerSpec&, double, double) const override;

private:
    std::function<void()> changed_;
    std::function<void(uint32_t)> action_;
};

View::View() {
    ++views;
}

View::~View() {
    --views;
}

void View::onChange(std::function<void()> callback) {
    if (bindingFails && callback) {
        throw std::runtime_error("injected binding failure");
    }
    changed_ = std::move(callback);
}

void View::onAction(std::function<void(uint32_t)> callback) {
    action_ = std::move(callback);
}

void View::pause(bool) {}

void View::cancelInput() {}

void View::setSeeking(bool) {}

void View::syncInput(const Media&, CdRect) {}

void View::input(const CdInput&) {}

bool View::ready() const {
    return true;
}

bool View::render(const cdsdk::Settings&, const Media&, double, const CdFrame&, CdRect) {
    return true;
}

cybercanvas::Rect View::seekRegion(const media::PlayerSpec&, double, double) const {
    return {10, 20, 100, 5};
}

struct Host {
    CdServiceHostApi api{};
    uint64_t next = 0;
    std::map<uint64_t, std::pair<CdMediaCallback, void*>> watches;
    std::map<uint64_t, std::pair<CdTimerCallback, void*>> timers;
    bool mediaFails = false, timerFails = false;
    Host();
    static void CD_CALL redraw(void*);
    static void CD_CALL stop(void*, uint64_t);
    static CdResult CD_CALL timer(void*, uint32_t, CdTimerCallback, void*, uint64_t*);
    static CdResult CD_CALL subscribe(void*, const char*, CdMediaCallback, void*, uint64_t*);
    static CdResult CD_CALL query(void*, const char*, uint32_t, void*, uint32_t);
    static CdResult CD_CALL command(void*, uint64_t, uint32_t, double);
    bool empty() const;
};

Host::Host() {
    auto& base = api.input.media.base;
    base.struct_size = sizeof(api);
    base.abi_version = CD_PLUGIN_ABI_VERSION;
    base.request_redraw = redraw;
    base.unsubscribe = stop;
    base.subscribe_timer = timer;
    api.query_service = query;
}

void CD_CALL Host::redraw(void*) {}

void CD_CALL Host::stop(void* context, uint64_t token) {
    auto& host = *static_cast<Host*>(context);
    host.watches.erase(token);
    host.timers.erase(token);
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

CdResult CD_CALL Host::subscribe(void* context, const char*, CdMediaCallback callback, void* user, uint64_t* token) {
    auto& host = *static_cast<Host*>(context);
    if (host.mediaFails) {
        return CD_ERROR_INTERNAL;
    }
    *token = ++host.next;
    host.watches.emplace(*token, std::pair{callback, user});
    CdMediaInfo value{};
    value.struct_size = sizeof(value);
    value.status = CD_MEDIA_PLAYING;
    value.controls = CD_MEDIA_CAN_TOGGLE | CD_MEDIA_CAN_SEEK;
    value.source_id = "fixture";
    value.title = "Track";
    value.artist = "Artist";
    value.album = "Album";
    value.position_seconds = 10;
    value.duration_seconds = 100;
    value.playback_rate = 1;
    callback(user, &value);
    return CD_OK;
}

CdResult CD_CALL Host::command(void*, uint64_t, uint32_t, double) {
    return CD_OK;
}

CdResult CD_CALL Host::query(void*, const char* id, uint32_t version, void* output, uint32_t size) {
    if (id && std::string_view(id) == CD_SERVICE_MEDIA && version == 1 && size >= sizeof(CdMediaServiceApi)) {
        *static_cast<CdMediaServiceApi*>(output) = {sizeof(CdMediaServiceApi), 1, subscribe, command};
        return CD_OK;
    }
    return CD_ERROR_UNSUPPORTED;
}

bool Host::empty() const {
    return timers.empty() && watches.empty();
}

void scenario(bool reels) {
    Host host;
    const CdCreateContext context{
        sizeof(CdCreateContext), CD_PLUGIN_ABI_VERSION, "test", "test", "", &host.api.input.media.base, &host};
    const media::PlayerSpec spec{"test", "test", 4, 1, {100, 100}, {0, 0, 100, 100}, {100, 100}, 100, reels};
    for (int failure = 0; failure < 3; ++failure) {
        host.mediaFails = failure == 0;
        host.timerFails = failure == 1;
        bindingFails = failure == 2;
        bool failed = false;
        try {
            media::Controller controller(context, spec, std::make_unique<View>());
        } catch (const std::exception&) {
            failed = true;
        }
        check(failed && views == 0 && host.empty(), "failed media construction retained subscriptions or view");
    }
    host.mediaFails = host.timerFails = bindingFails = false;
    {
        media::Controller controller(context, spec, std::make_unique<View>());
        check(controller.media().title == L"Track", "synchronous subscription metadata was lost");
        check(views == 1 && host.watches.size() == 1, "controller did not own one view and watch");
        controller.setPaused(true);
        check(host.empty(), "paused media controller retained callbacks");
        controller.setPaused(false);
        const auto timers = host.timers.size();
        controller.setPaused(false);
        check(host.timers.size() == timers && host.watches.size() == 1, "repeated resume duplicated subscriptions");
        std::array<uint8_t, 16 * 16 * 4> rgba{};
        const CdFrame frame{sizeof(CdFrame), 16, 16, 64, rgba.size(), rgba.data(), 1};
        check(controller.render(frame) == CD_OK && views == 1, "media render recreated or failed its view");
    }
    check(views == 0 && host.empty(), "media destruction retained resources");
}
}  // namespace

int main() try {
    scenario(false);
    scenario(true);
    std::cout << "PASS: media construction failure/partial startup, synchronous metadata, pause/resume, destruction\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
