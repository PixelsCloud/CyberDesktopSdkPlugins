// Controller binding reused from plugins/vampire-alice/controllers/air/Instance.cpp.
#include <Windows.h>
#include <sdk/cpp/Services.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

#include "plugins/mecha-desk/controllers/air/AirQuality.h"

namespace aeris_air {
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
    CdAirServiceApi service{};
    void* context = nullptr;
    CdRect safe{};
    uint64_t timer = 0;
    bool paused = false, loading = false;

    Instance(Spec value, const std::filesystem::path& root) : spec(value), renderer(root / L"assets") {}

    void redraw() {
        host.request_redraw(context);
    }

    void request(bool force = false) {
        if (paused) {
            return;
        }
        if (force && timer) {
            service.refresh(context, timer);
            return;
        }
        const auto previous = timer;
        timer = 0;
        loading = true;
        if (start(*this) != CD_OK) {
            loading = false;
        }
        if (previous) {
            host.unsubscribe(context, previous);
        }
    }
};

void CD_CALL changed(void* pointer, const CdAirInfo* info) noexcept {
    if (!pointer || !info) {
        return;
    }
    try {
        auto& self = *static_cast<Instance*>(pointer);
        if (self.paused) {
            return;
        }
        Snapshot result;
        result.valid = (info->info.valid_fields & CD_DATA_VALID) != 0;
        result.cached = info->info.status == CD_DATA_STALE;
        result.fetchedAt = info->info.fetched_at_utc_ms / 1000;
        result.observedAt = info->info.observed_at_utc_ms;
        result.city = info->city ? info->city : "";
        const auto displayLocation = queryFrom(self.settings);
        if (displayLocation.mode == "coordinates" && !displayLocation.city.empty()) {
            result.city = displayLocation.city;
        }
        result.station = info->station ? info->station : "";
        result.url = info->station_url ? info->station_url : "";
        result.time = info->local_time ? info->local_time : "";
        result.dominant = info->dominant ? info->dominant : "";
        result.latitude = info->latitude;
        result.longitude = info->longitude;
        result.aqi = info->aqi;
        for (unsigned index = 0; index < 6; ++index) {
            result.pollutants[index] =
                (info->pollutant_valid_fields & (1u << index)) ? info->pollutant_indices[index] : -1;
        }
        if (info->sources && info->source_count <= 64) {
            for (uint32_t index = 0; index < info->source_count; ++index) {
                result.sources.push_back({info->sources[index].name ? info->sources[index].name : "",
                                          info->sources[index].url ? info->sources[index].url : ""});
            }
        }
        if (info->forecast && info->forecast_count <= 64) {
            for (uint32_t index = 0; index < info->forecast_count; ++index) {
                const auto& day = info->forecast[index];
                result.forecast.push_back({day.date ? day.date : "", day.average, day.low, day.high});
            }
        }
        result.error = info->info.error == CD_DATA_CREDENTIALS_MISSING ? Error::CredentialsMissing
                       : info->info.error == CD_DATA_UNAUTHORIZED      ? Error::Unauthorized
                       : info->info.error == CD_DATA_RATE_LIMITED      ? Error::RateLimited
                       : info->info.error == CD_DATA_NO_LOCATION       ? Error::LocationUnavailable
                       : info->info.error == CD_DATA_NO_DATA           ? Error::NoData
                       : info->info.error == CD_DATA_INVALID_RESPONSE  ? Error::InvalidResponse
                       : info->info.error                              ? Error::Network
                                                                       : Error::None;
        const bool loading = info->info.status == CD_DATA_LOADING;
        if (result != self.snapshot || loading != self.loading) {
            self.snapshot = std::move(result);
            self.loading = loading;
            self.redraw();
        }
    } catch (...) {
    }
}

void stop(Instance& self) {
    if (self.timer) {
        self.host.unsubscribe(self.context, self.timer);
    }
    self.timer = 0;
}

CdResult start(Instance& self) {
    const auto location = queryFrom(self.settings);
    const auto mode = location.mode == "auto"      ? CD_LOCATION_DEFAULT
                      : location.mode == "city"    ? CD_LOCATION_CITY
                      : location.mode == "station" ? CD_LOCATION_STATION
                                                   : CD_LOCATION_COORDINATES;
    CdLocationQuery query{sizeof(query),
                          mode,
                          location.latitude,
                          location.longitude,
                          location.city.c_str(),
                          location.station.c_str(),
                          location.credentials.c_str(),
                          location.language.c_str(),
                          uint32_t(self.settings.at("refresh_minutes").integer * 60),
                          5,
                          0,
                          0,
                          nullptr};
    return self.service.subscribe(self.context, &query, changed, &self, &self.timer);
}

void CD_CALL destroy(void* pointer) noexcept {
    if (auto self = std::unique_ptr<Instance>(static_cast<Instance*>(pointer))) {
        stop(*self);
    }
}

