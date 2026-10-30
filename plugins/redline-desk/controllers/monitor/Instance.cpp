#include <CyberCanvas/InstrumentMotion.h>
#include <Windows.h>
#include <sdk/RenderOutput.h>
#include <sdk/Update.h>
#include <sdk/cpp/Services.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>

#include "plugins/redline-desk/controllers/monitor/Monitor.h"

namespace redline_monitor {
Values decode(const CdSystemInfo& info) {
    Values values;
    if (info.struct_size < sizeof(CdSystemInfo))
        return values;
    const auto finite = [](double value) {
        return std::isfinite(value);
    };
    if (finite(info.cpu_percent)) {
        values.cpu = std::clamp(info.cpu_percent, 0.0, 100.0);
        values.valid |= CD_SYSTEM_CPU_VALID;
    }
    if (info.memory_total_bytes > 0 && info.memory_used_bytes <= info.memory_total_bytes) {
        values.ram = 100.0 * info.memory_used_bytes / info.memory_total_bytes;
        values.valid |= CD_SYSTEM_MEMORY_VALID;
    }
    if (finite(info.gpu_percent)) {
        values.gpu = std::clamp(info.gpu_percent, 0.0, 100.0);
        values.valid |= CD_SYSTEM_GPU_VALID;
    }
    if (info.struct_size >= sizeof(CdSystemInfoEx)) {
        const auto& extended = *reinterpret_cast<const CdSystemInfoEx*>(&info);
        values.valid &= extended.valid_fields;
        if ((extended.valid_fields & CD_SYSTEM_CPU_TEMPERATURE_VALID) && finite(extended.cpu_temperature_celsius) &&
            extended.cpu_temperature_celsius >= 0 && extended.cpu_temperature_celsius <= 150) {
            values.temperature = extended.cpu_temperature_celsius;
            values.valid |= CD_SYSTEM_CPU_TEMPERATURE_VALID;
        }
    }
    return values;
}

std::vector<Position> arrange(Spec spec, double width, double height, const Options& options) {
    const int count = spec.rows == 2 ? 4 : spec.columns, columns = spec.rows == 2 ? 2 : count;
    const double areaWidth = width / columns, areaHeight = height / spec.rows;
    // The grid fits the screen, so a nominal 72px cell can be ~70px.
    // The 48px gauge includes transparent padding: its drawn diameter is 44.1px.
    // Keep 14px typography through that small adjustment instead of scaling only
    // 1x1.
    const double scale = std::max(0.0, std::min({1.0, areaWidth / 46, areaHeight / 46}));
    int primary = options.primary == "ram"           ? 1
                  : options.primary == "gpu"         ? 2
                  : options.primary == "temperature" ? 3
                                                     : 0;
    std::vector<Position> positions;
    for (int i = 0; i < count; ++i)
        positions.push_back({(i % columns) * areaWidth + (areaWidth - 48 * scale) / 2,
                             (i / columns) * areaHeight + (areaHeight - 48 * scale) / 2, scale,
                             count == 1 ? primary : i});
    return positions;
}

namespace {
cdsdk::Settings defaults(bool mechanical = false) {
    return {{{"interval", CD_SETTING_INTEGER, 1000, 0, "", 500, 5000},
             {"palette", CD_SETTING_CHOICE, 0, 0, "theme", 0, 0, {"theme", "dark", "light"}},
             {"animate", CD_SETTING_TOGGLE, 1},
             {"reduced_motion", CD_SETTING_TOGGLE, 0},
             {"primary", CD_SETTING_CHOICE, 0, 0, "cpu", 0, 0, {"cpu", "ram", "gpu", "temperature"}},
             {"font_family", CD_SETTING_FONT, 0, 0, mechanical ? "Microsoft YaHei UI" : "@siyuan-heavy"},
             {"language", CD_SETTING_CHOICE, 0, 0, "zh-CN", 0, 0, {"zh-CN", "en"}}}};
}

struct Instance {
    Spec spec;
    cdsdk::Settings settings = defaults();
    CdHostApi host{};
    void* context = nullptr;
    uint64_t systemToken = 0, watchToken = 0, receivedAt = 0;
    bool paused = false;
    Values values{};
    Options options{};
    CdRect safe{};
    Renderer renderer;
    cdsdk::FrameCache output;
    uint64_t revision = 1;
    bool expired = false;
    cdsdk::Timer motionTimer;
    cybercanvas::mechanical::NeedleBank<4> needles;

