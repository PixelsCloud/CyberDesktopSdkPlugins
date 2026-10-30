#include "sdk/cpp/Services.h"

#include <utility>

cdsdk::Subscription::operator bool() const noexcept {
    return token_ != 0;
}

namespace cdsdk {
Subscription::Subscription() = default;

Subscription::Subscription(const CdHostApi* host, void* context, uint64_t token) : context_(context), token_(token) {
    if (host && host->abi_version == CD_PLUGIN_ABI_VERSION && host->struct_size >= sizeof(CdHostApi)) {
        unsubscribe_ = host->unsubscribe;
    }
}

Subscription::Subscription(Subscription&& other) noexcept {
    swap(other);
}

auto Subscription::operator=(Subscription&& other) noexcept -> Subscription& {
    if (this != &other) {
        reset();
        swap(other);
    }
    return *this;
}

Subscription::~Subscription() {
    reset();
}

auto Subscription::reset() noexcept -> void {
    const auto token = std::exchange(token_, 0);
    if (token && unsubscribe_) {
        unsubscribe_(context_, token);
    }
}

auto Subscription::token() const noexcept -> uint64_t {
    return token_;
}

auto Subscription::swap(Subscription& other) noexcept -> void {
    std::swap(unsubscribe_, other.unsubscribe_);
    std::swap(context_, other.context_);
    std::swap(token_, other.token_);
}

Services::Services(const CdHostApi* host, void* context) : context_(context) {
    if (!host || host->abi_version != CD_PLUGIN_ABI_VERSION || host->struct_size < sizeof(CdHostApi)) {
        return;
    }
    host_ = *host;
    if (host->struct_size >= sizeof(CdServiceHostApi)) {
        queryService_ = reinterpret_cast<const CdServiceHostApi*>(host)->query_service;
    }
}

auto Services::adopt(uint64_t token) const noexcept -> Subscription {
    return Subscription(&host_, context_, token);
}

auto Services::context() const noexcept -> void* {
    return context_;
}

auto Services::requestRedraw() const noexcept -> void {
    if (host_.request_redraw) {
        host_.request_redraw(context_);
    }
}

auto Services::audio(const char* service, const CdAudioOptions& options,
                     std::function<void(const AudioSnapshot&)> callback) const -> AudioWatch {
    CdAudioServiceApi api{};
    const auto result = query(service, api);
    if (result != CD_OK || !api.subscribe)
        return AudioWatch(result == CD_OK ? CD_ERROR_UNSUPPORTED : result);
    return AudioWatch(&host_, context_, std::move(callback), [&](auto delivery, void* user, uint64_t* token) {
        return api.subscribe(context_, &options, delivery, user, token);
    });
}

auto Services::sessions(std::function<void(const SessionSnapshot&)> callback) const -> SessionsWatch {
    CdSessionsServiceApi api{};
    const auto result = query(CD_SERVICE_SESSIONS, api);
    if (result != CD_OK || !api.subscribe)
        return SessionsWatch(result == CD_OK ? CD_ERROR_UNSUPPORTED : result);
    return SessionsWatch(&host_, context_, std::move(callback), [&](auto delivery, void* user, uint64_t* token) {
        return api.subscribe(context_, delivery, user, token);
    });
}

auto Services::audioStatus(const char* service, const CdAudioOptions& options,
                           std::function<void(const CdAudioStatus&)> callback) const -> AudioStatusWatch {
    CdAudioServiceApi api{};
    const auto result = query(service, api);
    if (result != CD_OK || !api.subscribe_status)
        return AudioStatusWatch(result == CD_OK ? CD_ERROR_UNSUPPORTED : result);
    return AudioStatusWatch(&host_, context_, std::move(callback), [&](auto delivery, void* user, uint64_t* token) {
        return api.subscribe_status(context_, &options, delivery, user, token);
    });
}

auto Services::command(uint64_t subscription, uint32_t command, double value, uint64_t* requestId) const noexcept
    -> CdResult {
    if (!requestId)
        return CD_ERROR_ARGUMENT;
    *requestId = 0;
    CdMediaServiceApi api{};
    const auto result = query(CD_SERVICE_MEDIA, api);
    return result != CD_OK       ? result
           : api.command_request ? api.command_request(context_, subscription, command, value, requestId)
                                 : CD_ERROR_UNSUPPORTED;
}

auto Services::input(std::function<void(const CdInputStats&)> callback) const -> InputWatch {
    CdInputServiceApi api{};
    const auto result = query(CD_SERVICE_INPUT, api);
    if (result != CD_OK || !api.subscribe)
        return InputWatch(result == CD_OK ? CD_ERROR_UNSUPPORTED : result);
    return InputWatch(&host_, context_, std::move(callback), [&](auto delivery, void* user, uint64_t* token) {
        return api.subscribe(context_, delivery, user, token);
    });
}

auto Services::system(uint32_t interval, std::function<void(const SystemSnapshot&)> callback) const -> SystemWatch {
    CdSystemServiceApi api{};
    const auto result = query(CD_SERVICE_SYSTEM, api);
    if (result != CD_OK || !api.subscribe)
        return SystemWatch(result == CD_OK ? CD_ERROR_UNSUPPORTED : result);
    return SystemWatch(&host_, context_, std::move(callback), [&](auto delivery, void* user, uint64_t* token) {
        return api.subscribe(context_, interval, delivery, user, token);
    });
}

auto Services::media(const char* source, std::function<void(const MediaSnapshot&)> callback) const -> MediaWatch {
    CdMediaServiceApi api{};
    const auto result = query(CD_SERVICE_MEDIA, api);
    if (result != CD_OK || !api.subscribe)
        return MediaWatch(result == CD_OK ? CD_ERROR_UNSUPPORTED : result);
    return MediaWatch(&host_, context_, std::move(callback), [&](auto delivery, void* user, uint64_t* token) {
        return api.subscribe(context_, source, delivery, user, token);
    });
}

auto Services::location(const CdLocationQuery& options, std::function<void(const LocationSnapshot&)> callback) const
    -> LocationWatch {
    CdLocationServiceApi api{};
    const auto result = query(CD_SERVICE_LOCATION, api);
    if (result != CD_OK || !api.subscribe)
        return LocationWatch(result == CD_OK ? CD_ERROR_UNSUPPORTED : result);
    return LocationWatch(&host_, context_, std::move(callback), [&](auto delivery, void* user, uint64_t* token) {
        return api.subscribe(context_, &options, delivery, user, token);
    });
}

ContentWatch Services::content(const CdContentQuery& options,
                               std::function<void(const ContentSnapshot&)> callback) const {
    CdContentServiceApi api{};
    const auto result = query(CD_SERVICE_CONTENT, api);
    if (result != CD_OK || !api.subscribe) {
        return ContentWatch(result == CD_OK ? CD_ERROR_UNSUPPORTED : result);
    }
    return ContentWatch(&host_, context_, std::move(callback), [&](auto deliver, void* user, uint64_t* token) {
        return api.subscribe(context_, &options, deliver, user, token);
    });
}

CdResult Services::refreshContent(uint64_t token) const noexcept {
    CdContentServiceApi api{};
    const auto result = query(CD_SERVICE_CONTENT, api);
    return result != CD_OK ? result : api.refresh ? api.refresh(context_, token) : CD_ERROR_UNSUPPORTED;
}

CdResult Services::openUrl(const char* url) const noexcept {
    CdContentServiceApi api{};
    const auto result = query(CD_SERVICE_CONTENT, api);
    return result != CD_OK ? result : api.open_url ? api.open_url(context_, url) : CD_ERROR_UNSUPPORTED;
}

auto Services::volume(const CdVolumeQuery& options, std::function<void(const VolumeSnapshot&)> callback) const
    -> VolumeWatch {
    CdVolumeServiceApi api{};
    const auto result = query(CD_SERVICE_VOLUME, api);
    if (result != CD_OK || !api.subscribe) {
        return VolumeWatch(result == CD_OK ? CD_ERROR_UNSUPPORTED : result);
    }
    return VolumeWatch(&host_, context_, std::move(callback), [&](auto delivery, void* user, uint64_t* token) {
        return api.subscribe(context_, &options, delivery, user, token);
    });
}

auto Services::weather(const CdLocationQuery& options, std::function<void(const WeatherSnapshot&)> callback) const
    -> WeatherWatch {
    CdWeatherServiceApi api{};
    const auto result = query(CD_SERVICE_WEATHER, api);
    if (result != CD_OK || !api.subscribe)
        return WeatherWatch(result == CD_OK ? CD_ERROR_UNSUPPORTED : result);
    return WeatherWatch(&host_, context_, std::move(callback), [&](auto delivery, void* user, uint64_t* token) {
        return api.subscribe(context_, &options, delivery, user, token);
    });
}

auto Services::air(const CdLocationQuery& options, std::function<void(const AirSnapshot&)> callback) const -> AirWatch {
    CdAirServiceApi api{};
    const auto result = query(CD_SERVICE_AIR, api);
    if (result != CD_OK || !api.subscribe)
        return AirWatch(result == CD_OK ? CD_ERROR_UNSUPPORTED : result);
    return AirWatch(&host_, context_, std::move(callback), [&](auto delivery, void* user, uint64_t* token) {
        return api.subscribe(context_, &options, delivery, user, token);
    });
}
}  // namespace cdsdk

namespace cdsdk {
CdResult Services::queryRaw(const char* id, uint32_t version, void* table, uint32_t size) const noexcept {
    if (!queryService_) {
        return CD_ERROR_UNSUPPORTED;
    }
    return queryService_(context_, id, version, table, size);
}
}  // namespace cdsdk
