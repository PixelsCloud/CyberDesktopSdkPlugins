#ifndef CYBERDESKTOP_CPP_SERVICES_H
#define CYBERDESKTOP_CPP_SERVICES_H

#include <cstdint>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include "sdk/cpp/Snapshots.h"
#include "sdk/services/DataServices.h"

namespace cdsdk {
class Subscription {
public:
    Subscription();

    Subscription(const CdHostApi* host, void* context, uint64_t token);

    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;

    Subscription(Subscription&& other) noexcept;

    Subscription& operator=(Subscription&& other) noexcept;

    ~Subscription();

    void reset() noexcept;

    uint64_t token() const noexcept;

    explicit operator bool() const noexcept;

private:
    void swap(Subscription& other) noexcept;

    decltype(CdHostApi::unsubscribe) unsubscribe_ = nullptr;
    void* context_ = nullptr;
    uint64_t token_ = 0;
};

template <class Payload, class Raw>
class Watch {
public:
    explicit Watch(CdResult result = CD_ERROR_UNSUPPORTED) : result_(result) {}

    template <class Register>
    Watch(const CdHostApi* host, void* context, std::function<void(const Payload&)> callback, Register registration) {
        if (!callback) {
            result_ = CD_ERROR_ARGUMENT;
            return;
        }
        state_ = std::make_shared<State>();
        state_->callback = std::move(callback);
        uint64_t token = 0;
        if constexpr (std::is_same_v<Raw, AudioView>)
            result_ = registration(deliverAudio, state_.get(), &token);
        else if constexpr (std::is_same_v<Raw, SessionsView>)
            result_ = registration(deliverSessions, state_.get(), &token);
        else
            result_ = registration(deliver, state_.get(), &token);
        if (result_ == CD_OK) {
            if (!token)
                result_ = CD_ERROR_INTERNAL;
            else
                subscription_ = Subscription(host, context, token);
        }
    }

    Watch(Watch&&) noexcept = default;

    Watch& operator=(Watch&& other) noexcept {
        if (this != &other) {
            subscription_.reset();
            state_ = std::move(other.state_);
            subscription_ = std::move(other.subscription_);
            result_ = other.result_;
        }
        return *this;
    }

    CdResult result() const noexcept {
        return result_ != CD_OK ? result_ : state_ ? state_->callbackResult : CD_ERROR_INTERNAL;
    }

    uint64_t token() const noexcept {
        return subscription_.token();
    }

    void reset() noexcept {
        subscription_.reset();
        state_.reset();
        result_ = CD_ERROR_UNSUPPORTED;
    }

private:
    struct State : std::enable_shared_from_this<State> {
        std::function<void(const Payload&)> callback;
        CdResult callbackResult = CD_OK;
    };

    static void CD_CALL deliver(void* pointer, const Raw* data) noexcept {
        if (!pointer || !data)
            return;
        auto state = static_cast<State*>(pointer)->shared_from_this();
        uint32_t size;
        if constexpr (requires { data->info.struct_size; })
            size = data->info.struct_size;
        else
            size = data->struct_size;
        if (size < sizeof(Raw)) {
            state->callbackResult = CD_ERROR_VERSION;
            return;
        }
        try {
            const auto callback = state->callback;
            callback(Payload(*data));
        } catch (...) {
            state->callbackResult = CD_ERROR_INTERNAL;
        }
    }

    static void CD_CALL deliverAudio(void* pointer, const float* samples, uint32_t count, uint32_t rate,
                                     uint32_t channels) noexcept {
        if (!pointer)
            return;
        auto state = static_cast<State*>(pointer)->shared_from_this();
        if ((count && !samples) || count > 262144 || channels > 8) {
            state->callbackResult = CD_ERROR_ARGUMENT;
            return;
        }
        try {
            const auto callback = state->callback;
            callback(Payload(AudioView(samples, count, rate, channels)));
        } catch (...) {
            state->callbackResult = CD_ERROR_INTERNAL;
        }
    }

    static void CD_CALL deliverSessions(void* pointer, const CdMediaSession* entries, uint32_t count) noexcept {
        const SessionsView view{sizeof(SessionsView), entries, count};
        deliver(pointer, &view);
    }

    std::shared_ptr<State> state_;
    Subscription subscription_;
    CdResult result_ = CD_ERROR_UNSUPPORTED;
};

using ContentWatch = Watch<ContentSnapshot, CdContentInfo>;
using VolumeWatch = Watch<VolumeSnapshot, CdVolumeInfo>;
using InputWatch = Watch<CdInputStats, CdInputStats>;
using SystemWatch = Watch<SystemSnapshot, CdSystemInfo>;
using MediaWatch = Watch<MediaSnapshot, CdMediaInfo>;
using LocationWatch = Watch<LocationSnapshot, CdLocationInfo>;
using WeatherWatch = Watch<WeatherSnapshot, CdWeatherInfo>;
using AirWatch = Watch<AirSnapshot, CdAirInfo>;
using AudioWatch = Watch<AudioSnapshot, AudioView>;
using SessionsWatch = Watch<SessionSnapshot, SessionsView>;
using AudioStatusWatch = Watch<CdAudioStatus, CdAudioStatus>;

class Services {
public:
    Services(const CdHostApi* host, void* context);

    template <class Table>
    CdResult query(const char* id, Table& table, uint32_t version = CD_SERVICE_VERSION) const noexcept {
        table = {};
        const auto result = queryRaw(id, version, &table, sizeof(table));
        if (result != CD_OK)
            return result;
        if (table.struct_size < sizeof(table))
            return CD_ERROR_BUFFER;
        return table.version == version ? CD_OK : CD_ERROR_VERSION;
    }

    Subscription adopt(uint64_t token) const noexcept;

    void* context() const noexcept;

    void requestRedraw() const noexcept;

    AudioWatch audio(const char* service, const CdAudioOptions& options,
                     std::function<void(const AudioSnapshot&)> callback) const;

    SessionsWatch sessions(std::function<void(const SessionSnapshot&)> callback) const;

    AudioStatusWatch audioStatus(const char* service, const CdAudioOptions& options,
                                 std::function<void(const CdAudioStatus&)> callback) const;

    CdResult command(uint64_t subscription, uint32_t command, double value, uint64_t* requestId) const noexcept;

    ContentWatch content(const CdContentQuery& options, std::function<void(const ContentSnapshot&)> callback) const;
    CdResult refreshContent(uint64_t token) const noexcept;
    CdResult openUrl(const char* url) const noexcept;

    VolumeWatch volume(const CdVolumeQuery& options, std::function<void(const VolumeSnapshot&)> callback) const;

    InputWatch input(std::function<void(const CdInputStats&)> callback) const;

    SystemWatch system(uint32_t interval, std::function<void(const SystemSnapshot&)> callback) const;

    MediaWatch media(const char* source, std::function<void(const MediaSnapshot&)> callback) const;

    LocationWatch location(const CdLocationQuery& options, std::function<void(const LocationSnapshot&)> callback) const;

    WeatherWatch weather(const CdLocationQuery& options, std::function<void(const WeatherSnapshot&)> callback) const;

    AirWatch air(const CdLocationQuery& options, std::function<void(const AirSnapshot&)> callback) const;

private:
    CdResult queryRaw(const char* id, uint32_t version, void* table, uint32_t size) const noexcept;

    CdHostApi host_{};
    decltype(CdServiceHostApi::query_service) queryService_ = nullptr;
    void* context_ = nullptr;
};

}  // namespace cdsdk
#endif