    explicit Instance(Spec value) : spec(value), settings(defaults(value.mechanical)) {
        if (value.columns == 1) {
            const CdSettingValue primary{sizeof(CdSettingValue), CD_SETTING_CHOICE, "primary", 0, 0, "ram"};
            settings.apply(&primary, 1);
        }
    }

    void redraw() {
        host.request_redraw(context);
    }

    void sync() {
        options.palette = settings.at("palette").text;
        options.primary = settings.at("primary").text;
        options.fontFamily = settings.at("font_family").text;
        options.language = settings.at("language").text;
        options.interpolate = false;
        motionTimer.stop();
        needles.reset();
        if (receivedAt) {
            needles.retarget({values.cpu / 100, values.ram / 100, values.gpu / 100, values.temperature / 120},
                             GetTickCount64(), false);
        }
        ++revision;
    }

    bool motionAllowed() const {
        BOOL enabled = TRUE;
        SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0);
        return spec.mechanical && settings.at("animate").integer && !settings.at("reduced_motion").integer && enabled;
    }

    void advance(uint64_t now) {
        if (!options.interpolate) {
            return;
        }
        const auto value = needles.sample(now);
        options.needles = {value[0] * 100, value[1] * 100, value[2] * 100, value[3] * 120, values.valid};
        if (!needles.active(now) || !motionAllowed()) {
            options.interpolate = false;
            motionTimer.stop();
        }
    }
};

void CD_CALL motion(void* pointer, uint64_t) noexcept {
    try {
        auto& self = *static_cast<Instance*>(pointer);
        if (self.paused) {
            return;
        }
        self.advance(GetTickCount64());
        ++self.revision;
        self.redraw();
    } catch (...) {
        auto& self = *static_cast<Instance*>(pointer);
        self.options.interpolate = false;
        self.motionTimer.stop();
    }
}

void CD_CALL data(void* pointer, const CdSystemInfo* info) {
    auto& self = *static_cast<Instance*>(pointer);
    if (self.paused || !info || info->struct_size < sizeof(CdSystemInfo))
        return;
    const auto next = decode(*info);
    const bool wasValid = self.receivedAt && self.values.valid == next.valid;
    const bool changed = self.values != next || self.expired || !self.receivedAt;
    self.receivedAt = GetTickCount64();
    if (info->struct_size >= sizeof(CdSystemInfoEx)) {
        const auto time = reinterpret_cast<const CdSystemInfoEx*>(info)->sampled_at_ms;
        if (time && time <= self.receivedAt)
            self.receivedAt = time;
    }
    if (!changed) {
        return;
    }
    const bool active = self.needles.retarget({next.cpu / 100, next.ram / 100, next.gpu / 100, next.temperature / 120},
                                              GetTickCount64(), wasValid && self.motionAllowed());
    self.values = next;
    self.options.interpolate = active && self.motionTimer.replace(33, motion, &self) == CD_OK;
    if (self.options.interpolate) {
        self.advance(GetTickCount64());
    } else {
        self.motionTimer.stop();
    }
    ++self.revision;
    self.redraw();
}

CdResult subscribe(Instance& self, uint64_t* token) {
    return self.host.subscribe_system(self.context, uint32_t(self.settings.at("interval").integer), data, &self, token);
}

void CD_CALL watch(void* pointer, uint64_t) {
    auto& self = *static_cast<Instance*>(pointer);
    if (self.paused)
        return;
    if (!self.systemToken) {
        uint64_t token = 0;
        if (subscribe(self, &token) == CD_OK)
            self.systemToken = token;
    }
    const bool stale =
        !self.receivedAt || GetTickCount64() - self.receivedAt >
                                uint64_t(std::max<int64_t>(5000, self.settings.at("interval").integer * 3));
    if (stale != self.expired) {
        self.redraw();
    }
}

void stop(Instance& self) {
    self.motionTimer.stop();
    self.options.interpolate = false;
    self.needles.reset();
    if (self.systemToken)
        self.host.unsubscribe(self.context, self.systemToken);
    if (self.watchToken)
        self.host.unsubscribe(self.context, self.watchToken);
    self.systemToken = 0;
    self.watchToken = 0;
}

void CD_CALL destroy(void* pointer) noexcept {
    if (auto self = std::unique_ptr<Instance>(static_cast<Instance*>(pointer))) {
        stop(*self);
    }
}

void CD_CALL layout(void* pointer, const CdLayout* context) noexcept {
    if (!pointer || !context || context->struct_size < sizeof(CdLayout))
        return;
    auto& self = *static_cast<Instance*>(pointer);
    self.options.themeText = context->text_color;
    self.safe = context->safe_content;
    ++self.revision;
    self.redraw();
}

void CD_CALL input(void*, const CdInput*) noexcept {}

void CD_CALL pause(void* pointer, uint32_t value) noexcept {
    if (!pointer)
        return;
    auto& self = *static_cast<Instance*>(pointer);
    if (self.paused == (value != 0))
        return;
    self.paused = value != 0;
    self.renderer.pause(self.paused);
    if (self.paused) {
        stop(self);
        return;
    }
    self.receivedAt = 0;
    self.values = {};
    uint64_t timer = 0;
    if (self.host.subscribe_timer(self.context, 1000, watch, &self, &timer) == CD_OK)
        self.watchToken = timer;
    uint64_t system = 0;
    if (subscribe(self, &system) == CD_OK)
        self.systemToken = system;
    ++self.revision;
    self.redraw();
}

CdResult CD_CALL get(void* pointer, const char* id, CdSettingValue* out, uint32_t size) noexcept {
    return pointer ? static_cast<Instance*>(pointer)->settings.get(id, out, size) : CD_ERROR_ARGUMENT;
}

CdResult CD_CALL apply(void* pointer, const CdSettingValue* values, uint32_t count) noexcept {
    try {
        if (!pointer)
            return CD_ERROR_ARGUMENT;
        auto& self = *static_cast<Instance*>(pointer);
        auto previous = self.settings;
        const auto result = self.settings.apply(values, count);
        if (result != CD_OK)
            return result;
        if (!self.paused && self.settings.at("interval").integer != previous.at("interval").integer) {
            uint64_t token = 0;
            const auto subscribed = subscribe(self, &token);
            if (subscribed != CD_OK) {
                self.settings = std::move(previous);
                return subscribed;
            }
            if (self.systemToken)
                self.host.unsubscribe(self.context, self.systemToken);
            self.systemToken = token;
        }
        self.sync();
        self.redraw();
        return CD_OK;
    } catch (...) {
        return CD_ERROR_INTERNAL;
    }
}

constexpr uint8_t state[]{'S', 'Y', 'S', 'M', 'O', 'N', 1, 0};

CdResult CD_CALL save(void* pointer, uint8_t* bytes, uint64_t capacity, uint64_t* size) noexcept {
    if (!pointer || !size)
        return CD_ERROR_ARGUMENT;
    *size = sizeof(state);
    if (!bytes && !capacity)
        return CD_OK;
    if (!bytes || capacity < sizeof(state))
        return CD_ERROR_BUFFER;
    std::memcpy(bytes, state, sizeof(state));
    return CD_OK;
}

CdResult CD_CALL restore(void* pointer, const uint8_t* bytes, uint64_t size) noexcept {
    if (!pointer || (size && !bytes))
        return CD_ERROR_ARGUMENT;
    if (size && (size != sizeof(state) || std::memcmp(state, bytes, size)))
        return CD_ERROR_ARGUMENT;
    auto& self = *static_cast<Instance*>(pointer);
    ++self.revision;
    self.redraw();
    return CD_OK;
}

CdResult CD_CALL render(void* pointer, const CdFrame* frame) noexcept {
    try {
        if (!pointer || !frame || frame->struct_size < sizeof(CdFrame) || !frame->rgba || !frame->width ||
            !frame->height || frame->width > 4096 || frame->height > 4096 || frame->stride < frame->width * 4 ||
            frame->capacity < uint64_t(frame->stride) * frame->height || !std::isfinite(frame->device_pixel_ratio) ||
            frame->device_pixel_ratio <= 0 || frame->device_pixel_ratio > 8)
            return CD_ERROR_ARGUMENT;
        auto& self = *static_cast<Instance*>(pointer);
        const bool stale =
            self.paused
                ? self.expired
                : !self.receivedAt || GetTickCount64() - self.receivedAt >
                                          uint64_t(std::max<int64_t>(5000, self.settings.at("interval").integer * 3));
        if (self.renderer.poll()) {
            ++self.revision;
        }
        self.expired = stale;
        return self.output.render(*frame, self.revision * 2 + uint64_t(stale), [&](const CdFrame& target) {
            return self.renderer.render(self.spec, self.options, stale ? Values{} : self.values, target, self.safe)
                       ? CD_OK
                       : CD_ERROR_INTERNAL;
        });
    } catch (...) {
        return CD_ERROR_INTERNAL;
    }
}
}  // namespace

