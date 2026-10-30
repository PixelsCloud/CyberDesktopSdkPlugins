#include <CyberCanvas/InstrumentMotion.h>
#include <Windows.h>
#include <sdk/Update.h>
#include <sdk/cpp/Services.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

#include "plugins/songyun-desk/controllers/weather/Weather.h"

namespace songyun_weather {
namespace {
struct Instance;
CdResult start(Instance& self);
void stop(Instance& self);

struct Instance {
    Spec spec;
    cdsdk::Settings settings = defaults();
    Renderer renderer;
    Snapshot snapshot;
    CdHostApi host{};
    CdWeatherServiceApi service{};
    void* context = nullptr;
    CdRect safe{};
    uint64_t timer = 0;
    cdsdk::Timer motionTimer;
    cybercanvas::mechanical::NeedleBank<2> needles;
    bool paused = false, loading = false;

    Instance(Spec value, const std::filesystem::path& root)
        : spec(value), settings(defaults(value.mechanical)), renderer(root / L"assets") {}

    void redraw() {
        host.request_redraw(context);
    }

    void request(bool force = false) {
        if (paused)
            return;
        if (force && timer) {
            service.refresh(context, timer);
            return;
        }
        const auto previous = timer;
        timer = 0;
        loading = true;
        if (start(*this) != CD_OK)
            loading = false;
        if (previous)
            host.unsubscribe(context, previous);
    }
};

bool motionAllowed(const Instance& self) {
    BOOL enabled = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0);
    return !self.paused && self.spec.rows > 1 && self.spec.mechanical && !self.settings.at("reduced_motion").integer &&
           enabled;
}

void CD_CALL moveNeedles(void* pointer, uint64_t) noexcept {
    try {
        auto& self = *static_cast<Instance*>(pointer);
        const auto now = GetTickCount64();
        self.snapshot.needles = self.needles.sample(now);
        if (!motionAllowed(self) || !self.needles.active(now)) {
            self.snapshot.needles = {-1, -1};
            self.motionTimer.stop();
        }
        self.redraw();
    } catch (...) {
        auto& self = *static_cast<Instance*>(pointer);
        self.snapshot.needles = {-1, -1};
        self.motionTimer.stop();
    }
}

void CD_CALL changed(void* pointer, const CdWeatherInfo* info) noexcept {
    if (!pointer || !info)
        return;
    try {
        auto& self = *static_cast<Instance*>(pointer);
        if (self.paused)
            return;
        Snapshot result;
        result.valid = (info->info.valid_fields & CD_DATA_VALID) != 0;
        result.cached = info->info.status == CD_DATA_STALE;
        result.fetchedAt = info->info.fetched_at_utc_ms / 1000;
        result.city = info->city ? info->city : "";
        const auto displayLocation = locationFrom(self.settings);
        if (!displayLocation.automatic && displayLocation.coordinates && !displayLocation.city.empty())
            result.city = displayLocation.city;
        result.region = info->region ? info->region : "";
        result.time = info->local_time ? info->local_time : "";
        result.latitude = info->latitude;
        result.longitude = info->longitude;
        result.temperature = info->temperature_celsius;
        result.feels = info->feels_celsius;
        result.humidity = info->humidity_percent;
        result.wind = info->wind_mps;
        result.code = info->condition_code;
        result.day = info->is_day != 0;
        if (info->days && info->day_count <= 14)
            for (uint32_t index = 0; index < info->day_count; ++index) {
                const auto& day = info->days[index];
                result.days.push_back({day.date ? day.date : "", day.condition_code, day.high_celsius, day.low_celsius,
                                       day.precipitation_probability});
            }
        result.error = info->info.error == CD_DATA_RATE_LIMITED       ? Error::RateLimited
                       : info->info.error == CD_DATA_NO_LOCATION      ? Error::LocationUnavailable
                       : info->info.error == CD_DATA_INVALID_RESPONSE ? Error::InvalidResponse
                       : info->info.error                             ? Error::Network
                                                                      : Error::None;
        const bool loading = info->info.status == CD_DATA_LOADING;
        auto previous = self.snapshot;
        previous.needles = {-1, -1};
        if (result != previous || loading != self.loading) {
            const bool active = self.needles.retarget({result.humidity / 100, result.wind / 25}, GetTickCount64(),
                                                      previous.valid && result.valid && motionAllowed(self));
            self.snapshot = std::move(result);
            if (active && self.motionTimer.replace(33, moveNeedles, &self) == CD_OK) {
                self.snapshot.needles = self.needles.sample(GetTickCount64());
            } else {
                self.motionTimer.stop();
            }
            self.loading = loading;
            self.redraw();
        }
    } catch (...) {
    }
}

void stop(Instance& self) {
    self.motionTimer.stop();
    self.needles.reset();
    self.snapshot.needles = {-1, -1};
    if (self.timer)
        self.host.unsubscribe(self.context, self.timer);
    self.timer = 0;
}

CdResult start(Instance& self) {
    const auto location = locationFrom(self.settings);
    CdLocationQuery query{sizeof(query),
                          location.automatic     ? CD_LOCATION_DEFAULT
                          : location.coordinates ? CD_LOCATION_COORDINATES
                                                 : CD_LOCATION_CITY,
                          location.latitude,
                          location.longitude,
                          location.city.c_str(),
                          nullptr,
                          "default",
                          location.language.c_str(),
                          uint32_t(self.settings.at("refresh_minutes").integer * 60),
                          5,
                          0,
                          0,
                          location.country.c_str()};
    return self.service.subscribe(self.context, &query, changed, &self, &self.timer);
}

void CD_CALL destroy(void* pointer) noexcept {
    if (auto self = std::unique_ptr<Instance>(static_cast<Instance*>(pointer))) {
        stop(*self);
    }
}

void CD_CALL pause(void* pointer, uint32_t paused) noexcept {
    try {
        if (!pointer)
            return;
        auto& self = *static_cast<Instance*>(pointer);
        if (self.paused == (paused != 0))
            return;
        self.paused = paused != 0;
        self.renderer.pause(self.paused);
        if (self.paused) {
            stop(self);
            self.loading = false;
            return;
        }
        if (start(self) != CD_OK) {
            self.paused = true;
            self.renderer.pause(true);
            stop(self);
            return;
        }
        self.request();
        self.redraw();
    } catch (...) {
    }
}

void CD_CALL layout(void* pointer, const CdLayout* value) noexcept {
    if (!pointer || !value || value->struct_size < sizeof(CdLayout))
        return;
    auto& self = *static_cast<Instance*>(pointer);
    self.safe = value->safe_content;
    if (self.safe.width <= 0 || self.safe.height <= 0)
        self.safe = {0, 0, value->logical_width, value->logical_height};
    self.redraw();
}

void CD_CALL input(void* pointer, const CdInput* event) noexcept {
    try {
        if (!pointer || !event || event->struct_size < sizeof(CdInput))
            return;
        auto& self = *static_cast<Instance*>(pointer);
        if (!self.paused && event->kind == CD_INPUT_DOUBLE_CLICK)
            self.request(true);
    } catch (...) {
    }
}

CdResult CD_CALL get(void* pointer, const char* id, CdSettingValue* value, uint32_t size) noexcept {
    return pointer ? static_cast<Instance*>(pointer)->settings.get(id, value, size) : CD_ERROR_ARGUMENT;
}

CdResult CD_CALL apply(void* pointer, const CdSettingValue* values, uint32_t count) noexcept {
    try {
        if (!pointer)
            return CD_ERROR_ARGUMENT;
        auto& self = *static_cast<Instance*>(pointer);
        auto changed = self.settings;
        const auto result = changed.apply(values, count);
        if (result != CD_OK)
            return result;
        auto query = locationFrom(changed);
        if (!query.automatic && !query.coordinates && query.city.find_first_not_of(" \t\r\n") == std::string::npos)
            return CD_ERROR_ARGUMENT;
        if (!query.country.empty() &&
            (query.country.size() != 2 || !std::all_of(query.country.begin(), query.country.end(), [](char c) {
                 return c >= 'A' && c <= 'Z';
             })))
            return CD_ERROR_ARGUMENT;
        wide(query.city);
        auto numerical = query;
        auto previous = locationFrom(self.settings);
        numerical.language.clear();
        previous.language.clear();
        const bool locationChanged = numerical != previous;
        const bool intervalChanged =
            changed.at("refresh_minutes").integer != self.settings.at("refresh_minutes").integer;
        self.settings = std::move(changed);
        self.motionTimer.stop();
        self.needles.reset();
        self.snapshot.needles = {-1, -1};
        if (locationChanged) {
            self.snapshot = {};
            if (!self.paused)
                self.request();
            else
                stop(self);
        } else if (intervalChanged && !self.paused)
            self.request();
        self.redraw();
        return CD_OK;
    } catch (...) {
        return CD_ERROR_INTERNAL;
    }
}

CdResult CD_CALL save(void* pointer, uint8_t* bytes, uint64_t capacity, uint64_t* size) noexcept {
    if (!pointer || !size)
        return CD_ERROR_ARGUMENT;
    *size = 4;
    if (!bytes && !capacity)
        return CD_OK;
    if (!bytes || capacity < 4)
        return CD_ERROR_BUFFER;
    std::memcpy(bytes, "WTH1", 4);
    return CD_OK;
}

CdResult CD_CALL restore(void* pointer, const uint8_t* bytes, uint64_t size) noexcept {
    if (!pointer || (size && (!bytes || size != 4 || std::memcmp(bytes, "WTH1", 4))))
        return CD_ERROR_ARGUMENT;
    static_cast<Instance*>(pointer)->redraw();
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
        return self.renderer.render(self.spec, self.settings, self.snapshot, self.loading, *frame, self.safe)
                   ? CD_OK
                   : CD_ERROR_INTERNAL;
    } catch (...) {
        return CD_ERROR_INTERNAL;
    }
}
}  // namespace

CdResult create(Spec spec, const CdCreateContext* context, CdInstanceApi* out, uint32_t size) {
    try {
        if (!context || context->struct_size < sizeof(CdCreateContext) ||
            context->abi_version != CD_PLUGIN_ABI_VERSION || !context->plugin_directory || !context->host ||
            context->host->struct_size < sizeof(CdHostApi) || context->host->abi_version != CD_PLUGIN_ABI_VERSION ||
            !context->host->request_redraw || !context->host->unsubscribe || !out || size < sizeof(CdInstanceApi))
            return CD_ERROR_ARGUMENT;
        auto self = std::make_unique<Instance>(spec, std::filesystem::path(wide(context->plugin_directory)));
        self->host = *context->host;
        self->context = context->host_context;
        self->renderer.bind(*context);
        self->motionTimer.bind(*context);
        const auto serviceResult =
            cdsdk::Services(context->host, self->context).query(CD_SERVICE_WEATHER, self->service);
        if (serviceResult != CD_OK)
            return serviceResult;
        // Give the host time to restore saved manual settings before the first
        // timer tick. Do not issue a default IP request during construction.
        if (start(*self) != CD_OK) {
            stop(*self);
            return CD_ERROR_INTERNAL;
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
}  // namespace songyun_weather
