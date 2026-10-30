#include "plugins/beat-machine/tools/TestHost.h"

Host::Host() {
    auto& base = api.input.media.base;
    base.struct_size = sizeof(api);
    base.abi_version = 1;
    base.request_redraw = [](void* host) {
        ++static_cast<Host*>(host)->redraws;
    };
    base.subscribe_timer = [](void* host, uint32_t interval, CdTimerCallback callback, void* user,
                              uint64_t* output) -> CdResult {
        auto& self = *static_cast<Host*>(host);
        *output = ++self.token;
        self.timers.emplace(*output, Timer{interval, self.now + interval, callback, user});
        return CD_OK;
    };
    base.unsubscribe = [](void* host, uint64_t value) {
        auto& self = *static_cast<Host*>(host);
        self.timers.erase(value);
        self.audio.erase(value);
        self.status.erase(value);
    };
    api.query_service = [](void*, const char* name, uint32_t version, void* output, uint32_t size) -> CdResult {
        if (version != 1) {
            return CD_ERROR_UNSUPPORTED;
        }
        if (std::string_view(name) == CD_SERVICE_ENVIRONMENT && size >= sizeof(CdEnvironmentApi)) {
            *static_cast<CdEnvironmentApi*>(output) = {sizeof(CdEnvironmentApi), 1,
                                                       [](void* host, CdEnvironmentSnapshot* value) -> CdResult {
                                                           *value = static_cast<Host*>(host)->environment;
                                                           return CD_OK;
                                                       }};
            return CD_OK;
        }
        if (std::string_view(name) == CD_SERVICE_INTERACTION && size >= sizeof(CdInteractionApi)) {
            *static_cast<CdInteractionApi*>(output) = {sizeof(CdInteractionApi), 1,
                                                       [](void* host, uint32_t enable) -> CdResult {
                                                           static_cast<Host*>(host)->enabled = enable != 0;
                                                           return CD_OK;
                                                       },
                                                       [](void* host, uint32_t capture) -> CdResult {
                                                           auto& self = *static_cast<Host*>(host);
                                                           if (capture && !self.captureAllowed) {
                                                               return CD_ERROR_UNSUPPORTED;
                                                           }
                                                           self.captured = capture != 0;
                                                           return CD_OK;
                                                       }};
            return CD_OK;
        }
        if (std::string_view(name) == CD_SERVICE_FFT && size >= sizeof(CdAudioServiceApi)) {
            *static_cast<CdAudioServiceApi*>(output) = {
                sizeof(CdAudioServiceApi), 1,
                [](void* host, const CdAudioOptions*, CdAudioCallback callback, void* user,
                   uint64_t* token) -> CdResult {
                    auto& self = *static_cast<Host*>(host);
                    *token = ++self.token;
                    self.audio.emplace(*token, Audio{callback, user});
                    return CD_OK;
                },
                [](void* host, const CdAudioOptions*, CdAudioStatusCallback callback, void* user,
                   uint64_t* token) -> CdResult {
                    auto& self = *static_cast<Host*>(host);
                    *token = ++self.token;
                    self.status.emplace(*token, Status{callback, user});
                    return CD_OK;
                }};
            return CD_OK;
        }
        return CD_ERROR_UNSUPPORTED;
    };
}

auto Host::send(float value) -> void {
    std::array<float, 63> bands;
    bands.fill(value);
    const auto copy = audio;
    for (const auto& [id, sub] : copy) {
        if (audio.contains(id)) {
            sub.callback(sub.user, bands.data(), 63, 48000, 0);
        }
    }
}

auto Host::advance(uint32_t milliseconds) -> void {
    now += milliseconds;
    environment.monotonic_ms = now;
    environment.unix_ms += milliseconds;
    const auto copy = timers;
    for (const auto& [id, timer] : copy) {
        if (timers.contains(id) && now >= timer.due) {
            timers.at(id).due = now + timer.interval;
            timer.callback(timer.user, now);
        }
    }
}

auto Host::context(const char* id) -> CdCreateContext {
    return {sizeof(CdCreateContext), 1, id, "test", CD_BEAT_TEST_ROOT, &api.input.media.base, this};
}