CdResult create(Spec spec, const CdCreateContext* context, CdInstanceApi* out, uint32_t size) {
    try {
        if (!context || context->struct_size < sizeof(CdCreateContext) ||
            context->abi_version != CD_PLUGIN_ABI_VERSION || !out || size < sizeof(CdInstanceApi) || !context->host ||
            context->host->struct_size < sizeof(CdHostApi) || context->host->abi_version != CD_PLUGIN_ABI_VERSION ||
            !context->host->request_redraw || !context->host->subscribe_system || !context->host->subscribe_timer ||
            !context->host->unsubscribe)
            return CD_ERROR_ARGUMENT;
        auto self = std::make_unique<Instance>(spec);
        self->host = *context->host;
        self->context = context->host_context;
        self->output.bind(*context);
        self->motionTimer.bind(*context);
        {
            self->renderer.bind(*context, [instance = self.get()] {
                ++instance->revision;
                instance->redraw();
            });
        }
        if (context->host->struct_size >= sizeof(CdServiceHostApi)) {
            CdSystemServiceApi service{};
            const auto discovered = cdsdk::Services(context->host, self->context).query(CD_SERVICE_SYSTEM, service);
            if (discovered != CD_OK)
                return discovered;
            self->host.subscribe_system = service.subscribe;
        }
        auto result = subscribe(*self, &self->systemToken);
        if (result != CD_OK) {
            stop(*self);
            return result;
        }
        result = self->host.subscribe_timer(self->context, 1000, watch, self.get(), &self->watchToken);
        if (result != CD_OK) {
            stop(*self);
            return result;
        }
        *out = {sizeof(CdInstanceApi),
                CD_PLUGIN_ABI_VERSION,
                self.get(),
                destroy,
                render,
                input,
                layout,
                pause,
                save,
                restore,
                get,
                apply};
        self.release();
        return CD_OK;
    } catch (...) {
        return CD_ERROR_INTERNAL;
    }
}
}  // namespace redline_monitor
