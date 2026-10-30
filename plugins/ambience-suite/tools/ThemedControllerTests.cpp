#include <array>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "plugins/ambience-suite/Atmosphere.h"
#include "plugins/ambience-suite/ThemedController.h"
#include "sdk/services/AmbientAudio.h"

namespace {
using namespace atmosphere;
int views = 0;
bool bindingFails = false, escapedCallbacks = false;
uint32_t renderedAudioError = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class View final : public themed::View {
public:
    View();
    ~View() override;
    void bindHost(const CdCreateContext&) override;
    void bindPlant(const CdCreateContext&, std::function<void(uint32_t)>) override;
    bool plantInput(Spec, const cdsdk::Settings&, CdRect, const CdInput&) override;
    cybercanvas::SceneEvent noiseInput(Spec, const cdsdk::Settings&, const Model&, CdRect, const CdInput&, double,
                                       bool&) override;
    void onInvalidate(std::function<void()>) override;
    void onAction(std::function<void(cybercanvas::SceneEvent)>) override;
    void onDiagnostics(std::function<void(cdsdk::InstanceStats&)>) override;
    void cancelInput() override;
    bool advanceFeedback(double) override;
    bool feedbackActive() const override;
    bool imagesPending() const override;
    bool pollImages() override;
    void pauseImages(bool) override;
    bool render(Spec, const cdsdk::Settings&, const Model&, const CdFrame&, CdRect) override;

private:
    std::function<void()> changed_;
    std::function<void(cybercanvas::SceneEvent)> action_;
    std::function<void(cdsdk::InstanceStats&)> diagnostics_;
};

View::View() {
    ++views;
}

View::~View() {
    escapedCallbacks |= bool(changed_) || bool(action_) || bool(diagnostics_);
    --views;
}

void View::bindHost(const CdCreateContext&) {}

void View::bindPlant(const CdCreateContext&, std::function<void(uint32_t)>) {}

bool View::plantInput(Spec, const cdsdk::Settings&, CdRect, const CdInput&) {
    return false;
}

cybercanvas::SceneEvent View::noiseInput(Spec, const cdsdk::Settings&, const Model&, CdRect, const CdInput&, double,
                                         bool& needsPaint) {
    needsPaint = false;
    cybercanvas::SceneEvent event;
    event.key = 101;
    event.clicked = true;
    return event;
}

void View::onInvalidate(std::function<void()> callback) {
    changed_ = std::move(callback);
}

void View::onAction(std::function<void(cybercanvas::SceneEvent)> callback) {
    action_ = std::move(callback);
}

void View::onDiagnostics(std::function<void(cdsdk::InstanceStats&)> callback) {
    if (bindingFails && callback) {
        throw std::runtime_error("injected diagnostics binding failure");
    }
    diagnostics_ = std::move(callback);
}

void View::cancelInput() {}

bool View::advanceFeedback(double) {
    return false;
}

bool View::feedbackActive() const {
    return false;
}

bool View::imagesPending() const {
    return false;
}

bool View::pollImages() {
    return false;
}

void View::pauseImages(bool) {}

bool View::render(Spec, const cdsdk::Settings&, const Model& model, const CdFrame&, CdRect) {
    renderedAudioError = model.audioError;
    return true;
}

std::unique_ptr<themed::View> makeView(const CdCreateContext&) {
    return std::make_unique<View>();
}

struct Host {
    CdServiceHostApi api{};
    uint64_t next = 0;
    std::map<uint64_t, uint32_t> timers;
    std::set<uint64_t> audio;
    CdAmbientConfig config{sizeof(CdAmbientConfig), 30, .25f, {.6f}};
    bool timerFails = false, audioAvailable = true;
    Host();
    static void CD_CALL redraw(void*);
    static void CD_CALL stop(void*, uint64_t);
    static CdResult CD_CALL timer(void*, uint32_t, CdTimerCallback, void*, uint64_t*);
    static CdResult CD_CALL open(void*, const char*, uint64_t*);
    static void CD_CALL close(void*, uint64_t);
    static CdResult CD_CALL configure(void*, uint64_t, const CdAmbientConfig*);
    static CdResult CD_CALL command(void*, uint64_t, uint32_t);
    static CdResult CD_CALL snapshot(void*, uint64_t, CdAmbientSnapshot*);
    static CdResult CD_CALL query(void*, const char*, uint32_t, void*, uint32_t);
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
    static_cast<Host*>(context)->timers.erase(token);
}

CdResult CD_CALL Host::timer(void* context, uint32_t interval, CdTimerCallback, void*, uint64_t* token) {
    auto& host = *static_cast<Host*>(context);
    if (host.timerFails) {
        return CD_ERROR_INTERNAL;
    }
    *token = ++host.next;
    host.timers.emplace(*token, interval);
    return CD_OK;
}

CdResult CD_CALL Host::open(void* context, const char*, uint64_t* token) {
    auto& host = *static_cast<Host*>(context);
    *token = ++host.next;
    host.audio.insert(*token);
    return CD_OK;
}

void CD_CALL Host::close(void* context, uint64_t token) {
    static_cast<Host*>(context)->audio.erase(token);
}

CdResult CD_CALL Host::configure(void* context, uint64_t, const CdAmbientConfig* config) {
    static_cast<Host*>(context)->config = *config;
    return CD_OK;
}

CdResult CD_CALL Host::command(void*, uint64_t, uint32_t) {
    return CD_OK;
}

CdResult CD_CALL Host::snapshot(void* context, uint64_t, CdAmbientSnapshot* snapshot) {
    *snapshot = {};
    snapshot->struct_size = sizeof(*snapshot);
    snapshot->status = CD_AMBIENT_PLAYING;
    snapshot->config = static_cast<Host*>(context)->config;
    return CD_OK;
}

CdResult CD_CALL Host::query(void* context, const char* id, uint32_t version, void* output, uint32_t size) {
    if (static_cast<Host*>(context)->audioAvailable && id && std::string_view(id) == CD_SERVICE_AMBIENT &&
        version == 1 && size >= sizeof(CdAmbientApi)) {
        *static_cast<CdAmbientApi*>(output) = {sizeof(CdAmbientApi), 1, open, close, configure, command, snapshot};
        return CD_OK;
    }
    return CD_ERROR_UNSUPPORTED;
}

bool Host::empty() const {
    return timers.empty() && audio.empty();
}

void scenario(Spec spec, themed::Defaults presentation) {
    Host host;
    const CdCreateContext context{
        sizeof(CdCreateContext), CD_PLUGIN_ABI_VERSION, "test", "test", "", &host.api.input.media.base, &host};
    CdInstanceApi instance{};
    bindingFails = true;
    check(themed::create(spec, &context, &instance, sizeof(instance), makeView, presentation) == CD_ERROR_INTERNAL,
          "binding exception accepted");
    check(views == 0 && host.empty() && !escapedCallbacks, "binding exception escaped owned callbacks");
    bindingFails = false;
    host.timerFails = true;
    check(themed::create(spec, &context, &instance, sizeof(instance), makeView, presentation) != CD_OK,
          "timer startup failure accepted");
    check(views == 0 && host.empty() && !escapedCallbacks, "partial startup retained callbacks or audio group");
    host.timerFails = false;
    check(themed::create(spec, &context, &instance, sizeof(instance), makeView, presentation) == CD_OK,
          "ambience creation failed");
    if (spec.kind == Kind::Plant) {
        CdSettingValue value{};
        check(instance.get_setting(instance.instance, "show_background", &value, sizeof(value)) == CD_OK &&
                  bool(value.integer) == (presentation.plantBackground && spec.rows > 1),
              "plant background default changed");
        check(instance.get_setting(instance.instance, "show_info", &value, sizeof(value)) == CD_OK &&
                  bool(value.integer) == (presentation.plantInfo && spec.rows > 1),
              "plant information default changed");
    } else {
        check(host.audio.size() == 1 && host.timers.size() == 1 &&
                  host.timers.begin()->second == (presentation.noiseAnimation ? 50u : 1000u),
              "noise scheduling lost its theme motion policy");
        const CdInput input{sizeof(CdInput), CD_INPUT_RELEASE, 0, 0, 10, 10};
        instance.input(instance.instance, &input);
        CdSettingValue value{};
        check(instance.get_setting(instance.instance, "ocean", &value, sizeof(value)) == CD_OK &&
                  value.integer == (presentation.trackToggle ? 60 : 0),
              "theme track toggle policy changed");
    }
    instance.set_paused(instance.instance, 1);
    check(host.timers.empty(), "hidden ambience retained animation timer");
    instance.set_paused(instance.instance, 0);
    const auto active = host.timers.size();
    instance.set_paused(instance.instance, 0);
    check(host.timers.size() == active, "repeated ambience resume duplicated timer");
    instance.destroy(instance.instance);
    check(host.empty() && views == 0 && !escapedCallbacks, "ambience destruction retained resources");
    if (spec.kind == Kind::Noise) {
        host.audioAvailable = false;
        check(themed::create(spec, &context, &instance, sizeof(instance), makeView, presentation) == CD_OK,
              "unavailable audio prevented truthful presentation");
        std::array<uint8_t, 16 * 16 * 4> pixels{};
        const CdFrame frame{sizeof(CdFrame), 16, 16, 64, pixels.size(), pixels.data(), 1};
        check(instance.render(instance.instance, &frame) == CD_OK &&
                  renderedAudioError == uint32_t(presentation.indicateMissingAudio),
              "missing-audio presentation changed");
        instance.destroy(instance.instance);
        check(host.empty() && views == 0 && !escapedCallbacks, "offline ambience retained resources");
    }
}
}  // namespace

int main() try {
    for (auto presentation :
         {themed::Defaults{}, themed::Defaults{.trackToggle = true},
          themed::Defaults{.compactSucculent = true, .noiseAnimation = false, .trackToggle = true},
          themed::Defaults{
              .plantBackground = false, .plantInfo = false, .compactSucculent = true, .indicateMissingAudio = true}}) {
        for (auto dimensions : {std::pair{1, 1}, std::pair{2, 4}}) {
            for (auto kind : {Kind::Plant, Kind::Noise}) {
                scenario({"test", kind, dimensions.first, dimensions.second}, presentation);
            }
        }
    }
    std::cout << "PASS: 16 ambience policy/lifecycle scenarios, partial startup and missing audio\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