void CD_CALL pause(void* pointer, uint32_t paused) noexcept {
    try {
        if (!pointer) {
            return;
        }
        auto& self = *static_cast<Instance*>(pointer);
        if (self.paused == (paused != 0)) {
            return;
        }
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
    if (!pointer || !value || value->struct_size < sizeof(CdLayout)) {
        return;
    }
    auto& self = *static_cast<Instance*>(pointer);
    self.safe = value->safe_content;
    if (self.safe.width <= 0 || self.safe.height <= 0) {
        self.safe = {0, 0, value->logical_width, value->logical_height};
    }
    self.redraw();
}

void CD_CALL input(void* pointer, const CdInput* event) noexcept {
    try {
        if (!pointer || !event || event->struct_size < sizeof(CdInput)) {
            return;
        }
        auto& self = *static_cast<Instance*>(pointer);
        if (!self.paused && event->kind == CD_INPUT_DOUBLE_CLICK) {
            self.request(true);
        }
    } catch (...) {
    }
}

CdResult CD_CALL get(void* pointer, const char* id, CdSettingValue* value, uint32_t size) noexcept {
    return pointer ? static_cast<Instance*>(pointer)->settings.get(id, value, size) : CD_ERROR_ARGUMENT;
}

CdResult CD_CALL apply(void* pointer, const CdSettingValue* values, uint32_t count) noexcept {
    try {
        if (!pointer) {
            return CD_ERROR_ARGUMENT;
        }
        auto& self = *static_cast<Instance*>(pointer);
        auto changed = self.settings;
        const auto result = changed.apply(values, count);
        if (result != CD_OK) {
            return result;
        }
        auto query = queryFrom(changed);
        if (query.mode == "city" && query.city.find_first_not_of(" \t\r\n") == std::string::npos) {
            return CD_ERROR_ARGUMENT;
        }
        if (query.mode == "station" && !validStation(query.station)) {
            return CD_ERROR_ARGUMENT;
        }
        wide(query.city);
        wide(query.credentials);
        auto numerical = query;
        auto previous = queryFrom(self.settings);
        numerical.language.clear();
        previous.language.clear();
        const bool locationChanged = numerical != previous;
        const bool intervalChanged =
            changed.at("refresh_minutes").integer != self.settings.at("refresh_minutes").integer;
        self.settings = std::move(changed);
        if (locationChanged) {
            self.snapshot = {};
            if (!self.paused) {
                self.request();
            } else {
                stop(self);
            }
        } else if (intervalChanged && !self.paused) {
            self.request();
        }
        self.redraw();
        return CD_OK;
    } catch (...) {
        return CD_ERROR_INTERNAL;
    }
}

CdResult CD_CALL save(void* pointer, uint8_t* bytes, uint64_t capacity, uint64_t* size) noexcept {
    if (!pointer || !size) {
        return CD_ERROR_ARGUMENT;
    }
    *size = 4;
    if (!bytes && !capacity) {
        return CD_OK;
    }
    if (!bytes || capacity < 4) {
        return CD_ERROR_BUFFER;
    }
    std::memcpy(bytes, "AQI1", 4);
    return CD_OK;
}

CdResult CD_CALL restore(void* pointer, const uint8_t* bytes, uint64_t size) noexcept {
    if (!pointer || (size && (!bytes || size != 4 || std::memcmp(bytes, "AQI1", 4)))) {
        return CD_ERROR_ARGUMENT;
    }
    static_cast<Instance*>(pointer)->redraw();
    return CD_OK;
}

CdResult CD_CALL render(void* pointer, const CdFrame* frame) noexcept {
    try {
        if (!pointer || !frame || frame->struct_size < sizeof(CdFrame) || !frame->rgba || !frame->width ||
            !frame->height || frame->width > 4096 || frame->height > 4096 || frame->stride < frame->width * 4 ||
            frame->capacity < uint64_t(frame->stride) * frame->height || !std::isfinite(frame->device_pixel_ratio) ||
            frame->device_pixel_ratio <= 0 || frame->device_pixel_ratio > 8) {
            return CD_ERROR_ARGUMENT;
        }
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
            !context->host->request_redraw || !context->host->unsubscribe || !out || size < sizeof(CdInstanceApi)) {
            return CD_ERROR_ARGUMENT;
        }
        auto self = std::make_unique<Instance>(spec, std::filesystem::path(wide(context->plugin_directory)));
        self->host = *context->host;
        self->context = context->host_context;
        self->renderer.bind(*context);
        const auto serviceResult = cdsdk::Services(context->host, self->context).query(CD_SERVICE_AIR, self->service);
        if (serviceResult != CD_OK) {
            return serviceResult;
        }
        // Restore saved settings before the first network request.
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
}  // namespace aeris_air
